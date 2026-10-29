#!/bin/sh
# Smoke test for the KosmosAV economy: starts a throwaway kosmosavd, takes a job, "flies" it with a heartbeat and checks the pay.
# Usage: sh kav/tests/smoke.sh [path/to/kosmosavd]
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BIN=${1:-$ROOT/build-kav/kosmosavd}
T=$(mktemp -d)
PORT=18431
U=http://127.0.0.1:$PORT
fail() { echo "FAIL: $1"; [ -f "$T/pid" ] && kill "$(cat "$T/pid")" 2>/dev/null; exit 1; }

"$BIN" --db "$T/kav.db" --data "$T/data" --registry "$ROOT/kav/data/registry.txt" --make-admin pilot1 password1 >/dev/null
KEY=$(sqlite3 "$T/kav.db" "select api_token from accounts where callsign='pilot1'")
mkdir -p "$T/data"
("$BIN" --db "$T/kav.db" --data "$T/data" --registry "$ROOT/kav/data/registry.txt" --port $PORT >"$T/log" 2>&1 & echo $! >"$T/pid")
sleep 1
A="Authorization: Bearer $KEY"

curl -s $U/api/v1/jobs | grep -q '"jobs":\[{' || fail "no jobs generated"
curl -s -H "$A" $U/api/v1/me | grep -q '"credits":5000' || fail "starting credits"
curl -s -H "$A" $U/api/v1/me | grep -q '"location":"chippenham"' || fail "starting location"
[ "$(curl -s -o /dev/null -w %{http_code} $U/api/v1/me)" = 401 ] || fail "me without key should be 401"

# a short hop out of Chippenham that the Deltaglider can fly
JOB=$(sqlite3 "$T/kav.db" "select id from jobs where status='open' and origin='chippenham' and dest in ('twickenham','helsinki') and kind='cargo' and qty<=800 limit 1")
if [ -z "$JOB" ]; then
    sqlite3 "$T/kav.db" "insert into jobs(origin,dest,kind,qty,pay,dv,dist,created,deadline) values('chippenham','twickenham','cargo',300,1000,0.1,123,strftime('%s','now'),strftime('%s','now')+86400)"
    JOB=$(sqlite3 "$T/kav.db" "select max(id) from jobs")
fi
DEST=$(sqlite3 "$T/kav.db" "select dest from jobs where id=$JOB")
PAY=$(sqlite3 "$T/kav.db" "select pay from jobs where id=$JOB")

curl -s -H "$A" -d "id=$JOB&ship=nonsense" $U/api/v1/jobs/accept | grep -q error || fail "bad ship accepted"
curl -s -H "$A" -d "id=$JOB&ship=deltaglider" $U/api/v1/jobs/accept | grep -q '"credits":4700' || fail "accept / rental fee"
curl -s -H "$A" -d "id=$JOB" $U/api/v1/jobs/complete | grep -q error || fail "completed without flying"
curl -s -H "$A" -d "ship=CVEL Deltaglider&location=$DEST&job=$JOB" $U/api/v1/heartbeat >/dev/null
sqlite3 "$T/kav.db" "update jobs set accepted=accepted-60 where id=$JOB"
curl -s -H "$A" -d "id=$JOB" $U/api/v1/jobs/complete | grep -q "\"credits\":$((4700 + PAY))" || fail "pay on completion"
curl -s -H "$A" $U/api/v1/me | grep -q "\"location\":\"$DEST\"" || fail "location after delivery"
[ "$(sqlite3 "$T/kav.db" "select count(*) from ledger")" = 2 ] || fail "ledger entries"

# a job from somewhere else cannot be taken
OTHER=$(sqlite3 "$T/kav.db" "select id from jobs where status='open' and origin<>'$DEST' limit 1")
curl -s -H "$A" -d "id=$OTHER&ship=shuttle-a" $U/api/v1/jobs/accept | grep -q "leaves from" || fail "accepted a job from another location"

kill "$(cat "$T/pid")"
rm -rf "$T"
echo "ok"
