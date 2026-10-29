#!/bin/sh
# End-to-end test of the KosmosAV program's logic: a throwaway server and test_agent playing a fake game through a whole delivery.
# Usage: sh kav/tests/client.sh   (needs build-kav/kosmosavd and build-kavc/test_agent; scripts/deploy-kav.sh builds the first)
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BIN=$ROOT/build-kav/kosmosavd
AGENT=$ROOT/build-kavc/test_agent
T=$(mktemp -d)
PORT=18432
REG=$ROOT/kav/data/registry.txt

"$BIN" --db "$T/kav.db" --data "$T/data" --registry "$REG" --make-admin pilot1 password1 >/dev/null
KEY=$(sqlite3 "$T/kav.db" "select api_token from accounts where callsign='pilot1'")
mkdir -p "$T/data"
("$BIN" --db "$T/kav.db" --data "$T/data" --registry "$REG" --port $PORT >"$T/log" 2>&1 & echo $! >"$T/pid")
sleep 1
sqlite3 "$T/kav.db" "insert into jobs(origin,dest,kind,qty,pay,dv,dist,created,deadline) values('chippenham','twickenham','cargo',300,1000,0.1,123,strftime('%s','now'),strftime('%s','now')+86400)"
RESULT=0
"$AGENT" "http://127.0.0.1:$PORT" "$KEY" chippenham twickenham deltaglider || RESULT=1
kill "$(cat "$T/pid")"
rm -rf "$T"
[ $RESULT = 0 ] && echo "client test ok" || echo "client test FAILED"
exit $RESULT
