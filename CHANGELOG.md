# Changelog

All notable changes to this project are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [2.0.0] - 2026-07-19

### Added

- Bounded worker **thread pool** (`--threads`, default hardware concurrency clamped 2–32) replacing thread-per-connection handling
- **HTTP/1.1 keep-alive** with pipelined-request carry-over and a per-connection request cap
- Per-socket **read/write timeouts** (`408` on request timeout)
- **Graceful shutdown** on `SIGINT`/`SIGTERM`: poll-based accept loop notices within 250 ms, drains workers and open SSE streams, exits `0`
- Hardened request parsing: 64 KiB header cap (`431`), strict `Content-Length` validation, oversized bodies rejected without reading (`413`, `--max-body`), `405` responses carry `Allow`
- **Static-file caching**: strong FNV-1a `ETag`, `Last-Modified`, `Cache-Control: public, max-age=300`, conditional `If-None-Match`/`If-Modified-Since` → `304`; API responses marked `no-store`
- Canonical-path static serving with symlink-escape containment and NUL rejection
- Per-IP **token-bucket rate limiting** (`--rate-limit`, default 50 req/s, `0` disables) returning `429` + `Retry-After`
- **`GET /api/stream`**: Server-Sent Events telemetry (1 s cadence, 32-stream cap → `503` over cap) served on detached threads
- Deep metrics: status-class counters (`2xx`–`5xx`) and latency `count`/`mean`/`max`/`p50`/`p99` from a 1024-entry ring buffer
- Structured, mutex-serialized **access log** with per-request latency (disable with `--quiet`)
- Frontend: live SSE-fed telemetry dashboard, nebula layers, shooting stars, warp mode (`w`), keyboard-shortcut overlay (`?`), `prefers-reduced-motion` support, mobile layout
- Tests: unit suite expanded to 100+ assertions (parsers, params, MIME, seeds, pool, rate limiter, ETag, containment, SSE); smoke suite extended with keep-alive, `413`, concurrency, caching, traversal, rate-limit, protocol-edge, SSE, and shutdown sections
- CI: g++/clang++ build matrix, `ctest` step, and a dedicated ASan+UBSan job running unit + smoke suites
- This changelog

### Changed

- `Server` constructor now takes a `CliOptions` struct instead of positional arguments
- Version is defined once (`aster::kVersion` in `src/util.hpp`) and reported consistently by `/api/health`, `/api/mission`, `--help`, and the startup banner
- README rewritten around the 2.0.0 feature set

### Fixed

- Graceful shutdown previously never triggered while the accept loop was idle
- Request bodies larger than the limit were silently truncated instead of rejected
- Concurrent request log lines could interleave
- Static file resolution could follow symlinks out of the public root
- `/api/mission` hardcoded version `1.1.0`

## [1.1.0] - 2026-07-10

### Added

- Modular server split (`http`, `mission`, `metrics`, `server`, `util` headers)
- `/api/palettes`, `/api/constellation`, `/api/metrics`, `POST /api/echo`
- HEAD/OPTIONS support, CORS + `nosniff` headers, thread-safe metrics
- Constellation workspace frontend, unit tests, smoke script, GitHub Actions CI

## [1.0.0] - 2026-06-16

### Added

- Initial release: C++17 POSIX-socket HTTP server serving a static site with mission/health JSON APIs
