#!/usr/bin/env bash
set -euo pipefail

docker compose up -d --build
for port in 19001 19002 19003; do
  until curl --fail --silent "http://127.0.0.1:${port}/identity" >/dev/null; do sleep 1; done
done
docker compose ps
cat <<'EOF'
Run EventEdge natively:
  ./build/debug/eventedge 127.0.0.1 8080 4 127.0.0.1:19001 127.0.0.1:19002 127.0.0.1:19003

Try distinct /identity or /counted paths for round robin, /slow/6000 for 504,
and /counted-slow/1000/live with a small concurrent curl burst for coalescing.
Use `docker compose stop backend-b`, `docker compose start backend-b`, and
`docker compose down` for failure/recovery and cleanup.
EOF
