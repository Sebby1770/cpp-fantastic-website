#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="${PORT:-8097}"

cmake -S "$ROOT_DIR" -B "$ROOT_DIR/build" >/dev/null
cmake --build "$ROOT_DIR/build" >/dev/null

"$ROOT_DIR/build/cpp_fantastic_website" --port "$PORT" >/tmp/asterforge-smoke.log 2>&1 &
SERVER_PID=$!
trap 'kill "$SERVER_PID" >/dev/null 2>&1 || true' EXIT

for _ in {1..30}; do
  if curl -fsS "http://localhost:$PORT/api/health" 2>/dev/null | grep -q '"status":"ok"'; then
    break
  fi
  sleep 0.2
done

curl -fsS "http://localhost:$PORT/" | grep -q "AsterForge"
curl -fsS "http://localhost:$PORT/api/presets" | grep -q '"presets"'
curl -fsS "http://localhost:$PORT/api/mission?seed=smoke&mode=forge&intensity=73&tempo=51&density=64" | grep -q '"shortId"'
curl -fsS "http://localhost:$PORT/api/share?seed=smoke%20space&mode=bad&intensity=200&tempo=0&density=44" | grep -q '"mode":"orbit"'
curl -fsSI "http://localhost:$PORT/api/mission?seed=head&mode=night" | grep -q "Content-Type: application/json"
curl -fs "http://localhost:$PORT/%2e%2e/CMakeLists.txt" >/tmp/asterforge-traversal.out 2>/dev/null && exit 1 || true

echo "AsterForge smoke test passed on port $PORT"
