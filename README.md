# EventEdge

[![CI](https://github.com/ErfanSadri/eventedge/actions/workflows/ci.yml/badge.svg)](https://github.com/ErfanSadri/eventedge/actions/workflows/ci.yml)

EventEdge is a C++20 asynchronous reverse proxy, load balancer, and response cache for bursty live-event traffic.

## Why EventEdge

During a sports event, concert, esports match, livestream, or product launch, many clients can request the same backend data at once. EventEdge sits in front of those backends: it sends work only to healthy servers, caches safe `GET` responses, collapses simultaneous cold misses into one upstream fetch, bounds slow work with timeouts, and exposes operational metrics.

## Features

- Asynchronous TCP and HTTP/1.1 with Boost.Asio and Boost.Beast.
- Configurable multithreaded `io_context` runtime and per-session strand serialization.
- Health-aware round-robin upstream selection with active TCP checks.
- Explicit connect, write, and response-read deadlines with 502, 503, and 504 handling.
- Custom thread-safe LRU response cache (2-second TTL, 1,024 entries) and process-local request coalescing.
- Prometheus-compatible `/metrics` endpoint, Dockerized upstream fixtures, GoogleTest coverage, sanitizer presets, and GitHub Actions CI.

## Architecture

```text
Clients
   |
   v
EventEdge
   +-- local /health
   +-- local /metrics
   +-- response cache --> request coalescer
   +-- upstream pool + health monitor
          |--> Backend A
          |--> Backend B
          `--> Backend C
```

## Quick start

Prerequisites: CMake 3.24+, a C++20 compiler, Unix build tools, Boost headers, Docker Compose, and `curl` for the local fixture demo.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure

docker compose up -d --build
./build/debug/eventedge 127.0.0.1 8080 4 \
  127.0.0.1:19001 127.0.0.1:19002 127.0.0.1:19003
```

In another terminal:

```sh
curl -i http://127.0.0.1:8080/health
curl -s http://127.0.0.1:8080/metrics
curl -i http://127.0.0.1:8080/identity
```

Stop EventEdge with `Ctrl-C`, then clean up the fixtures with:

```sh
docker compose down
```

With no arguments, EventEdge listens on `127.0.0.1:8080`, uses at least one hardware-concurrency worker, and proxies to `127.0.0.1:9000`. Supply `listen-address listen-port workers` followed by one or more `host:port` upstreams to override those defaults.

## Metrics and reliability

`GET /health` and `GET /metrics` are local routes. Metrics include request, cache, coalescing, gateway-failure, upstream-selection/health, in-flight proxy, and request-duration data in Prometheus text format.

Upstreams start provisionally healthy, then periodic TCP probes (2-second interval and 1-second connect timeout) determine eligibility. EventEdge rotates across healthy upstreams; no healthy upstream returns `503`, a normal selected-upstream failure returns `502`, and a response that exceeds the configured deadline returns `504`. Default budgets are 2 seconds for connect, 2 seconds for write, and 5 seconds for response read. Failed requests are not retried.

Only unauthenticated proxied `GET` requests are cacheable. EventEdge stores eligible `200 OK` responses that do not set cookies or mark themselves `no-store`/`private`; cache hits happen before upstream selection. Matching simultaneous misses have one leader fetch the response while waiters receive that result. The cache and coalescer are process-local.

## Performance

The following are **local development-machine measurements**, not production capacity claims:

- Three-upstream median: **801.78 req/s** at c16.
- Concurrency-sweep peak: **890.59 req/s** at c64.
- Cache-heavy median: **1,724.67 req/s** with zero failures.
- Coalescing reduced **64 simultaneous cold misses to one upstream fetch**.
- Sustained 30-second run: **26,173 requests**, **871.43 req/s**, zero failures, and **p99 102 ms**.

See [the benchmark report](docs/benchmarks.md) for methodology, local-fixture limitations, and retained invalid single-upstream trials.

## Testing

The normal suite has 43 unit, integration, concurrency, and lifecycle tests. Docker fixtures support controlled upstream failure/recovery simulation. CMake presets support ASan and TSan builds:

```sh
cmake --preset asan && cmake --build --preset asan
cmake --preset tsan && cmake --build --preset tsan
```

GitHub Actions is configured to run Debug tests, a Release build, shell checks, the benchmark parser self-check, and ASan/TSan test jobs on Linux. Local macOS sanitizer binaries may build but have a platform runtime issue, so this repository does not claim local sanitizer runtime success.

## Project structure

```text
include/eventedge/  Public interfaces and core types
src/                Proxy, server, cache, coalescer, metrics, and health monitor
tests/              GoogleTest unit and loopback integration coverage
scripts/            Docker demo and local benchmark harness
docker/             Controllable upstream fixture image
docs/               Scope and benchmark methodology/results
.github/workflows/  GitHub Actions CI
```

## Design decisions and limitations

EventEdge deliberately keeps its current scope focused: HTTP/1.1 only, no TLS, static upstream configuration, process-local cache/coalescing/metrics, and a new upstream connection per proxied request. Connection pooling, retries/failover, circuit breakers, distributed coordination, passive health scoring, external monitoring deployment, streaming bodies, HTTP/2, and TLS are not implemented.

For a hands-on failure demo, start the Compose fixtures above and use `docker compose stop backend-b` / `docker compose start backend-b`. The fixture exposes `/identity`, `/counted/<resource>`, `/counted-slow/<milliseconds>/<resource>`, `/slow/<milliseconds>`, and `/status/<code>`.
