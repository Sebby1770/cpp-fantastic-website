# AsterForge

AsterForge is a visually rich website app served by a native C++17 HTTP server. The C++ backend generates live mission data, palette systems, metrics, routes, rings, notes, and constellation geometry that the browser turns into an animated observatory.

## Highlights

- Native C++ web server with zero runtime framework dependencies
- Responsive command interface with a full-bleed animated canvas workspace
- Live generative JSON endpoint at `/api/mission`
- Preset discovery endpoint at `/api/presets`
- Share metadata endpoint at `/api/share`
- Health endpoint at `/api/health`
- Seeded modes for orbit, bloom, forge, and night compositions
- Shareable URL state for seed, mode, intensity, tempo, and density
- Local mission snapshots saved in the browser
- One-click JSON copy, link copy, and PNG export from the canvas
- HEAD support and safer static file path handling in the C++ server
- No frontend build step or package install required
- CMake build for macOS and Linux

## Run Locally

```bash
cmake -S . -B build
cmake --build build
./build/cpp_fantastic_website --port 8080
```

Then open:

```text
http://localhost:8080
```

Useful endpoints:

- `GET /api/health`
- `GET /api/presets`
- `GET /api/share?seed=sebby&mode=orbit&intensity=68&tempo=54&density=72`
- `GET /api/mission?seed=sebby&mode=orbit&intensity=68&tempo=54&density=72`
- `HEAD /`
- `HEAD /api/mission?seed=sebby&mode=orbit`

## Smoke Test

```bash
./tests/smoke.sh
```

The smoke test builds the server, starts it on port `8097`, checks the health endpoint, preset endpoint, share endpoint, mission API, HEAD handling, path traversal rejection, and confirms the home page is served.

## Project Layout

```text
.
|-- CMakeLists.txt
|-- public
|   |-- app.js
|   |-- index.html
|   `-- styles.css
|-- src
|   `-- main.cpp
`-- tests
    `-- smoke.sh
```
