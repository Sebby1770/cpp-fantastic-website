# AsterForge Observatory 3.1

AsterForge is a live mission-control observatory served by a native C++17 POSIX HTTP server. Zero third-party C++ libraries. The backend forges seeded mission packets, sky layers, orbital systems, and telemetry; the browser renders a flyable WebGL sky.

## Highlights

- Header-split C++17 server: thread pool, per-IP token bucket, keep-alive, Range/ETag static files, SSE hub
- Generative JSON: `/api/mission` nodes include `x,y,z` plus energy, phase, and kind
- Six modes: orbit, bloom, forge, night, pulse, drift
- WebGL observatory (Three.js r160 CDN) with slow auto-orbit and canvas 3D fallback
- SSE telemetry strip when the C++ server is present; GitHub Pages uses in-browser generators
- Shareable URL state for `seed`, `mode`, `intensity`, `tempo`, and `density`

## Run locally

```bash
cmake -S . -B build
cmake --build build
./build/cpp_fantastic_website --port 8080
```

Open `http://localhost:8080`.

### CLI flags

| Flag | Default | Notes |
| --- | --- | --- |
| `--port` | `8080` | Clamped to 1024–65535 |
| `--threads` | hardware concurrency | Clamped to 2–32 |
| `--max-body` | `1048576` | POST body limit in bytes |
| `--rate-limit` | `50` | Per-IP requests/second; `0` disables |
| `--log-format` | `json` | `json` or `text` |
| `--quiet` | off | Suppress access log |
| `--help` |  | Print usage |

## HTTP API

| Method | Path | Notes |
| --- | --- | --- |
| GET/HEAD | `/api/health` | `status`, `service`, `language`, `version` `3.1.0`, `uptime_seconds`, `request_count` |
| GET/HEAD | `/api/version` | Version plus endpoint list |
| GET/HEAD | `/api/presets` | Six named missions |
| GET/HEAD | `/api/mission` | Query: `seed`, `mode`, `intensity`, `tempo`, `density` |
| GET/HEAD | `/api/share` | `shortId` plus share path |
| GET/HEAD | `/api/sky` | Nebula layers (`seed`, `layers`) |
| GET/HEAD | `/api/orbit` | Planets and moons (`seed`, `planets`) |
| GET/HEAD | `/api/constellation` | Named points (`seed`, `points`) |
| GET/HEAD | `/api/catalog` | Palettes and modes |
| GET/HEAD | `/api/metrics` | Totals, `by_path`, latency p50/p99, status classes |
| GET | `/api/stream` | `text/event-stream`, `event: telemetry` |
| POST | `/api/echo` | Echoes the JSON body |
| OPTIONS | `*` | `204` CORS |

Static files use `Cache-Control: public, max-age=300`, ETag / Last-Modified (`304`), and byte ranges (`206` / `416`). APIs are `no-store`. Path traversal is rejected with `400`.

## Frontend

`public/` is a no-build observatory:

- Orbit camera with inertia (Three.js `OrbitControls`)
- Starfield, nebula sprites from `/api/sky`, glowing 3D constellation from mission nodes
- Optional `/api/orbit` overlay, time scale `0.25x / 1x / 4x`
- Click a node for intel; snapshots, copy JSON, copy link, PNG from `renderer.domElement.toDataURL`
- Keys: `?` help, `g` generate, `r` randomize, `1–6` modes
- `prefers-reduced-motion` freezes orbits
- Relative assets (`./styles.css`, `./app.js`) so GitHub Pages works
- If `/api/*` is missing, the UI synthesizes matching JSON locally and hides SSE

## Tests

```bash
cmake -S . -B build && cmake --build build
./build/aster_unit_tests
./tests/smoke.sh
```

`aster_unit_tests` covers decode/escape, query parsing, FNV seed determinism, mission JSON, path containment, integer clamp, and Range parsing. `tests/smoke.sh` builds both binaries, hits the API surface, and checks HEAD, OPTIONS, traversal, echo, 405 Allow, SSE, 304, Range, and 429.

CI (`.github/workflows/ci.yml`) runs the same sequence on Ubuntu.

## Layout

```text
.
|-- CMakeLists.txt
|-- public
|   |-- app.js
|   |-- index.html
|   `-- styles.css
|-- src
|   |-- http.hpp
|   |-- json.hpp
|   |-- log.hpp
|   |-- main.cpp
|   |-- metrics.hpp
|   |-- mission.hpp
|   |-- rate_limiter.hpp
|   |-- server.hpp
|   |-- stream.hpp
|   |-- thread_pool.hpp
|   `-- util.hpp
`-- tests
    |-- smoke.sh
    `-- unit_tests.cpp
```

The CMake target remains `cpp_fantastic_website`.
