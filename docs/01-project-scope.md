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

## Current milestone: EVE-010

The C++20 foundation now includes an asynchronous HTTP/1.1 server with local `GET /health` and Prometheus-compatible `GET /metrics` responses, health-aware round-robin proxying, a shared LRU + TTL response cache, and process-local request coalescing. Metrics expose request, cache, coalescing, failure, upstream, health, in-flight, and duration observations. Retries, passive health scoring, distributed coordination, and external monitoring deployment are not implemented.
