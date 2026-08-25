# Changelog

All notable changes to this project are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [2.4.0] - 2026-08-25

Comets, planet inspect, and orbital time scale on top of the 2.3 orrery.

### Added

- `GET /api/comet` — deterministic comet / shooting-body packet from an integer
  `seed` (default 7) and `count` (1–6, default 2). Each body has `x`/`y` in
  0–1, `dx` −0.4–0.4, `dy` −0.2–0.2, `len` 0.05–0.25, and `hue` 0–360.
  Seeded with `mt19937`. POST is `405`. Listed on `/api/version`.
- Frontend comet streaks from `/api/comet`, with an in-browser generator when
  the API is missing. Keyboard `c` (and a Comet toggle) show or hide them.
- Click a planet to inspect it: name, orbit, period, moons, and ring on a
  small glass card on the stage.
- Orbital time scale buttons (0.25× / 1× / 4×) that multiply orbital speed.
  `prefers-reduced-motion` still freezes orbits.

### Changed

- Version **2.4.0**; `aster::kVersion` and CMake `VERSION` bumped together.
- `/api/version` endpoint list includes `/api/comet`.

## [2.3.0] - 2026-08-25

Orbital skybox: a living orrery on the stage, two new deterministic JSON APIs,
and a static GitHub Pages demo that stays interactive without the C++ binary.

### Added

- `GET /api/sky` — nebula / skybox packet from a string `seed` (default `sebby`)
  and `layers` (2–8, default 4): haze, radial-gradient layers, dust count, and
  an optional aurora ribbon. Seeded with `stable_seed` + `mt19937`.
- `GET /api/orbit` — tiny solar system from an integer `seed` (default 42) and
  `planets` (3–10, default 6): a glowing star plus named planets with orbit,
  period, phase, moons, and rings. Deterministic shuffle of a mythic name list.
- Frontend orrery: nebula layers, dust motes, aurora, revolving planets with
  moons, constellation in the background, Warp / Orbit / Nebula toggles.
  Keyboard `o` toggles the orbital system; `w` and `?` are unchanged.
  `prefers-reduced-motion` freezes orbital angles.
- Client-side sky / orbit / constellation / mission generators so a static
  GitHub Pages copy of `public/` still runs when `/api/*` is missing.
- `.github/workflows/pages.yml` publishes `public/` with
  `peaceiris/actions-gh-pages@v4`.

### Changed

- Version **2.3.0**; `aster::kVersion` and CMake `VERSION` bumped together.
- `/api/version` endpoint list includes `/api/sky` and `/api/orbit`.
- `index.html` uses relative `./styles.css` and `./app.js` so Pages project
  URLs and the C++ static server both resolve assets.

## [2.2.0] - 2026-07-25

Unifies the 2.x engine line with the 1.2 API additions that landed on `main`
in parallel — both feature sets ship together.

### Added

- Carried over from 1.2: `GET /api/version`, `/api/time`, `/api/random`,
  `/api/status`, and a JSON `404` body for unknown `/api/*` paths, now served
  by the 2.x worker-pool dispatch. `/api/version` lists `/api/stream` too.
- `--log-format json|text`. JSON is the default, restoring the structured
  request logging 1.2 introduced, now with a timestamp, client IP and
  sub-millisecond latency, and still mutex-serialized so concurrent workers
  cannot tear a line.

### Fixed

- **Access log was invisible on a running server.** `std::cout` is block
  buffered when redirected to a file, so log lines sat in a 4 KiB buffer until
  it filled or the process exited — `tail -f` on a live server showed nothing.
  Each line is now flushed as it is written.
- The 1.2 log reported latency as truncated integer milliseconds, which showed
  `0` for essentially every request; latency is now sub-millisecond.

### Changed

- Version **2.2.0**; the single `aster::kVersion` constant is reported by
  `/api/health`, `/api/version`, `/api/status`, `/api/mission`, and the banner.

## [2.1.0] - 2026-07-24

### Added

- **HTTP Range requests** for static files: `Accept-Ranges: bytes` on every
  static response, single-range `Range` support (`start-end`, `start-`,
  `-suffix`) → `206 Partial Content` with `Content-Range`, and `416` with
  `Content-Range: bytes */<len>` for unsatisfiable ranges. Multi-range requests
  fall back to a normal `200` (a server may ignore a Range form it does not
  serve).
- **Transparent gzip** content negotiation: when the client sends
  `Accept-Encoding: gzip` (honoring an explicit `q=0`) and a pre-built
  `<file>.gz` sidecar exists, it is served with `Content-Encoding: gzip`, a
  distinct representation `ETag` (`…-gz`), and `Vary: Accept-Encoding`. No
  request-time compression — zero added CPU and still zero runtime dependencies.

### Tests

- Unit coverage for `parse_byte_range` (closed/open/suffix ranges, clamping,
  inverted and out-of-range specs, empty representation) and `accepts_gzip`
  (q-values, `x-gzip`, casing, lists).
- Smoke coverage exercising `206`/`Content-Range`/`416` and gzip sidecar
  negotiation (including byte-identical decode and `q=0` refusal).

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

## [1.2.0] - 2026-07-24

Developed in parallel with the 2.x line; merged in 2.2.0.

### Added
- `GET /api/version` — service metadata and endpoint list
- `GET /api/time` — ISO-8601 UTC and unix timestamp
- `GET /api/random` — deterministic seeded integer (`seed`, `min`, `max`)
- `GET /api/status` — richer health (public_dir, port, uptime, requests)
- JSON 404 body for unknown `/api/*` paths
