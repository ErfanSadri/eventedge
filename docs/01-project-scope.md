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

## Current milestone: EVE-006

The C++20 foundation now includes an asynchronous HTTP/1.1 server with a local `GET /health` response and round-robin reverse proxying across static configured upstreams. One `io_context` runs on configurable worker threads while per-session strands serialize connection state. Periodic asynchronous TCP health checks update shared backend eligibility: unhealthy backends are skipped and recovered backends return automatically. With no healthy backend, application requests receive `503 Service Unavailable`; a selected backend that fails during proxying still yields `502 Bad Gateway`. Retries, passive health scoring, caching, and metrics are not implemented.
