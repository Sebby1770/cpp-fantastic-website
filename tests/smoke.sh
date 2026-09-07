#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="${PORT:-8097}"
HOST="127.0.0.1"
BASE="http://$HOST:$PORT"
RL_PORT=$((PORT + 1))
PIDS=()

cleanup() {
  local pid
  for pid in "${PIDS[@]:-}"; do
    kill "$pid" >/dev/null 2>&1 || true
    wait "$pid" >/dev/null 2>&1 || true
  done
}
trap cleanup EXIT

cmake -S "$ROOT_DIR" -B "$ROOT_DIR/build"
cmake --build "$ROOT_DIR/build"

"$ROOT_DIR/build/aster_unit_tests"

"$ROOT_DIR/build/cpp_fantastic_website" --port "$PORT" --quiet >/tmp/asterforge-smoke.log 2>&1 &
PIDS+=($!)

wait_health() {
  local url=$1
  local i
  for i in {1..40}; do
    if curl -fsS --max-time 1 "$url" 2>/dev/null | grep -q '"status":"ok"'; then
      return 0
    fi
    sleep 0.15
  done
  echo "Server did not become healthy at $url" >&2
  return 1
}

wait_health "$BASE/api/health"

check() {
  local url=$1
  local pattern=$2
  local tmp
  tmp=$(mktemp)
  curl -fsS --max-time 5 -H "Connection: close" "$url" -o "$tmp"
  if grep -q "$pattern" "$tmp"; then
    rm -f "$tmp"
    return 0
  fi
  rm -f "$tmp"
  return 1
}

check "$BASE/" "AsterForge"
check "$BASE/api/health" '"version":"3.1.0"'
check "$BASE/api/version" '"/api/stream"'
check "$BASE/api/presets" '"presets"'
check "$BASE/api/presets" '"pulse"'
check "$BASE/api/presets" '"drift"'
check "$BASE/api/mission?seed=smoke&mode=forge&intensity=73&tempo=51&density=64" '"shortId"'
check "$BASE/api/mission?seed=smoke&mode=forge&intensity=73&tempo=51&density=64" '"z":'
check "$BASE/api/share?seed=smoke%20space&mode=bad&intensity=200&tempo=0&density=44" '"mode":"orbit"'
check "$BASE/api/sky?seed=smoke&layers=3" '"layers"'
check "$BASE/api/orbit?seed=smoke&planets=4" '"planets"'
check "$BASE/api/constellation?seed=smoke&points=12" '"points"'
check "$BASE/api/catalog" '"palettes"'
check "$BASE/api/metrics" '"latency_ms"'

curl -fsSI --max-time 5 -H "Connection: close" "$BASE/api/mission?seed=head&mode=night" | grep -q "Content-Type: application/json"

code=$(curl -s -o /tmp/asterforge-traversal.out -w "%{http_code}" "$BASE/%2e%2e/CMakeLists.txt" || true)
if [[ "$code" != "400" ]]; then
  echo "Expected 400 for path traversal, got $code" >&2
  exit 1
fi

curl -fsS -X POST -H "Content-Type: application/json" --data '{"ping":true}' "$BASE/api/echo" | grep -q 'ping'

allow=$(curl -sD - -o /dev/null -X POST "$BASE/api/health")
echo "$allow" | grep -q "405"
echo "$allow" | grep -i "Allow:" | grep -q "GET"

opt=$(curl -sD - -o /dev/null -X OPTIONS "$BASE/api/health")
echo "$opt" | grep -q "204"
echo "$opt" | grep -i "Access-Control-Allow-Origin" | grep -q "*"

etag=$(curl -sI --max-time 2 "$BASE/styles.css" | tr -d '\r' | awk 'tolower($1)=="etag:" {print $2}')
if [[ -z "$etag" ]]; then
  echo "Missing ETag on styles.css" >&2
  exit 1
fi
curl -sI --max-time 2 -H "If-None-Match: $etag" "$BASE/styles.css" | tr -d '\r' | grep -q "HTTP/1.1 304"

range=$(curl -sD - -o /dev/null -H "Range: bytes=0-15" "$BASE/index.html")
echo "$range" | grep -q "206"

set +e
curl -Ns --max-time 2 "$BASE/api/stream" > /tmp/asterforge-sse.out 2>/dev/null
set -e
grep -q "telemetry" /tmp/asterforge-sse.out

"$ROOT_DIR/build/cpp_fantastic_website" --port "$RL_PORT" --rate-limit 2 --quiet >/tmp/asterforge-rate.log 2>&1 &
PIDS+=($!)
wait_health "http://$HOST:$RL_PORT/api/health"

rate_dir=$(mktemp -d)
curl_pids=()
for i in $(seq 1 24); do
  curl -s --max-time 2 -o /dev/null -w "%{http_code}" "http://$HOST:$RL_PORT/api/health" > "$rate_dir/$i" &
  curl_pids+=($!)
done
for pid in "${curl_pids[@]}"; do
  wait "$pid" || true
done
if ! grep -q 429 "$rate_dir"/*; then
  echo "Expected 429 from rate limiter" >&2
  cat "$rate_dir"/* || true
  exit 1
fi

echo "AsterForge smoke test passed on port $PORT"
