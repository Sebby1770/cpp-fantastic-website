# C++ Fantastic Website

**AsterForge** is a polished interactive web app served by a native **C++17** HTTP server. The backend generates live mission data, color palettes, constellation geometry, nebula skyboxes, orbital systems, and streaming request telemetry — with zero runtime framework dependencies (POSIX sockets only).

**Version: 2.3.0**

## Highlights

- **Real HTTP engine** — bounded worker thread pool, HTTP/1.1 keep-alive with pipelining carry-over, per-socket read/write timeouts, graceful shutdown on `SIGINT`/`SIGTERM`
- **Hardened parsing** — 64 KiB header cap (`431`), strict `Content-Length` validation, oversized bodies rejected without reading them (`413`), malformed requests → `400`, `405` with `Allow`
- **Static-file caching** — strong FNV-1a `ETag` + `Last-Modified` + `Cache-Control: public, max-age=300`; conditional `If-None-Match` / `If-Modified-Since` → `304` (APIs stay `no-store`)
- **Range requests** — `Accept-Ranges: bytes` plus single-range `Range` support (`start-end`, `start-`, `-suffix`) → `206 Partial Content` with `Content-Range`, or `416` when unsatisfiable
- **Transparent gzip** — serves a pre-built `<file>.gz` sidecar when the client sends `Accept-Encoding: gzip` (honoring `q=0`), with a distinct representation `ETag` and `Vary: Accept-Encoding`; zero runtime CPU, still dependency-free
- **Traversal defense** — canonical-path containment (symlink escapes and NUL bytes rejected)
- **Per-IP rate limiting** — token bucket (`--rate-limit`), `429` + `Retry-After`
- **Live telemetry** — `GET /api/stream` Server-Sent Events pushing metrics snapshots every second; deep metrics with status-class counters and latency `mean`/`max`/`p50`/`p99`
- **Structured access log** — JSON by default (`--log-format json|text`), one object per request with time, IP, method, path, status, bytes and sub-millisecond latency; mutex-serialized and flushed per line so `tail -f` works on a live server
- **Frontend workspace** — living orrery (orbiting planets, nebula skybox, aurora, dust) plus constellation, warp, SSE telemetry, keyboard shortcuts (`o` / `w` / `?`), `prefers-reduced-motion` support, mobile layout
- **GitHub Pages demo** — `public/` is published statically; in-browser generators keep sky / orbit / constellation interactive when the C++ APIs are absent
- **Quality gates** — 100+ unit tests, end-to-end smoke suite, CI matrix (g++/clang++) plus an ASan+UBSan job

## Run Locally

```bash
cmake -S . -B build
cmake --build build
./build/cpp_fantastic_website --port 8080
```

Optional flags:

| Flag | Description |
|------|-------------|
| `--port N` | Listen port (default `8080`, clamped 1024–65535) |
| `--threads N` | Worker pool size (default: hardware concurrency, clamped 2–32) |
| `--max-body BYTES` | Max request body size (default 1 MiB; larger → `413`) |
| `--rate-limit N` | Sustained requests/sec per IP (default `50`, `0` disables) |
| `--log-format json\|text` | Access-log format (default `json`) |
| `--quiet` / `-q` | Disable per-request access logging |
| `--help` / `-h` | Show usage |

Then open:

```text
http://localhost:8080
```

## HTTP API

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/api/health` | Liveness: `status`, `version`, `uptime_seconds`, `request_count` |
| `GET` | `/api/version` | Service metadata + endpoint list |
| `GET` | `/api/time` | ISO-8601 UTC + unix timestamp |
| `GET` | `/api/random` | Deterministic seeded random int (`seed`, `min`, `max`) |
| `GET` | `/api/status` | Richer health snapshot (`public_dir`, `port`, `uptime`, `request_count`) |
| `GET` | `/api/mission` | Mission packet (seed, mode, intensity, tempo, optional `palette`) |
| `GET` | `/api/palettes` | Named color palettes |
| `GET` | `/api/constellation` | Star points for the canvas (`seed`, `points`) |
| `GET` | `/api/sky` | Deterministic nebula / skybox (`seed` string, `layers` 2–8) |
| `GET` | `/api/orbit` | Deterministic miniature solar system (`seed` int, `planets` 3–10) |
| `GET` | `/api/metrics` | `total_requests`, `by_path`, `uptime_seconds`, status classes (`2xx`–`5xx`), `latency_ms` (`count`/`mean`/`max`/`p50`/`p99`) |
| `GET` | `/api/stream` | Server-Sent Events telemetry (`event: telemetry` every 1 s; max 32 concurrent streams, over cap → `503`) |
| `POST` | `/api/echo` | Echo JSON body back (demo / Content-Length parsing) |
| `HEAD` | any GET route | Headers only (body length advertised, no body sent) |
| `OPTIONS` | any | CORS preflight (`204`) |

Status behaviors: `206` + `Content-Range` for satisfiable `Range` requests, `304` conditional static hits, `400` malformed, `404` unknown path, `405` + `Allow` for unsupported methods, `408` request timeout, `413` oversized body, `416` unsatisfiable range, `429` + `Retry-After` when rate-limited, `431` oversized headers, `503` over the SSE stream cap.

### Mission query parameters

| Param | Default | Notes |
|-------|---------|-------|
| `seed` | `sebby` | String seed for deterministic generation |
| `mode` | `pulse` | `pulse` / `route` / `forge` |
| `intensity` | `68` | 1–100 |
| `tempo` | `42` | 1–100 |
| `palette` | _(mode-based)_ | Palette id from `/api/palettes` (e.g. `ember`) |

### Constellation query parameters

| Param | Default | Notes |
|-------|---------|-------|
| `seed` | `42` | Integer seed |
| `points` | `24` | 4–128 star count |

### Sky query parameters

| Param | Default | Notes |
|-------|---------|-------|
| `seed` | `sebby` | String seed (`stable_seed` → `mt19937`) |
| `layers` | `4` | 2–8 nebula layers |

### Orbit query parameters

| Param | Default | Notes |
|-------|---------|-------|
| `seed` | `42` | Integer seed |
| `planets` | `6` | 3–10 planet count |

### Example requests

```bash
curl -s http://localhost:8080/api/health
# {"status":"ok","service":"AsterForge","language":"C++17","version":"2.3.0",...}

curl -s "http://localhost:8080/api/constellation?seed=7&points=12"
curl -s "http://localhost:8080/api/sky?seed=orion&layers=5"
curl -s "http://localhost:8080/api/orbit?seed=42&planets=6"
curl -s -X POST http://localhost:8080/api/echo -H 'Content-Type: application/json' -d '{"ping":1}'

# Live telemetry stream (SSE)
curl -N http://localhost:8080/api/stream

# Conditional GET → 304
ETAG=$(curl -sI http://localhost:8080/ | tr -d '\r' | awk 'tolower($1)=="etag:" {print $2}')
curl -s -o /dev/null -w '%{http_code}\n' -H "If-None-Match: $ETAG" http://localhost:8080/
```

All responses include:

- `X-Content-Type-Options: nosniff`
- `Access-Control-Allow-Origin: *` (local-dev friendly)

## Frontend

The orrery workspace in `public/` consumes the SSE stream for a live telemetry dashboard (request totals, status classes, latency percentiles) and renders a nebula skybox, dust, an aurora ribbon, a glowing star with revolving planets and moons, constellation links in the background, shooting stars, and warp mode. Keyboard shortcuts are listed in-app (`?`); warp toggles with `w`, the orbital system with `o`. Ambient animation honors `prefers-reduced-motion` (orbital angles freeze).

When `/api/sky`, `/api/orbit`, `/api/constellation`, or `/api/mission` are missing (GitHub Pages, offline), matching in-browser generators produce the same JSON shapes so the demo stays interactive.

## Tests

```bash
# Full smoke suite (build + unit + live HTTP checks)
./tests/smoke.sh

# Unit tests only
cmake -S . -B build && cmake --build build && ./build/aster_unit_tests

# ctest wrapper
ctest --test-dir build --output-on-failure
```

The smoke suite builds the server, starts it on port `8097` (override with `PORT=`), and exercises: health/mission/palettes/constellation/sky/orbit/echo APIs, HEAD/OPTIONS, security headers, keep-alive reuse, oversized-body `413`, header-cap `431`, 50-way concurrency, ETag/`304` conditional GETs, `Range` requests (`206`/`Content-Range`/`416`), gzip sidecar negotiation, path-traversal probes, per-IP `429` rate limiting, `405` `Allow`, echo JSON validity, SSE streaming, access-log format (JSON parsed and field-checked, plus `--log-format text`), and graceful shutdown (including with an open SSE stream) with exit code `0`.

CI runs the full suite under g++ and clang++, plus a dedicated ASan+UBSan job.

## Project Layout

```text
.
|-- CMakeLists.txt
|-- public/                 # Static frontend
|   |-- app.js
|   |-- index.html
|   `-- styles.css
|-- src/
|   |-- main.cpp            # Thin entry + signals
|   |-- http.hpp            # Request / Response / parse / send / limits
|   |-- log.hpp             # Mutex-serialized access log (JSON / text)
|   |-- metrics.hpp         # Thread-safe counters + latency ring
|   |-- mission.hpp         # Mission, palettes, constellation, sky, orbit JSON
|   |-- rate_limiter.hpp    # Per-IP token bucket
|   |-- server.hpp          # Server class, routing, static cache, CLI
|   |-- stream.hpp          # SSE hub + telemetry stream threads
|   |-- thread_pool.hpp     # Bounded worker pool
|   `-- util.hpp            # url_decode, json_escape, http_date, ETag hash
|-- tests/
|   |-- smoke.sh
|   `-- unit_tests.cpp
`-- .github/workflows/
    |-- ci.yml
    `-- pages.yml           # Publishes public/ to GitHub Pages
```

## GitHub Pages

A static copy of `public/` is published by `.github/workflows/pages.yml`
(`peaceiris/actions-gh-pages@v4`, `publish_dir: ./public`) on pushes to `main`.
Asset paths in `index.html` are relative (`./styles.css`, `./app.js`) so they
work both at the C++ server root and at a project Pages URL such as
`/cpp-fantastic-website/`. Without the native binary the UI falls back to
in-browser generators for sky, orbit, constellation, palettes, and mission.

## Architecture

```text
            ┌─ SIGINT/SIGTERM ──► running=false ─ drain pool ─ wait streams ─┐
            │                                                                ▼
Client ──► accept (poll, shutdown-aware) ──► ThreadPool worker               exit 0
              │                                   │
              │                     keep-alive loop (timeouts, pipelining)
              │                                   │
              │                        ┌── RateLimiter (per-IP bucket)
              │                        ├── route → APIs | static (ETag/304)
              │                        ├── GET /api/stream ──► detached SSE thread
              │                        ├── Metrics::record (status class + latency)
              │                        └── send_response + access log
```

Version: **2.3.0**
