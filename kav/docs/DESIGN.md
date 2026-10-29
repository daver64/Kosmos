# KosmosAV (Kosmos Air Virtual)

A shared, persistent freight and passenger network for Kosmos, in the spirit of FSEconomy. Separate from multiplayer.

| Part | Where | State |
|---|---|---|
| `kosmosavd` server and members' web pages | `kav/server/` | built (phase 1a) |
| Static landing page | `/var/www/html/kosmos/kav/index.html` | built |
| Location registry, job board, ships, ledger (web and `/api/v1`) | `kav/data/registry.txt`, `kav/server/` | built (phase 1b) |
| Game hooks | `src/ship.cpp` (`/ships/ship[N]/class`) | done (nothing else needed) |
| KosmosAV GTK client | `kav/client/` | done (Linux tested, Windows built) |
| Interstellar routes | Shuttle-AQ, four depots | done (first version) |

"KosmosLink" is already the native-addon protocol (`src/link/`, TCP 9420). KosmosAV will use it only as the local bridge from the client to the running game.

## Architecture

```
Kosmos <-KosmosLink 9420-> KosmosAV (GTK3) <-HTTPS-> Apache /kosmos/kav/app <-proxy-> kosmosavd (127.0.0.1:9480, SQLite)
                                                                                     ^ browsers: sign-up, board, roster, flights
```

## kosmosavd (built)

C++17, SQLite 3 and OpenSSL libcrypto only. Single threaded, listens on 127.0.0.1 only; Apache does TLS and forwards `X-Forwarded-For`.

Build: `cmake -S kav/server -B build-kav -G Ninja && ninja -C build-kav -j4`

Run: `kosmosavd --db kav.db --data img --port 9480 --base /kosmos/kav/app --secure`
Create the first administrator: `kosmosavd --db kav.db --make-admin CALLSIGN PASSWORD` (8+ characters).

Install or upgrade: `sh scripts/deploy-kav.sh` on this machine, or `sh scripts/deploy-kav.sh dave@gemini [--secure]` for another one. It builds here (4 threads), copies the binary, `kosmosavd.service`, `apache-kav.conf` and `kav/web/index.html` to the target and runs `kav/server/install.sh` there with sudo (you type the sudo password; ssh is run with `-t`). The database and uploads in `/var/lib/kosmosav` are never touched. Per-machine options (such as `--secure` for a machine that serves HTTPS) live in `/etc/default/kosmosav` as `KAV_OPTS`, written on the first install only. The landing page is edited in `kav/web/index.html`.

Pages (relative to `--base`): `/` home, `/account` (API key and change password), `/signup`, `/login`, `/logout`, `/board`, `/board/new`, `/thread/N`, `/img/NAME`, `/roster`, `/flights`, `/admin`.

- Sign-up is a request (callsign, email, note), with a honeypot field, a limit of 3 per address per hour and 200 pending. An administrator approves it in `/admin`; that creates the account and shows a one-time password and API key to pass to the pilot (no email sending yet).
- Passwords: PBKDF2-HMAC-SHA256, 200000 rounds, per-account salt, constant-time compare. Login failures are limited to 8 per address per 10 minutes. Sessions are random 256 bit cookies (HttpOnly, SameSite=Lax, Secure with `--secure`), 14 days, with a per-session CSRF token checked on every state-changing form.
- Message board: threads and replies, plain text (all output is HTML-escaped), optional PNG or JPEG screenshot up to 4 MB (checked by file signature, stored under a random name, served with `nosniff` and a `default-src 'none'` CSP). Not re-encoded yet, so metadata is kept. Admins can delete posts and lock threads. 5 posts a minute per pilot.
- `POST /api/v1/heartbeat` with `Authorization: Bearer <api key>` and a form-encoded body `ship=&location=&job=` fills the Flights page (entries expire after 5 minutes).

## Economy

- `kav/data/registry.txt` (made by `kav/tools/make-registry.py` from `data/sol.json` and `data/bases/*.json`) lists the bodies, the locations (the shipped bases, by their base ids) and the three ships: Deltaglider, Shuttle-A and Dragonfly. The server takes it with `--registry`; install puts it at `/opt/kosmosav/registry.txt`. Cargo capacity, seats, maximum delta-v and rental fee per ship are game-balance numbers in the generator script, not derived from the ships' tanks.
- Delta-v is a rough patched-conic estimate (low-orbit ascent with losses, Hohmann transfer in the common parent's frame, escape and capture at each level, landing with aerobraking where there is air). `kosmosavd --dv-table` prints it for every pair.
- Pay = mass x (0.05 + 0.4 per km/s of delta-v + 0.08 per million km), passengers counted as 90 kg and paid three times, minimum 400.
- Every location keeps six open jobs; they expire after 3 to 30 days. A new pilot has 5000 credits and starts at Chippenham.
- Taking a job: the pilot must be at its origin, pick a ship that can do the delta-v and carry the load (all jobs in hand must use the same ship), and pay the rental fee (not refunded if the job is given back).
- Completing a job: the KosmosAV program's last heartbeat (under 15 minutes old) must be at the destination, at least 20 s after the job was taken. The pilot is then paid, moved there and the ledger updated.
- Pages: `/jobs` (public board, take a job when logged in), `/mine` (credits, jobs, ledger). API: `GET /api/v1/jobs?from=ID`, `GET /api/v1/registry`, and with the API key `GET /api/v1/me`, `POST /api/v1/heartbeat`, `POST /api/v1/jobs/accept|complete|abandon` (form field `id`, and `ship` for accept).
- `sh kav/tests/smoke.sh` runs a throwaway server through a delivery.

## Still to do

1. Done: no game hooks were needed beyond the `/ships/ship[N]/class` property. The client reads the flown ship, landed flag and nearest base (within 15 km) over KosmosLink; cargo is server-side bookkeeping. Job completion requires the last heartbeat to show the same ship as the job.
2. Done: GTK3 client in `kav/client` (built into `addons/kosmosav/bin`; libcurl on Linux, WinHTTP on Windows via `scripts/build-win-tools.sh`), tested by `kav/tests/client.sh` and against the real game on Linux. The Windows exe compiles but has not been run. Still to do: fleet, ledger and map views.
3. Done: unit-style tests (`kav/tests/smoke.sh`, `kav/tests/client.sh`). Still to do: a `--selftest` delivery.
4. Reviewed once (CSRF, cookies, rate limits and CSP were already in place). The program is trusted to report location honestly, so completion has a minimum trip time of 20 s + 8 s per km/s of delta-v; true verification would need the game to sign its reports. Still to do before the pages are opened to the public: HTTPS (`--secure`) on gemini; image re-encoding; password reset (forgotten password); email notification of approval.
5. Interstellar routing: done for the first version. The registry has `system` records (star positions in light years, as in `src/galaxy.cpp`) and the CVEL Shuttle-AQ (`jump_ly` 50) is the only ship that can take a job between systems. A cross-system route costs only the climb and landing delta-v (a jump leaves from and arrives in low orbit) and pays 4 credits per kg per light year instead of the distance term; the depots are `data/bases/depots.json`. Jobs between stars are about a quarter of those from Sol bases, and every job from a depot is interstellar (each system has one depot). Still to do: more depots, multi-leg and passenger-liner routes, systems beyond the five.
6. Packaging: a server tarball, release notes, README and in-game help text, website manual pages.
