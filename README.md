# EventEdge

EventEdge is a high-performance C++20 reverse proxy, load balancer, and cache designed for bursty live-event traffic.

## Status

EventEdge uses asynchronous Boost.Asio/Beast networking with one `io_context` run by a configurable multithreaded runtime. Per-session strand serialization protects a local `GET /health` endpoint and reverse proxying across multiple static upstream backends with thread-safe round-robin selection. An unavailable selected upstream receives a basic `502 Bad Gateway` response.

Active health checks, unhealthy-backend avoidance, retries, connection pooling, advanced timeout policy, caching, request coalescing, metrics, and measured performance claims are not implemented yet.

## Planned technical direction

Future milestones will add health-aware balancing, cache-stampede protection, metrics, and Linux-focused validation.

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

Each non-health request atomically selects the next configured upstream. A selected failed backend returns `502`; EventEdge does not skip it, retry, or mark it unhealthy. Each proxied request opens a new upstream HTTP connection. Connection pooling and streaming request/response bodies are intentionally deferred. The worker runtime has not been benchmarked; no performance improvement is claimed.

For an optimized build:

```sh
cmake --preset release
cmake --build --preset release
```
