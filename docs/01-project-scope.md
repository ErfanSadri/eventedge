# EventEdge project scope

Live events can cause abrupt traffic spikes that overwhelm a single application backend. EventEdge is intended to sit at the request edge and direct traffic to healthy upstream services.

## Planned request path

```text
Client -> EventEdge -> healthy upstream backend
```

## Intended responsibilities

- Asynchronous proxying
- Upstream load balancing and health checks
- Response caching and request coalescing
- Operational metrics

## Non-goals for the initial system

EventEdge will not initially include a database, Redis, Kafka, Kubernetes integration, a frontend, TLS, or HTTP/2.

## Current milestone: EVE-011

The C++20 foundation includes asynchronous HTTP/1.1 proxying, local health and metrics routes, health-aware round-robin balancing, caching, and process-local coalescing. Separate ASan and TSan CMake presets support memory and concurrency validation. Retries, passive health scoring, distributed coordination, and external monitoring deployment are not implemented.
