# EventEdge

EventEdge is a high-performance C++20 reverse proxy, load balancer, and cache designed for bursty live-event traffic.

## Status

Only the EVE-001 project foundation exists today. The executable is a minimal startup placeholder; no networking, proxying, load balancing, caching, concurrency, or metrics functionality has been implemented.

## Planned technical direction

Future milestones will introduce asynchronous HTTP/1.1 proxying with Boost.Asio and Boost.Beast, upstream load balancing and health checks, cache-stampede protection, metrics, and Linux-focused validation.

## Build and test

Prerequisites: CMake, a C++20 compiler, and standard Unix build tools (`make`).

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
./build/debug/eventedge
```

For an optimized build:

```sh
cmake --preset release
cmake --build --preset release
```
