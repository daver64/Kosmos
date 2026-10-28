# KosmosLink protocol, version 1

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
| 1 | hello | `u16 protocol(1)`, str addon id, str name, str version, str token, `u32 capabilities` |
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

Error codes: 1 bad request, 2 unknown, 3 denied, 4 failed, 5 version.

Events: `ship` (the flown ship changed; argument is its name), `mfd` (redraw the page named in the argument; sent at 20 Hz while the page is on screen), `granted` (the player changed the grant; argument is the new capability mask), `shutdown`.

## Capabilities

read 1, controls 2, write 4, spawn 8, draw 16, keys 32. The player grants these per addon. A manifest's `capabilities` narrow what a request can obtain. The welcome message and `granted` events report what you actually have. Requests beyond that fail with error 3.

## Limits

There is no lockstep mode, no way to remove a spawned ship, and no ping timeout. A page that has not been redrawn for a second shows NO SIGNAL.
