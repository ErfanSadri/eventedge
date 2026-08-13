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

## Current milestone: EVE-007

The C++20 foundation now includes an asynchronous HTTP/1.1 server with a local `GET /health` response and round-robin reverse proxying across static configured upstreams. One `io_context` runs on configurable worker threads while per-session strands serialize connection state. Periodic asynchronous TCP health checks update shared backend eligibility. Normal proxy connect, write, and read stages have explicit deadlines: a selected backend that times out yields `504 Gateway Timeout`, an ordinary selected-backend failure yields `502 Bad Gateway`, and no eligible backend yields `503 Service Unavailable`. Retries, passive health scoring, caching, and metrics are not implemented.
