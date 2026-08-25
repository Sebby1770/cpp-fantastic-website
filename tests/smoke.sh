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

echo "== sky =="
curl -fsS "$BASE/api/sky?seed=smoke&layers=4" | grep -q '"layers"'
curl -fsS "$BASE/api/sky?seed=smoke&layers=4" | grep -q '"aurora"'
curl -fsS "$BASE/api/version" | grep -q '/api/sky'
CODE=$(curl -s -o /dev/null -w '%{http_code}' -X POST "$BASE/api/sky")
[ "$CODE" = "405" ]

echo "== orbit =="
curl -fsS "$BASE/api/orbit?seed=7&planets=5" | grep -q '"planets"'
curl -fsS "$BASE/api/orbit?seed=7&planets=5" | grep -q '"star"'
curl -fsS "$BASE/api/version" | grep -q '/api/orbit'
CODE=$(curl -s -o /dev/null -w '%{http_code}' -X POST "$BASE/api/orbit")
[ "$CODE" = "405" ]

echo "== comet =="
curl -fsS "$BASE/api/comet?seed=7&count=2" | grep -q '"comets"'
curl -fsS "$BASE/api/comet?seed=7&count=2" | grep -q '"hue"'
curl -fsS "$BASE/api/version" | grep -q '/api/comet'
CODE=$(curl -s -o /dev/null -w '%{http_code}' -X POST "$BASE/api/comet")
[ "$CODE" = "405" ]

echo "== unknown api 404 =="
CODE=$(curl -s -o /dev/null -w '%{http_code}' "$BASE/api/does-not-exist")
[ "$CODE" = "404" ]
curl -s "$BASE/api/does-not-exist" | grep -q '"error":"not_found"'

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

echo "== keep-alive =="
REUSED=$(curl -v -s -o /dev/null "$BASE/api/health" -o /dev/null "$BASE/api/palettes" 2>&1 | grep -c 'Re-using existing connection' || true)
[ "$REUSED" -ge 1 ]

echo "== 413 payload too large =="
BIG_BODY=/tmp/asterforge-big-body.bin
head -c 2097152 /dev/zero > "$BIG_BODY"
CODE=$(curl -s -o /dev/null -w '%{http_code}' -X POST --data-binary @"$BIG_BODY" "$BASE/api/echo")
[ "$CODE" = "413" ]
rm -f "$BIG_BODY"

echo "== 431 header cap =="
BIG_HEADER=$(head -c 70000 /dev/zero | tr '\0' 'a')
CODE=$(curl -s -o /dev/null -w '%{http_code}' -H "X-Padding: $BIG_HEADER" "$BASE/api/health")
[ "$CODE" = "431" ]

echo "== concurrency =="
CONC_OUT=/tmp/asterforge-conc.out
: > "$CONC_OUT"
CURL_PIDS=()
for _ in {1..50}; do
  curl -s -o /dev/null -w '%{http_code}\n' "$BASE/api/health" >> "$CONC_OUT" &
  CURL_PIDS+=("$!")
done
wait "${CURL_PIDS[@]}"
[ "$(grep -c '^200$' "$CONC_OUT")" -eq 50 ]

echo "== static caching (ETag / Last-Modified / 304) =="
CSS_HDRS=$(curl -sD - -o /dev/null "$BASE/styles.css")
echo "$CSS_HDRS" | grep -qi '^ETag:'
echo "$CSS_HDRS" | grep -qi '^Last-Modified:'
echo "$CSS_HDRS" | grep -qi 'Cache-Control: public, max-age=300'
ETAG=$(echo "$CSS_HDRS" | grep -i '^ETag:' | awk '{print $2}' | tr -d '\r')
CODE=$(curl -s -o /dev/null -w '%{http_code}' -H "If-None-Match: $ETAG" "$BASE/styles.css")
[ "$CODE" = "304" ]
CODE=$(curl -s -o /dev/null -w '%{http_code}' -H "If-None-Match: \"mismatch\"" "$BASE/styles.css")
[ "$CODE" = "200" ]
LASTMOD=$(echo "$CSS_HDRS" | grep -i '^Last-Modified:' | sed 's/^[^:]*: //' | tr -d '\r')
CODE=$(curl -s -o /dev/null -w '%{http_code}' -H "If-Modified-Since: $LASTMOD" "$BASE/styles.css")
[ "$CODE" = "304" ]
curl -sD - -o /dev/null "$BASE/api/health" | grep -qi 'Cache-Control: no-store'

echo "== range requests (206 / Content-Range / 416) =="
echo "$CSS_HDRS" | grep -qi '^Accept-Ranges: bytes'
FULL_LEN=$(echo "$CSS_HDRS" | grep -i '^Content-Length:' | awk '{print $2}' | tr -d '\r')
RANGE_HDRS=$(curl -sD - -o /tmp/asterforge-range.bin -H 'Range: bytes=0-9' "$BASE/styles.css")
echo "$RANGE_HDRS" | grep -qiE 'HTTP/1\.[01] 206'
echo "$RANGE_HDRS" | grep -qi "Content-Range: bytes 0-9/$FULL_LEN"
[ "$(wc -c < /tmp/asterforge-range.bin | tr -d ' ')" = "10" ]
# The partial body must equal the first 10 bytes of the real file.
head -c 10 public/styles.css > /tmp/asterforge-range-expect.bin
cmp -s /tmp/asterforge-range.bin /tmp/asterforge-range-expect.bin
# Suffix range: last 16 bytes.
CODE=$(curl -s -o /dev/null -w '%{http_code}' -H 'Range: bytes=-16' "$BASE/styles.css")
[ "$CODE" = "206" ]
# Unsatisfiable range -> 416 with a Content-Range: bytes */<len> header.
UNSAT_HDRS=$(curl -sD - -o /dev/null -H 'Range: bytes=99999999-' "$BASE/styles.css")
echo "$UNSAT_HDRS" | grep -qiE 'HTTP/1\.[01] 416'
echo "$UNSAT_HDRS" | grep -qi "Content-Range: bytes \*/$FULL_LEN"
rm -f /tmp/asterforge-range.bin /tmp/asterforge-range-expect.bin

echo "== gzip sidecar negotiation =="
# Create a .gz sidecar next to a served file, then confirm negotiation.
gzip -9 -c public/styles.css > public/styles.css.gz
GZ_HDRS=$(curl -sD - -o /dev/null -H 'Accept-Encoding: gzip' "$BASE/styles.css")
echo "$GZ_HDRS" | grep -qi '^Content-Encoding: gzip'
echo "$GZ_HDRS" | grep -qi '^Vary: Accept-Encoding'
# The gzip representation must have its own ETag (…-gz).
echo "$GZ_HDRS" | grep -i '^ETag:' | grep -q 'gz"'
# Decoded gzip body must equal the original file.
curl -s -H 'Accept-Encoding: gzip' --compressed "$BASE/styles.css" | cmp -s - public/styles.css
# No Accept-Encoding -> identity (no Content-Encoding header).
curl -sD - -o /dev/null "$BASE/styles.css" | grep -qiv '^Content-Encoding:'
# gzip;q=0 explicitly refuses gzip.
curl -sD - -o /dev/null -H 'Accept-Encoding: gzip;q=0' "$BASE/styles.css" \
  | { ! grep -qi '^Content-Encoding: gzip'; }
rm -f public/styles.css.gz

echo "== path traversal defense =="
CODE=$(curl -s -o /dev/null -w '%{http_code}' --path-as-is "$BASE/../src/main.cpp")
[ "$CODE" = "400" ] || [ "$CODE" = "404" ]
CODE=$(curl -s -o /dev/null -w '%{http_code}' "$BASE/%2e%2e/%2e%2e/etc/passwd")
[ "$CODE" = "400" ] || [ "$CODE" = "404" ]

echo "== metrics (status classes + latency, shapes preserved) =="
METRICS_JSON=$(curl -fsS "$BASE/api/metrics")
echo "$METRICS_JSON" | grep -q '"status"'
echo "$METRICS_JSON" | grep -q '"2xx"'
echo "$METRICS_JSON" | grep -q '"latency_ms"'
echo "$METRICS_JSON" | grep -q '"p99"'
echo "$METRICS_JSON" | grep -q '"total_requests"'
echo "$METRICS_JSON" | grep -q '"by_path"'

echo "== SSE telemetry stream =="
STREAM_OUT=$(curl -sN --max-time 3 "$BASE/api/stream" || true)
echo "$STREAM_OUT" | grep -q 'event: telemetry'
echo "$STREAM_OUT" | grep -q '"total_requests"'
echo "$STREAM_OUT" | grep -q '"tick"'
echo "$STREAM_OUT" | grep -q '"latency_ms"'
curl -sN --max-time 2 -D /tmp/sse-headers.txt "$BASE/api/stream" >/dev/null || true
grep -qi 'Content-Type: text/event-stream' /tmp/sse-headers.txt
grep -qi 'Cache-Control: no-store' /tmp/sse-headers.txt
grep -qi 'X-Accel-Buffering: no' /tmp/sse-headers.txt

echo "== frontend wiring =="
grep -q 'EventSource' "$ROOT_DIR/public/app.js"
grep -q 'telemetryPanel' "$ROOT_DIR/public/index.html"
grep -q 'sparkCanvas' "$ROOT_DIR/public/index.html"
grep -q 'shortcutOverlay' "$ROOT_DIR/public/index.html"
grep -q 'warpButton' "$ROOT_DIR/public/index.html"
grep -q 'orbitToggle' "$ROOT_DIR/public/index.html"
grep -q 'nebulaToggle' "$ROOT_DIR/public/index.html"
grep -q 'generateSky' "$ROOT_DIR/public/app.js"
grep -q 'generateOrbit' "$ROOT_DIR/public/app.js"
grep -q 'generateConstellation' "$ROOT_DIR/public/app.js"
grep -q 'generateComet' "$ROOT_DIR/public/app.js"
grep -q '/api/comet' "$ROOT_DIR/public/app.js"
grep -q 'cometToggle' "$ROOT_DIR/public/index.html"
grep -q 'planetInspect' "$ROOT_DIR/public/index.html"
grep -q 'data-timescale' "$ROOT_DIR/public/index.html"
grep -q 'prefers-reduced-motion' "$ROOT_DIR/public/styles.css"
grep -q 'prefers-reduced-motion' "$ROOT_DIR/public/app.js"

echo "== rate limiting =="
PORT3=$((PORT + 2))
"$ROOT_DIR/build/cpp_fantastic_website" --port "$PORT3" --rate-limit 3 --quiet >/tmp/asterforge-smoke3.log 2>&1 &
PID3=$!
trap 'kill "$SERVER_PID" "$PID3" >/dev/null 2>&1 || true' EXIT
for _ in {1..40}; do
  if curl -fsS "http://localhost:$PORT3/api/health" >/dev/null 2>&1; then
    break
  fi
  sleep 0.15
done
RL_OUT=/tmp/asterforge-rl.out
: > "$RL_OUT"
for _ in {1..20}; do
  curl -sD - -o /dev/null "http://localhost:$PORT3/api/health" >> "$RL_OUT"
done
grep -q " 429" "$RL_OUT"
grep -qi "Retry-After: 1" "$RL_OUT"
kill "$PID3" >/dev/null 2>&1 || true

echo "== protocol edges (405 Allow, HEAD length, 404, echo JSON) =="
curl -sD - -o /dev/null -X DELETE "$BASE/api/health" | grep -qi "^Allow:"
HEAD_LEN=$(curl -sS -I "$BASE/api/health" | tr -d '\r' | awk 'tolower($1)=="content-length:" {print $2}')
[ -n "$HEAD_LEN" ] && [ "$HEAD_LEN" -gt 0 ]
CODE=$(curl -s -o /dev/null -w '%{http_code}' "$BASE/definitely/not/here")
[ "$CODE" = "404" ]
printf '{"msg":"quote \\" and\\nnewline"}' \
  | curl -sS -X POST -H 'Content-Type: application/json' --data-binary @- "$BASE/api/echo" \
  | python3 -m json.tool >/dev/null

echo "== graceful shutdown + access log =="
PORT2=$((PORT + 1))
LOG2=/tmp/asterforge-smoke2.log
"$ROOT_DIR/build/cpp_fantastic_website" --port "$PORT2" > "$LOG2" 2>&1 &
PID2=$!
trap 'kill "$SERVER_PID" "$PID2" "$PID3" >/dev/null 2>&1 || true' EXIT
for _ in {1..40}; do
  if curl -fsS "http://localhost:$PORT2/api/health" >/dev/null 2>&1; then
    break
  fi
  sleep 0.15
done
curl -fsS "http://localhost:$PORT2/api/health" >/dev/null
kill -TERM "$PID2"
for _ in {1..30}; do
  kill -0 "$PID2" 2>/dev/null || break
  sleep 0.1
done
if kill -0 "$PID2" 2>/dev/null; then
  echo "server did not shut down within 3s"
  kill -9 "$PID2"
  exit 1
fi
SHUTDOWN_STATUS=0
wait "$PID2" || SHUTDOWN_STATUS=$?
[ "$SHUTDOWN_STATUS" -eq 0 ]
# Default format is JSON: the request line must be a real, parseable object
# carrying the documented fields (not just text that looks JSON-ish).
JSON_LINE=$(grep '^{"time"' "$LOG2" | head -1)
[ -n "$JSON_LINE" ]
echo "$JSON_LINE" | python3 -c '
import json, sys
entry = json.load(sys.stdin)
for key in ("time", "ip", "method", "path", "status", "bytes", "ms"):
    assert key in entry, f"missing {key}"
assert entry["method"] == "GET", entry
assert entry["path"] == "/api/health", entry
assert entry["status"] == 200, entry
assert isinstance(entry["ms"], (int, float)), entry
'
grep -q 'AsterForge shutting down' "$LOG2"

echo "== access log --log-format text =="
PORT5=$((PORT + 4))
LOG5=/tmp/asterforge-smoke5.log
"$ROOT_DIR/build/cpp_fantastic_website" --port "$PORT5" --log-format text > "$LOG5" 2>&1 &
PID5=$!
# Register immediately: a failing assertion below must not leak a server that
# then squats the port and breaks every later run.
trap 'kill "$SERVER_PID" "$PID2" "$PID3" "$PID5" >/dev/null 2>&1 || true' EXIT
for _ in {1..40}; do
  if curl -fsS "http://localhost:$PORT5/api/health" >/dev/null 2>&1; then
    break
  fi
  sleep 0.15
done
curl -fsS "http://localhost:$PORT5/api/health" >/dev/null
# The access line is written after the response is sent, so poll for it rather
# than assuming it has landed by the time curl returns.
for _ in {1..40}; do
  grep -q '"GET /api/health HTTP/1.1" 200' "$LOG5" && break
  sleep 0.1
done
grep -q '"GET /api/health HTTP/1.1" 200' "$LOG5"
grep -q 'ms' "$LOG5"
# Text mode must not emit JSON objects.
! grep -q '^{"time"' "$LOG5"
kill "$PID5" >/dev/null 2>&1 || true
wait "$PID5" 2>/dev/null || true

echo "== graceful shutdown with open SSE stream =="
PORT4=$((PORT + 3))
LOG4=/tmp/asterforge-smoke4.log
"$ROOT_DIR/build/cpp_fantastic_website" --port "$PORT4" --quiet > "$LOG4" 2>&1 &
PID4=$!
trap 'kill "$SERVER_PID" "$PID2" "$PID3" "$PID4" "$PID5" >/dev/null 2>&1 || true' EXIT
for _ in {1..40}; do
  if curl -fsS "http://localhost:$PORT4/api/health" >/dev/null 2>&1; then
    break
  fi
  sleep 0.15
done
curl -sN "http://localhost:$PORT4/api/stream" >/dev/null 2>&1 &
SSE_PID=$!
sleep 1
kill -TERM "$PID4"
for _ in {1..30}; do
  kill -0 "$PID4" 2>/dev/null || break
  sleep 0.1
done
if kill -0 "$PID4" 2>/dev/null; then
  echo "server with open SSE stream did not shut down within 3s"
  kill -9 "$PID4" "$SSE_PID" >/dev/null 2>&1 || true
  exit 1
fi
SHUTDOWN_STATUS=0
wait "$PID4" || SHUTDOWN_STATUS=$?
[ "$SHUTDOWN_STATUS" -eq 0 ]
kill "$SSE_PID" >/dev/null 2>&1 || true

echo "AsterForge smoke test passed on port $PORT"
