# EventEdge

EventEdge is a high-performance C++20 reverse proxy, load balancer, and cache designed for bursty live-event traffic.

## Status

EventEdge now has a C++20 foundation and a single-threaded asynchronous HTTP/1.1 server built with Boost.Asio and Boost.Beast. It serves a local `GET /health` endpoint.

Reverse proxying, upstream forwarding, load balancing, caching, upstream health checking, metrics, and production performance claims are not implemented yet.

## Planned technical direction

Future milestones will add reverse proxying, upstream load balancing and health checks, cache-stampede protection, metrics, and Linux-focused validation.

## Build and test

Prerequisites: CMake, a C++20 compiler, standard Unix build tools (`make`), and Boost headers. On macOS, this can be installed with Homebrew; Linux CI should provide the equivalent development package.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
./build/debug/eventedge
```

The server listens on `127.0.0.1:8080` by default. An address and port can be supplied explicitly:

```sh
./build/debug/eventedge 127.0.0.1 8080
curl -i http://127.0.0.1:8080/health
```

For an optimized build:

```sh
cmake --preset release
cmake --build --preset release
```
