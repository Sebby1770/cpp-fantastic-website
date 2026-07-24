# Changelog

## [1.2.0] — 2026-07-24

### Added
- `GET /api/version` — service metadata and endpoint list
- `GET /api/time` — ISO-8601 UTC and unix timestamp
- `GET /api/random` — deterministic seeded integer (`seed`, `min`, `max`)
- `GET /api/status` — richer health (public_dir, port, uptime, requests)
- JSON 404 body for unknown `/api/*` paths
- Structured JSON request logs with elapsed milliseconds

### Changed
- Version **1.2.0**
- Expanded unit tests for query parsing, seeds, and timestamps

## [1.1.0]

Modular server, mission APIs, constellation workspace, CI.
