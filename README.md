# C++ Fantastic Website

**AsterForge** is a polished interactive web app served by a native **C++17** HTTP server. The backend generates live mission data, color palettes, constellation geometry, and streaming request telemetry — with zero runtime framework dependencies (POSIX sockets only).

**Version: 2.0.0**

## Highlights

- **Real HTTP engine** — bounded worker thread pool, HTTP/1.1 keep-alive with pipelining carry-over, per-socket read/write timeouts, graceful shutdown on `SIGINT`/`SIGTERM`
- **Hardened parsing** — 64 KiB header cap (`431`), strict `Content-Length` validation, oversized bodies rejected without reading them (`413`), malformed requests → `400`, `405` with `Allow`
- **Static-file caching** — strong FNV-1a `ETag` + `Last-Modified` + `Cache-Control: public, max-age=300`; conditional `If-None-Match` / `If-Modified-Since` → `304` (APIs stay `no-store`)
- **Traversal defense** — canonical-path containment (symlink escapes and NUL bytes rejected)
- **Per-IP rate limiting** — token bucket (`--rate-limit`), `429` + `Retry-After`
- **Live telemetry** — `GET /api/stream` Server-Sent Events pushing metrics snapshots every second; deep metrics with status-class counters and latency `mean`/`max`/`p50`/`p99`
- **Structured access log** — per-request line with status and latency, mutex-serialized
- **Frontend workspace** — live telemetry dashboard fed by SSE, nebula layers, shooting stars, warp mode, keyboard shortcuts, `prefers-reduced-motion` support, mobile layout
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
| `GET` | `/api/mission` | Mission packet (seed, mode, intensity, tempo, optional `palette`) |
| `GET` | `/api/palettes` | Named color palettes |
| `GET` | `/api/constellation` | Star points for the canvas (`seed`, `points`) |
| `GET` | `/api/metrics` | `total_requests`, `by_path`, `uptime_seconds`, status classes (`2xx`–`5xx`), `latency_ms` (`count`/`mean`/`max`/`p50`/`p99`) |
| `GET` | `/api/stream` | Server-Sent Events telemetry (`event: telemetry` every 1 s; max 32 concurrent streams, over cap → `503`) |
| `POST` | `/api/echo` | Echo JSON body back (demo / Content-Length parsing) |
| `HEAD` | any GET route | Headers only (body length advertised, no body sent) |
| `OPTIONS` | any | CORS preflight (`204`) |

Status behaviors: `304` conditional static hits, `400` malformed, `404` unknown path, `405` + `Allow` for unsupported methods, `408` request timeout, `413` oversized body, `429` + `Retry-After` when rate-limited, `431` oversized headers, `503` over the SSE stream cap.

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

### Example requests

```bash
curl -s http://localhost:8080/api/health
# {"status":"ok","service":"AsterForge","language":"C++17","version":"2.0.0",...}

curl -s "http://localhost:8080/api/constellation?seed=7&points=12"
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

The constellation workspace in `public/` consumes the SSE stream for a live telemetry dashboard (request totals, status classes, latency percentiles) and renders nebula layers, shooting stars, and a warp mode on canvas. Keyboard shortcuts are listed in-app (`?`), warp toggles with `w`, and all ambient animation honors `prefers-reduced-motion`.

## Tests

```bash
# Full smoke suite (build + unit + live HTTP checks)
./tests/smoke.sh

# Unit tests only
cmake -S . -B build && cmake --build build && ./build/aster_unit_tests

# ctest wrapper
ctest --test-dir build --output-on-failure
```

The smoke suite builds the server, starts it on port `8097` (override with `PORT=`), and exercises: health/mission/palettes/constellation/echo APIs, HEAD/OPTIONS, security headers, keep-alive reuse, oversized-body `413`, header-cap `431`, 50-way concurrency, ETag/`304` conditional GETs, path-traversal probes, per-IP `429` rate limiting, `405` `Allow`, echo JSON validity, SSE streaming, access-log format, and graceful shutdown (including with an open SSE stream) with exit code `0`.

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
|   |-- log.hpp             # Mutex-serialized access log
|   |-- metrics.hpp         # Thread-safe counters + latency ring
|   |-- mission.hpp         # Mission, palettes, constellation JSON
|   |-- rate_limiter.hpp    # Per-IP token bucket
|   |-- server.hpp          # Server class, routing, static cache, CLI
|   |-- stream.hpp          # SSE hub + telemetry stream threads
|   |-- thread_pool.hpp     # Bounded worker pool
|   `-- util.hpp            # url_decode, json_escape, http_date, ETag hash
|-- tests/
|   |-- smoke.sh
|   `-- unit_tests.cpp
`-- .github/workflows/ci.yml
```

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

Version: **2.0.0**
