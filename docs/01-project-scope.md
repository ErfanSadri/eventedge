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

## Current milestone: EVE-009

The C++20 foundation now includes an asynchronous HTTP/1.1 server with a local `GET /health` response, health-aware round-robin proxying, and a shared mutex-protected LRU + TTL response cache. Eligible proxied `GET` cache misses use process-local request coalescing: one leader fetches from upstream while matching waiters share its final response. Only eligible `200` responses are cached; shared failures are not. Retries, passive health scoring, distributed coordination, and metrics are not implemented.
