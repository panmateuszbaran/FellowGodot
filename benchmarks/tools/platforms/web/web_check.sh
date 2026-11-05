#!/bin/bash
# FellowEngine's web check (milestone 50): the web template (platforms.sh build web) run in headless Chromium with a
# .pck of project/ made by a desktop build (PCKPacker; a template cannot export).
#   web_check.sh <web template zip> <a desktop godot binary> <output directory> [port]
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ZIP=$1
GODOT=$2
OUT=$(mkdir -p "$3" && cd "$3" && pwd)
PORT=${4:-8765}
SITE="$OUT/site"
mkdir -p "$SITE"
unzip -o -q "$ZIP" -d "$SITE"
cp "$HERE/index.html" "$SITE/index.html"
"$GODOT" --headless --path "$HERE/project" -s res://make_pck.gd -- "--out=$SITE/godot.pck" > "$OUT/make_pck.log" 2>&1
grep '^MAKE_PCK' "$OUT/make_pck.log"
python3 "$HERE/serve.py" "$SITE" "$PORT" &
SERVER=$!
trap 'kill $SERVER' EXIT
for _ in $(seq 50); do curl -s -o /dev/null "http://127.0.0.1:$PORT/index.html" && break; python3 -c "import time; time.sleep(0.1)"; done
NODE_PATH=${NODE_PATH:-$(npm root -g)} node "$HERE/web_check.js" "http://127.0.0.1:$PORT/index.html" "$OUT" "${WEB_CHECK_TIMEOUT:-600}"
