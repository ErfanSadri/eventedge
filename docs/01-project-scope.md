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

## EVE-001 boundary

This milestone establishes only the C++20 build, executable, tests, and project documentation. It contains no networking implementation.
