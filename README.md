# C++ Fantastic Website

**AsterForge** is a polished interactive web app served by a native **C++17** HTTP server. The backend generates live mission data, color palettes, constellation geometry, and request metrics — with zero runtime framework dependencies (POSIX sockets only).

## Highlights

- Modular C++17 server (`src/http`, `mission`, `metrics`, `server`)
- Static file hosting + JSON APIs
- Thread-safe request metrics
- CORS + security headers for local development
- HEAD / OPTIONS support
- Responsive constellation workspace in `public/`
- CMake build for macOS and Linux
- Smoke tests + unit tests + GitHub Actions CI

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
| `--quiet` / `-q` | Disable per-request logging |
| `--help` | Show usage |

Then open:

```text
http://localhost:8080
```

## HTTP API

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/api/health` |
| `GET` | `/api/version` | Version + endpoint list |
| `GET` | `/api/time` | Server UTC time |
| `GET` | `/api/random` | Seeded random int (`seed`,`min`,`max`) |
| `GET` | `/api/status` | Richer health snapshot | Liveness: `status`, `version`, `uptime_seconds`, `request_count` |
| `GET` | `/api/mission` | Mission packet (seed, mode, intensity, tempo, optional `palette`) |
| `GET` | `/api/palettes` | Named color palettes |
| `GET` | `/api/constellation` | Star points for the canvas (`seed`, `points`) |
| `GET` | `/api/metrics` | Counters: `total_requests`, `by_path`, `uptime_seconds` |
| `POST` | `/api/echo` | Echo JSON body back (demo / Content-Length parsing) |
| `HEAD` | any GET route | Headers only (no body) |
| `OPTIONS` | any | CORS preflight (`204`) |

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

### Example responses

```bash
curl -s http://localhost:8080/api/health
# {"status":"ok","service":"AsterForge","language":"C++17","version":"1.2.0",...}

curl -s "http://localhost:8080/api/constellation?seed=7&points=12"
curl -s -X POST http://localhost:8080/api/echo -H 'Content-Type: application/json' -d '{"ping":1}'
```

All responses include:

- `X-Content-Type-Options: nosniff`
- `Access-Control-Allow-Origin: *` (local-dev friendly)

## Tests

```bash
# Full smoke suite (build + unit + live HTTP checks)
./tests/smoke.sh

# Unit tests only
cmake -S . -B build && cmake --build build && ./build/aster_unit_tests
```

The smoke test builds the server, starts it on port `8097` (override with `PORT=`), and exercises health, mission, palettes, constellation, metrics, echo, HEAD, OPTIONS, and security headers.

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
|   |-- http.hpp            # Request / Response / parse / send
|   |-- mission.hpp         # Mission, palettes, constellation JSON
|   |-- metrics.hpp         # Thread-safe counters
|   |-- server.hpp          # Server class + routing
|   `-- util.hpp            # url_decode, json_escape, helpers
|-- tests/
|   |-- smoke.sh
|   `-- unit_tests.cpp
`-- .github/workflows/ci.yml
```

## Architecture

```text
Client ──► Server::handle_client
              │
              ├─ read_http_request (headers + Content-Length body)
              ├─ route → APIs | static files
              ├─ Metrics::record (atomic + mutex map)
              └─ send_response (CORS, nosniff, HEAD-safe)
```

Version: **1.2.0**
