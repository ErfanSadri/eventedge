# EventEdge

EventEdge is a high-performance C++20 reverse proxy, load balancer, and cache designed for bursty live-event traffic.

## Status

EventEdge now has a C++20 foundation, a single-threaded asynchronous Boost.Asio/Beast HTTP/1.1 server, a local `GET /health` endpoint, and asynchronous reverse proxying to one configured HTTP upstream. Unavailable upstreams receive a basic `502 Bad Gateway` response.

Multiple upstream backends, load balancing, active upstream health checks, retries, configurable timeout policy, caching, request coalescing, metrics, and measured performance claims are not implemented yet.

## Planned technical direction

Future milestones will add multiple upstream support, load balancing and health checks, cache-stampede protection, metrics, and Linux-focused validation.

## Build and test

Prerequisites: CMake, a C++20 compiler, standard Unix build tools (`make`), and Boost headers. On macOS, this can be installed with Homebrew; Linux CI should provide the equivalent development package.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
./build/debug/eventedge
```

The server defaults to listening on `127.0.0.1:8080` and proxying to `127.0.0.1:9000`. Address, port, upstream host, and upstream port can be supplied explicitly:

```sh
./build/debug/eventedge 127.0.0.1 8080 127.0.0.1 9000
curl -i http://127.0.0.1:8080/health
```

Each proxied request opens a new upstream HTTP connection. Connection pooling and streaming request/response bodies are intentionally deferred.

For an optimized build:

```sh
cmake --preset release
cmake --build --preset release
```
