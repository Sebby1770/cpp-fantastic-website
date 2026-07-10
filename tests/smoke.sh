#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="${PORT:-8097}"
BASE="http://localhost:$PORT"

cmake -S "$ROOT_DIR" -B "$ROOT_DIR/build" >/dev/null
cmake --build "$ROOT_DIR/build" >/dev/null

# Unit tests (no server required)
"$ROOT_DIR/build/aster_unit_tests"

"$ROOT_DIR/build/cpp_fantastic_website" --port "$PORT" --quiet >/tmp/asterforge-smoke.log 2>&1 &
SERVER_PID=$!
trap 'kill "$SERVER_PID" >/dev/null 2>&1 || true' EXIT

for _ in {1..40}; do
  if curl -fsS "$BASE/api/health" 2>/dev/null | grep -q '"status":"ok"'; then
    break
  fi
  sleep 0.15
done

echo "== static + health =="
curl -fsS "$BASE/" | grep -q "AsterForge"
curl -fsS "$BASE/api/health" | grep -q '"status":"ok"'
curl -fsS "$BASE/api/health" | grep -q '"uptime_seconds"'
curl -fsS "$BASE/api/health" | grep -q '"version"'
curl -fsS "$BASE/api/health" | grep -q '"request_count"'

echo "== mission =="
curl -fsS "$BASE/api/mission?seed=smoke&mode=forge&intensity=73&tempo=51" | grep -q '"missionName"'
curl -fsS "$BASE/api/mission?seed=smoke&mode=forge&intensity=73&tempo=51&palette=ember" | grep -q '"paletteId"'

echo "== palettes =="
curl -fsS "$BASE/api/palettes" | grep -q '"palettes"'
curl -fsS "$BASE/api/palettes" | grep -q '"ember"'

echo "== constellation =="
curl -fsS "$BASE/api/constellation?seed=7&points=12" | grep -q '"stars"'
curl -fsS "$BASE/api/constellation?seed=7&points=12" | grep -q '"links"'

echo "== metrics =="
curl -fsS "$BASE/api/metrics" | grep -q '"total_requests"'
curl -fsS "$BASE/api/metrics" | grep -q '"by_path"'

echo "== echo =="
ECHO_BODY='{"hello":"world","n":42}'
curl -fsS -X POST "$BASE/api/echo" \
  -H "Content-Type: application/json" \
  -d "$ECHO_BODY" | grep -q '"echoed":true'
curl -fsS -X POST "$BASE/api/echo" \
  -H "Content-Type: application/json" \
  -d "$ECHO_BODY" | grep -q 'hello'

echo "== HEAD / OPTIONS / headers =="
# curl -I uses HEAD and does not expect a response body (unlike -X HEAD).
HEAD_OUT=$(curl -sS -I "$BASE/api/health")
echo "$HEAD_OUT" | grep -Eiq "HTTP/1\.[01] 200"
echo "$HEAD_OUT" | grep -qi "X-Content-Type-Options: nosniff"
echo "$HEAD_OUT" | grep -qi "Access-Control-Allow-Origin: \*"
# Ensure body length is advertised but body is empty for HEAD
echo "$HEAD_OUT" | grep -qi "Content-Length:"
# Headers-only dump should not include the JSON body
echo "$HEAD_OUT" | grep -vq '"status"'

OPT_OUT=$(curl -sS -D - -o /dev/null -X OPTIONS "$BASE/api/mission")
echo "$OPT_OUT" | grep -Eiq "HTTP/1\.[01] (204|200)"
echo "$OPT_OUT" | grep -qi "Access-Control-Allow-Methods"

# Security header on GET as well
GET_HDR=$(curl -sS -D - -o /dev/null "$BASE/api/health")
echo "$GET_HDR" | grep -qi "X-Content-Type-Options: nosniff"

echo "AsterForge smoke test passed on port $PORT"
