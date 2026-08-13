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

## Current milestone: EVE-005

The C++20 foundation now includes an asynchronous HTTP/1.1 server with a local `GET /health` response and round-robin reverse proxying across static configured upstreams. One `io_context` runs on configurable worker threads while per-session strands serialize connection state; a shared atomic selector advances backend selection. It opens a new upstream connection for each proxied request and returns `502 Bad Gateway` if the selected upstream cannot be reached. Health checks, unhealthy-backend avoidance, retries, caching, and metrics are not implemented.
