# EventEdge

EventEdge is a high-performance C++20 reverse proxy, load balancer, and cache designed for bursty live-event traffic.

## Status

EventEdge uses asynchronous Boost.Asio/Beast networking with one `io_context` run by a configurable multithreaded runtime. Per-session strand serialization protects a local `GET /health` endpoint and reverse proxying across multiple static upstream backends with thread-safe round-robin selection. Active asynchronous TCP health checks exclude unhealthy backends and automatically return recovered backends to rotation.

Explicit proxy timeouts now bound upstream connect, write, and response-read stages. EventEdge also has a custom thread-safe in-memory LRU response cache with a 2-second TTL and 1024-entry capacity, process-local request coalescing, and Prometheus-compatible runtime metrics. Request retry/failover, passive failure scoring, circuit breakers, connection pooling, and measured performance claims are not implemented yet.

## Planned technical direction

Future milestones will add richer resilience policies, cache-stampede protection, metrics, and Linux-focused validation.

## Build and test

Prerequisites: CMake, a C++20 compiler, standard Unix build tools (`make`), and Boost headers. On macOS, this can be installed with Homebrew; Linux CI should provide the equivalent development package.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
./build/debug/eventedge
```

The server defaults to listening on `127.0.0.1:8080`, proxying to one upstream at `127.0.0.1:9000`, and using `std::thread::hardware_concurrency()` workers (minimum one). To configure multiple upstreams, supply the listen address, listen port, worker count, and one or more `host:port` endpoints:

```sh
./build/debug/eventedge 127.0.0.1 8080 4 \
  127.0.0.1:9001 127.0.0.1:9002 127.0.0.1:9003
curl -i http://127.0.0.1:8080/health
```

Backends begin provisionally healthy so EventEdge can serve traffic immediately. A periodic TCP resolve/connect probe (2-second interval, 1-second connect timeout) then updates eligibility. Each non-health request selects the next healthy upstream. If none are healthy, EventEdge returns `503 Service Unavailable`. A selected backend that fails normally returns `502 Bad Gateway`; one that exceeds the proxy deadline returns `504 Gateway Timeout`. Default proxy budgets are 2 seconds for connect, 2 seconds for write, and 5 seconds for response read. No failed request is retried. Each proxied request opens a new upstream HTTP connection. Connection pooling and streaming request/response bodies are intentionally deferred. The worker runtime has not been benchmarked; no performance improvement is claimed.

The cache considers only unauthenticated proxied `GET` requests. It stores only `200 OK` responses that lack `Set-Cookie` and `Cache-Control: no-store` or `private`. Cache hits preserve the current client connection semantics and occur before upstream selection. For a simultaneous cache miss on the same key, one request fetches from upstream while matching requests wait and receive the same final result. This coalescing is process-local, as is the cache: there is no Redis or distributed coordination. Current waiters can share a 502, 503, or 504 result, but failures are not cached. There are still no retries, circuit breakers, connection pooling, or measured performance claims.

`GET /metrics` is a local Prometheus text endpoint. It reports request, cache, coalescing, gateway-failure, upstream-selection/health, in-flight proxy, and fixed-bucket request-duration metrics. Metrics count cacheable requests that observe a miss (including coalescing waiters); failure counters represent client responses. Metrics are process-local instrumentation only: no Prometheus/Grafana deployment or benchmark claim is included.

For an optimized build:

```sh
cmake --preset release
cmake --build --preset release
```
