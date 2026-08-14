# EVE-013 local benchmark report

These are local development-machine measurements from `benchmark-results/20260813-204550`, not production benchmarks or general capacity claims. Client, EventEdge, and Docker fixtures shared one host.

## Environment

| Component | Value |
| --- | --- |
| Build | Release, 4 EventEdge workers |
| Host | macOS 26.5.2; Intel Core i7-9750H @ 2.60 GHz; 12 logical CPUs; 16 GiB RAM |
| Toolchain | Apple clang 17.0.0; CMake 4.4.2 |
| Fixtures | Docker 29.6.2; Docker Compose v5.3.1; `python:3.13-slim` |
| Load generator | ApacheBench 2.3 |

## Methodology

The harness starts the Compose fixtures and manages the Release EventEdge process. The comparable direct, one-upstream, and three-upstream baselines each use 10,000 requests at c16. Backends restart before each measured trial, readiness is probed through `/identity`, EventEdge phases also wait for healthy-upstream gauges, and a discarded 256-request c8 warm-up follows.

The separate three-upstream uncached concurrency sweep uses 3,000 requests at c1/c16/c64/c128. Cache, coalescing, and sustained workloads retain their dedicated methods. A result is invalid only for non-2xx, Connect, Receive, or Exceptions failures; ApacheBench Length mismatches alone do not invalidate it. Invalid trials are retained and not retried, and no median includes one.

## Direct backend baseline

10,000 requests at c16 against one Python fixture completed successfully in all trials.

| Trial | Requests/sec | Failures |
| --- | ---: | ---: |
| 1 | 627.01 | 0 |
| 2 | 622.77 | 0 |
| 3 | 601.47 | 0 |
| **Median** | **622.77** | **0** |

Typical p50 was 12 ms. The lightweight Python fixture showed occasional long-tail pauses, so this report does not draw a tail-latency conclusion from it.

## Single-upstream EventEdge caveat

The equivalent one-upstream proxy scenario was unstable with the local Python fixture under repeated connection load: trial 1 reached 558.33 req/s with zero failures, while trials 2 and 3 had 4,197 and 2,290 non-2xx responses. The EventEdge log recorded upstream read `end of stream` errors during those invalid trials.

Only one of three trials was valid, so no steady-state median or proxy-overhead claim is reported. The invalid trials were retained rather than retried or hidden.

## Three-upstream EventEdge baseline

With authorization bypassing cache, all 10,000-request c16 trials succeeded.

| Trial | Requests/sec | Failures |
| --- | ---: | ---: |
| 1 | 721.55 | 0 |
| 2 | 801.78 | 0 |
| 3 | 832.77 | 0 |
| **Median** | **801.78** | **0** |

This is approximately 28.7% above the direct single-fixture median (622.77 req/s) under the same request count and c16. It demonstrates parallel capacity from distributing work across three fixtures; it does not mean EventEdge makes an individual backend faster. Aggregate selections were exactly even at 10,256 each for backends A, B, and C, including warm-up traffic.

## Concurrency sweep

Three-upstream uncached topology, 3,000 requests per point:

| Concurrency | Requests/sec | p50 | Failures |
| ---: | ---: | ---: | ---: |
| 1 | 312.35 | 3 ms | 0 |
| 16 | 805.00 | 19 ms | 0 |
| 64 | 890.59 | 71 ms | 0 |
| 128 | 709.74 | 141 ms | 0 |

Local throughput peaked at c64. Raising concurrency from 64 to 128 reduced throughput about 20.3% while roughly doubling p50 latency. This is the local setup's observed sweet spot, not a universal EventEdge capacity limit.

## Cache workload

The cache-heavy three-upstream workload used 10,000 requests at c64 for each trial and had zero failures.

| Trial | Requests/sec |
| --- | ---: |
| 1 | 1506.85 |
| 2 | 1735.68 |
| 3 | 1724.67 |
| **Median** | **1724.67** |

Phase-level metrics included 768 discarded warm-up requests: 30,768 cacheable requests, 30,712 hits, 56 misses, 13 coalescing leaders, 43 waiters, and 13 actual proxy requests. That is a 99.82% phase-level cache-hit rate and 99.96% phase-level upstream-request reduction. Those percentages must not be read as applying solely to the 30,000 measured ApacheBench requests.

## Request coalescing

Parallel `curl` cold-cache bursts passed at 16, 32, and 64 clients. Each burst had one leader and one upstream request; waiters were 15, 31, and 63 respectively; cache misses equaled the client count; and every client received HTTP 200.

In particular, EventEdge collapsed 64 simultaneous cold-cache requests into a single upstream fetch. This demonstrates stampede protection and upstream-request amplification reduction, not a 64x throughput improvement.

## Sustained load

The three-upstream uncached c64 run lasted 30 seconds:

| Measure | Result |
| --- | ---: |
| Completed requests | 26,173 |
| Failures | 0 |
| Requests/sec | 871.43 |
| p50 / p90 / p95 / p99 | 72 / 87 / 91 / 102 ms |
| Maximum | 1126 ms |
| RSS before / after | 3036 / 3036 KB |

Settled metrics had no new bad-gateway, service-unavailable, or gateway-timeout responses, `eventedge_proxy_in_flight` returned to zero, and upstream selections remained even. The unchanged RSS snapshot is observational only, not a formal leak test.

## Limitations and interpretation

The final suite contained 18 valid and 2 invalid trials; both invalid trials were in the single-upstream EventEdge scenario. EventEdge's intentional five-second upstream response timeout remained enabled. The Python `ThreadingHTTPServer` fixture is deliberately lightweight and can itself become the limiting component at high concurrency, so timeout behavior must not be attributed simply to EventEdge capacity. HTTP/1.1, no TLS, new upstream connections per request, host contention, and ApacheBench's model further limit interpretation.
