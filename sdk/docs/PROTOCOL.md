# KosmosLink protocol, version 3

Most addons should use the C library (`kosmos_link.h`). This page is for writing a client in another language.

## Transport

Kosmos listens on `127.0.0.1:9420` (loopback only, TCP). Connect to the numeric address, not `localhost`. When Kosmos starts your program it sets `KOSMOS_LINK=127.0.0.1:<port>` and `KOSMOS_LINK_TOKEN=<token>`.

## Frames

All integers are little-endian.

```
u32  length      bytes that follow this field (at least 6, at most 1 MiB)
u16  type
u32  request id  chosen by the client; replies carry the same id; pushed messages use 0
...  payload
```

A malformed frame, a frame larger than 1 MiB or a missing HELLO within 5 seconds closes the connection.

Strings are `u16 byte-length` followed by UTF-8 bytes. A value is a `u8` tag (0 none, 1 bool, 2 int32, 3 double, 4 string) followed by `u8` / `i32` / `f64` / string.

## Client to Kosmos

| Type | Name | Payload |
|---|---|---|
| 1 | hello | `u16 protocol` (1 or 2; send 2), str addon id, str name, str version, str token, `u32 capabilities` |
| 2 | subscribe | str path, `u16 hz` (0 = every change) |
| 3 | unsubscribe | str path |
| 4 | get | str path |
| 5 | set | str path, value |
| 6 | define_key | str id, str label, str group, str key, `u8 shift`, `u8 mode` (2 = hold, else toggle) |
| 7 | spawn_ship | str ship name, `f64 x, y, z` (metres, in the flown ship's frame) |
| 8 | release | `i32 hardpoint` |
| 9 | give | str equipment id, `i32 count` |
| 10 | define_mfd | str id, str title |
| 11 | draw | str id, `u16 count`, then per command: `u8 kind`, `f32 a b c d e f`, `u8 r g b align`, str text |
| 12 | log | str text |
| 13 | ping | none |
| 14 | weather_at | str body (empty: the flown ship's), `f64 latitude, longitude, altitude` (degrees; metres above the ground) |
| 15 | set_weather | str mode (`metar`, `preset` or `auto`), str argument |
| 16 | subscribe_event | str event name |
| 17 | unsubscribe_event | str event name |
| 18 | list_ships | `f64 radius` (metres, 0 = all), `u8 flags` (1 = include buildings) |
| 19 | remove_ship | `u32 index` |
| 20 | place_ship | `u32 index`, str body (empty: the flown ship's), `f64 latitude, longitude, altitude, heading, speed` |
| 21 | list_facilities | str kind (`airport`, `base` or empty), str body, `f64 latitude, longitude, radius_km`, `u16 max` (0 = 100, at most 200) |

Draw kinds: 0 line, 1 rect, 2 fillrect, 3 circle, 4 disc, 5 tri, 6 text, 7 image. Coordinates are in MFD units, 0-400 on both axes. At most 2000 commands per page and 512 subscriptions per connection.

## Kosmos to client

| Type | Name | Payload |
|---|---|---|
| 101 | welcome | `u16 protocol`, str Kosmos version, `u32 granted` |
| 102 | error | `u16 code`, str message |
| 103 | value | str path, value (reply to get, or a subscription update with request id 0) |
| 104 | event | str name, str argument |
| 105 | ok | `u32 handle` (the new ship's index for spawn_ship, otherwise 0) |
| 106 | pong | none |
| 107 | weather | `f64` wind east, wind north, wind speed, wind from, gust, rain, thunder, cloud cover, cloud base, cloud top, visibility, temperature, pressure; str station; str METAR |
| 108 | ship_list | `u32 count`, then per ship: `u32 index`, str name, str body, `f64` latitude, longitude, altitude, heading, speed, vertical speed, distance, `u8 flags` (1 flown, 2 landed, 4 crashed, 8 spawned by a link) |
| 109 | facility_list | `u32 count`, then per base: str id, name, icao, body; `f64` latitude, longitude, elevation, heading, distance; `u16 runways` each: str name, `f64` latitude, longitude, heading, length, width, `u8 ils` then `f64` frequencies; `u16 navaids` each: str type, id, name, `f64` latitude, longitude, frequency, range |

Error codes: 1 bad request, 2 unknown, 3 denied, 4 failed, 5 version.

Events: `ship` (the flown ship changed; argument is its name), `mfd` (redraw the page named in the argument; sent at 20 Hz while the page is on screen), `granted` (the player changed the grant; argument is the new capability mask), `shutdown`.

## Protocol 2

Protocol 2 adds requests 14 to 21 and replies 107 to 109. Kosmos accepts a hello carrying 1 or 2, and a version 1 client simply never sends the new requests. An older Kosmos (protocol 1) refuses a version 2 hello with error 5; retry with 1 if you want to support it. Weather values are SI units except temperature (Celsius) and pressure (hPa); wind east and north are the way the air moves, wind from is the compass direction it blows from. The weather the game shows is a global override while `set_weather` is in force: it lasts until the player (or another addon) changes it, and `auto` returns to simulated weather. `remove_ship` works only on ships link addons spawned and never on the flown ship; the ships after it move down one place, so list again afterwards. `place_ship` may move any ship including the flown one (in multiplayer the other players will not follow). List replies are capped (2000 ships, 200 facilities) to stay under the frame limit.

Opt-in events, sent only after `subscribe_event`: `pause` (argument 1 or 0), `warp` (the new factor), `scenario` (the scenario id, when one starts), `soi` (the flown ship entered a new body's sphere of influence; the name), `crash` and `touchdown` (the flown ship's name), `weather` (conditions changed noticeably; the METAR if any), `ships` (the number of ships changed), `tick` (once a second; the Julian date). Always-on events are unchanged.

## Protocol 3

Protocol 3 adds requests 22 to 31. Kosmos accepts a hello carrying 1, 2 or 3. Requests answer with `ok` or an error frame (102) where noted; the others are fire and forget and report failures as errors with request id 0.

| Type | Name | Payload |
|---|---|---|
| 22 | ui_window | str id, str title, `u16 n`, then per widget: `u8 kind` (0 label, 1 button, 2 checkbox, 3 slider, 4 text, 5 separator), str id, str label, str text, `f64` value, low, high. Replaces a window of the same id. Needs `ui`. Fire and forget |
| 23 | ui_close | str id |
| 24 | ui_notify | str text, `f64` seconds |
| 25 | lockstep | `u8 enable`, `u32 max_wait_ms`, `f64 fixed_dt` (0, or 0.001 to 0.1). Needs `controls`. One holder at a time. Replies ok |
| 26 | step_done | `u32 frame`. Fire and forget |
| 27 | claim_ship | `u32 index`, `u8 passthrough`. Needs `vessel`. Replies ok or an error |
| 28 | release_ship | `u32 index` |
| 29 | ship_value | `u32 index`, str name, `f64 value`. A name beginning `controls/` sets a module control |
| 30 | ship_force | `u32 index`, 3 x `f64` force, 3 x `f64` torque (ship axes, N and N m) |
| 31 | draw3d | str layer, `u16 n`, then per primitive: `u8 kind` (0 line, 1 marker, 2 label, 3 ring, 4 sphere), `u8 frame` (0 lat/lon/alt, 1 body-fixed, 2 ship), `u32 ship`, 3 x `f64` a, 3 x `f64` b, `f64 size`, `u8` r, g, b, alpha, `u8 depth`, str text. `n` = 0 removes the layer. Needs `draw3d` |

New events: `step` (to the lockstep program only; argument `frame dt simtime`) and `ui` (to the window's owner; argument `window widget value`; a closed window sends an empty widget and `closed`). Kosmos sends a `ping` frame (type 13) to a protocol 3 client that has been silent for the timeout; any frame from the client, such as a `ping` back, resets the timer, and a client silent for twice the timeout is dropped. Protocol 1 and 2 clients are not timed out. Limits: 8 windows, 64 widgets, 8 layers and 2000 primitives per client. A program's windows, layers, claims and lockstep end when it disconnects.

## Capabilities

read 1, controls 2, write 4, spawn 8, draw 16, keys 32, world 64 (change the weather, place and remove ships), ui 128, vessel 256, draw3d 512. The player grants these per addon. A manifest's `capabilities` narrow what a request can obtain. The welcome message and `granted` events report what you actually have. Requests beyond that fail with error 3.

## Limits

Lockstep waits at most the `max_wait_ms` you ask for. A page that has not been redrawn for a second shows NO SIGNAL.
