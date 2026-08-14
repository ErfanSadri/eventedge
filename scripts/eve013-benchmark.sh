#!/usr/bin/env bash
set -euo pipefail

requests="${REQUESTS:-10000}"
connections="${CONNECTIONS:-64}"
baseline_connections="${BASELINE_CONNECTIONS:-16}"
sweep_requests="${SWEEP_REQUESTS:-3000}"
sustained_seconds="${SUSTAINED_SECONDS:-30}"
workers="${WORKERS:-4}"
warmup_requests="${WARMUP_REQUESTS:-256}"
warmup_connections="${WARMUP_CONNECTIONS:-8}"
results_dir="${RESULTS_DIR:-benchmark-results/$(date +%Y%m%d-%H%M%S)}"
edge_pid=""
valid_trials=0
invalid_trials=0
invalid_trial_paths=()

parse_ab_failures() {
  awk '
    BEGIN { non_2xx = connect = receive = length_failures = exceptions = 0 }
    /^[[:space:]]*Non-2xx responses:/ { non_2xx = $3 }
    /^[[:space:]]*\(Connect:/ {
      gsub(/[(),:]/, " ")
      for (field = 1; field <= NF; ++field) {
        if ($field == "Connect") connect = $(field + 1)
        if ($field == "Receive") receive = $(field + 1)
        if ($field == "Length") length_failures = $(field + 1)
        if ($field == "Exceptions") exceptions = $(field + 1)
      }
    }
    END { print non_2xx, connect, receive, length_failures, exceptions }
  '
}

ab_trial_is_valid() {
  local non_2xx connect receive length_failures exceptions
  read -r non_2xx connect receive length_failures exceptions < <(parse_ab_failures)
  (( non_2xx == 0 && connect == 0 && receive == 0 && exceptions == 0 ))
}

ab_parser_self_check() {
  local name expected sample actual
  while IFS='|' read -r name expected sample; do
    if printf '%b\n' "$sample" | ab_trial_is_valid; then actual=valid; else actual=invalid; fi
    [[ "$actual" == "$expected" ]] || { echo "parser self-check failed: $name" >&2; return 1; }
  done <<'EOF'
clean|valid|Failed requests: 0
length-only|valid|Failed requests: 12\n   (Connect: 0, Receive: 0, Length: 12, Exceptions: 0)
non-2xx|invalid|Non-2xx responses: 5
connect|invalid|   (Connect: 2, Receive: 0, Length: 0, Exceptions: 0)
EOF
  echo 'ApacheBench parser self-check passed'
}

if [[ "${1:-}" == '--parser-self-check' ]]; then
  ab_parser_self_check
  exit 0
fi

cleanup() {
  if [[ -n "$edge_pid" ]]; then kill -INT "$edge_pid" 2>/dev/null || true; wait "$edge_pid" 2>/dev/null || true; fi
  docker compose down || true
}
print_validation_summary() {
  echo "VALID trials: $valid_trials"
  echo "INVALID trials: $invalid_trials"
  for path in "${invalid_trial_paths[@]}"; do echo "INVALID: $path"; done
}
on_exit() { local status=$?; print_validation_summary; cleanup; return "$status"; }
trap on_exit EXIT

for command in ab docker curl; do command -v "$command" >/dev/null || { echo "missing: $command" >&2; exit 1; }; done
[[ -x ./build/release/eventedge ]] || { echo "build Release first" >&2; exit 1; }
mkdir -p "$results_dir"

value() { "$@" 2>/dev/null || echo unavailable; }
{
  echo "timestamp=$(date -Iseconds)"; echo "uname=$(uname -a)"; echo "macos=$(value sw_vers)"
  echo "cpu=$(value sysctl -n machdep.cpu.brand_string)"; echo "logical_cpus=$(value sysctl -n hw.logicalcpu)"
  echo "memory_bytes=$(value sysctl -n hw.memsize)"; echo "compiler=$(value /usr/bin/c++ --version)"
  echo "cmake=$(value cmake --version)"; echo "branch=$(value git branch --show-current)"; echo "commit=$(value git rev-parse HEAD)"
  echo "docker=$(value docker --version)"; echo "compose=$(value docker compose version)"; echo "ab=$(value ab -V)"
  echo "fixture_image=python:3.13-slim"; echo "release_binary=$(realpath ./build/release/eventedge)"
  echo "requests=$requests baseline_connections=$baseline_connections connections=$connections workers=$workers sweep_requests=$sweep_requests sustained_seconds=$sustained_seconds warmup_requests=$warmup_requests warmup_connections=$warmup_connections"
} >"$results_dir/environment.txt"

wait_http() { local url=$1; until curl -fsS "$url" >/dev/null; do sleep 1; done; }
wait_backends() { for port in 19001 19002 19003; do wait_http "http://127.0.0.1:$port/identity"; done; }
reset_backends() { docker compose restart "$@"; wait_backends; }
wait_edge_health() { local metrics_file; metrics_file=$(mktemp); until curl -fsS http://127.0.0.1:8080/metrics >"$metrics_file" && ! grep -q 'eventedge_upstream_healthy.* 0$' "$metrics_file"; do sleep 1; done; rm -f "$metrics_file"; }
start_edge() { local name=$1; shift; [[ -z "$edge_pid" ]] || { echo "EventEdge already running" >&2; exit 1; }; if command -v lsof >/dev/null && lsof -nP -iTCP:8080 -sTCP:LISTEN >/dev/null 2>&1; then echo 'port 8080 is already in use' >&2; exit 1; fi; ./build/release/eventedge 127.0.0.1 8080 "$workers" "$@" >"$results_dir/$name-eventedge.log" 2>&1 & edge_pid=$!; wait_http http://127.0.0.1:8080/health; }
stop_edge() { kill -INT "$edge_pid"; wait "$edge_pid"; edge_pid=""; }
metrics() { curl -fsS http://127.0.0.1:8080/metrics >"$1"; }
warm_up() { local url=$1; shift; echo "warm-up: $url"; ab -n "$warmup_requests" -c "$warmup_connections" "$@" "$url" >/dev/null; }
mark_valid() { valid_trials=$((valid_trials + 1)); }
mark_invalid() { invalid_trials=$((invalid_trials + 1)); invalid_trial_paths+=("$1"); }
ab_run() {
  local output=$1 ab_exit=0
  shift
  echo "ab $*"
  ab "$@" | tee "$output" || ab_exit=$?
  if [[ $ab_exit -ne 0 ]] || ! ab_trial_is_valid <"$output"; then
    echo "INVALID trial: non-2xx, connect, receive, exception, or ApacheBench execution failure; raw output retained: $output" >&2
    mark_invalid "$output"
  else
    mark_valid
  fi
}
warmup_target() { local url=$1 token=$2; if [[ "$url" == *\?* ]]; then echo "${url}&eve013-warmup=$token"; else echo "${url}?eve013-warmup=$token"; fi; }
trial_set() { local dir=$1 url=$2 reset_services=$3 trial_requests=$4 trial_connections=$5; shift 5; mkdir -p "$dir"; for trial in 1 2 3; do reset_backends $reset_services; [[ -z "$edge_pid" ]] || wait_edge_health; warm_up "$(warmup_target "$url" "$trial")" "$@"; ab_run "$dir/trial-$trial.txt" -n "$trial_requests" -c "$trial_connections" "$@" "$url"; done; }
metric_value() { local file=$1 name=$2; awk -v name="$name" '$1 == name { print $2; found=1; exit } END { if (!found) exit 1 }' "$file"; }
metric_delta() { local before=$1 after=$2 name=$3; local before_value after_value; before_value=$(metric_value "$before" "$name"); after_value=$(metric_value "$after" "$name"); echo $((after_value - before_value)); }
validate_coalescing_burst() {
  local n=$1 key=$2 before=$3 after=$4 logs=$5 dir=$6
  local leaders waiters proxy_requests cache_misses upstream_requests failed=0
  leaders=$(metric_delta "$before" "$after" eventedge_coalescing_leaders_total)
  waiters=$(metric_delta "$before" "$after" eventedge_coalescing_waiters_total)
  proxy_requests=$(metric_delta "$before" "$after" eventedge_proxy_requests_total)
  cache_misses=$(metric_delta "$before" "$after" eventedge_cache_misses_total)
  upstream_requests=$(grep -F -c "GET /counted-slow/1000/$key" "$logs" || true)
  for client in $(seq 1 "$n"); do [[ $(<"$dir/burst-$n-client-$client.status") == 200 ]] || failed=1; done
  {
    echo "leader_delta=$leaders expected=1"; echo "waiter_delta=$waiters expected=$((n - 1))"
    echo "proxy_request_delta=$proxy_requests expected=1"; echo "cache_miss_delta=$cache_misses expected=$n"
    echo "upstream_requests_for_key=$upstream_requests expected=1"; echo "all_clients_success=$((failed == 0)) expected=1"
  } >"$dir/burst-$n-validation.txt"
  if [[ $leaders -ne 1 || $waiters -ne $((n - 1)) || $proxy_requests -ne 1 || $cache_misses -ne $n || $upstream_requests -ne 1 || $failed -ne 0 ]]; then
    echo "INVALID coalescing burst ($n clients); evidence retained in $dir" >&2
    return 1
  fi
}

docker compose up -d --build
wait_backends

echo 'direct backend'; trial_set "$results_dir/direct" http://127.0.0.1:19001/identity backend-a "$requests" "$baseline_connections"
echo 'single upstream uncached'; start_edge single 127.0.0.1:19001; trial_set "$results_dir/single-upstream" http://127.0.0.1:8080/identity backend-a "$requests" "$baseline_connections" -H 'Authorization: Bearer benchmark'; stop_edge
echo 'three upstream uncached'; start_edge three 127.0.0.1:19001 127.0.0.1:19002 127.0.0.1:19003; metrics "$results_dir/three-before.metrics"; trial_set "$results_dir/three-upstream" http://127.0.0.1:8080/identity 'backend-a backend-b backend-c' "$requests" "$baseline_connections" -H 'Authorization: Bearer benchmark'; metrics "$results_dir/three-after.metrics"
echo 'concurrency sweep'; mkdir -p "$results_dir/concurrency"; for c in 1 16 64 128; do reset_backends backend-a backend-b backend-c; wait_edge_health; warm_up "http://127.0.0.1:8080/identity?eve013-warmup=sweep-$c" -H 'Authorization: Bearer benchmark'; ab_run "$results_dir/concurrency/c$c.txt" -n "$sweep_requests" -c "$c" -H 'Authorization: Bearer benchmark' http://127.0.0.1:8080/identity; done
echo 'cache-heavy'; reset_backends backend-a backend-b backend-c; wait_edge_health; warm_up 'http://127.0.0.1:8080/identity?eve013-warmup=cache' -H 'Authorization: Bearer benchmark'; mkdir -p "$results_dir/cache"; metrics "$results_dir/cache/before.metrics"; trial_set "$results_dir/cache" 'http://127.0.0.1:8080/identity?eve013-hot=1' 'backend-a backend-b backend-c' "$requests" "$connections"; metrics "$results_dir/cache/after.metrics"
echo 'coalescing bursts'; mkdir -p "$results_dir/coalescing"; for n in 16 32 64; do key="eve013-burst-$(date +%s)-$n"; reset_backends backend-a backend-b backend-c; wait_edge_health; warm_up "http://127.0.0.1:8080/identity?eve013-warmup=coalescing-$n" -H 'Authorization: Bearer benchmark'; before="$results_dir/coalescing/before-$n.metrics"; after="$results_dir/coalescing/after-$n.metrics"; logs="$results_dir/coalescing/backend-logs-$n.txt"; metrics "$before"; pids=(); for client in $(seq 1 "$n"); do { curl -sS -o "$results_dir/coalescing/burst-$n-client-$client.txt" -w '%{http_code}\n' "http://127.0.0.1:8080/counted-slow/1000/$key" >"$results_dir/coalescing/burst-$n-client-$client.status" || true; } & pids+=("$!"); done; for pid in "${pids[@]}"; do wait "$pid"; done; metrics "$after"; docker compose logs --no-color >"$logs"; if validate_coalescing_burst "$n" "$key" "$before" "$after" "$logs" "$results_dir/coalescing"; then mark_valid; else mark_invalid "$results_dir/coalescing/burst-$n-validation.txt"; fi; done
echo 'sustained uncached'; reset_backends backend-a backend-b backend-c; wait_edge_health; warm_up 'http://127.0.0.1:8080/identity?eve013-warmup=sustained' -H 'Authorization: Bearer benchmark'; mkdir -p "$results_dir/sustained"; metrics "$results_dir/sustained/before.metrics"; ps -o pid,rss,command -p "$edge_pid" >"$results_dir/sustained/rss-before.txt"; ab_run "$results_dir/sustained/ab.txt" -t "$sustained_seconds" -c "$connections" -H 'Authorization: Bearer benchmark' http://127.0.0.1:8080/identity; ps -o pid,rss,command -p "$edge_pid" >"$results_dir/sustained/rss-after.txt"; metrics "$results_dir/sustained/after.metrics"
stop_edge
if [[ "${RUN_WORKER_SWEEP:-0}" == 1 ]]; then echo 'worker sweep requires rerun with WORKERS=1,4,8; raw layout reserved under workers/'; fi
echo "raw results: $results_dir"
