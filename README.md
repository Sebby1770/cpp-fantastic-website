# C++ Fantastic Website

AsterForge is a polished interactive web app served by a native C++17 HTTP server. The C++ backend generates live mission data, palette choices, metrics, and constellation geometry for the browser workspace.

## Highlights

- Native C++ web server with zero runtime framework dependencies
- Responsive app interface with an animated canvas workspace
- Live JSON endpoint at `/api/mission`
- Health endpoint at `/api/health`
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

## Smoke Test

```bash
./tests/smoke.sh
```

The smoke test builds the server, starts it on port `8097`, checks the health endpoint, checks the mission API, and confirms the home page is served.

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
