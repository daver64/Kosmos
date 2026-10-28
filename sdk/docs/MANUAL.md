# Kosmos SDK manual: native addons

This manual extends the Kosmos modders manual. Most addons are ships, scenarios, MFD scripts and Lua, and need nothing here. A **native addon** is a separate program, written in C, C++, Python or anything else that can open a socket, that runs next to Kosmos and talks to it through **KosmosLink**. Use one when Lua is not enough: a hardware bridge, a heavy autopilot, an external tool or a library you do not want to port.

## How it works

Kosmos listens on `127.0.0.1` (this computer only, TCP port 9420). Your program connects, says who it is, and then exchanges small messages:

- read properties once, or **subscribe** and be told when they change;
- write properties (throttle, gear, lights, any `/controls/` value);
- define **key actions** the player can rebind in View > Keyboard;
- **spawn** ships, **release** ordnance and **give** equipment;
- add an **MFD page** and draw on it.

Your program never runs inside Kosmos. If it crashes, Kosmos carries on, and the MFD page shows NO SIGNAL. This is the same arm's-length model as SimConnect in Microsoft Flight Simulator.

### Running Kosmos with the link

- **View > Native addons** lists every addon that declares a native program. The player ticks the ones to allow, sees what each may do, and can start, stop or restart its program. Approval is remembered by addon id.
- **Listen for programs started outside Kosmos** accepts a program you start by hand (useful while developing). The setting is `/sim/settings/link-listen`.
- Command line: `--link` listens for outside programs, `--link-port N` changes the port (default 9420), `--link-window` opens the Native addons window at start.
- The link listens on loopback only, so nothing on the network can reach it.

A connection goes: connect, send *hello* (addon id, name, version, the capabilities you want), receive *welcome* (the capabilities you actually have), then make requests and receive replies, subscription updates and events. A request that expects an answer carries an id and the reply carries the same id.

## What is in the SDK

| Path | Contents |
|---|---|
| `include/kosmos_link.h` | The C99 client API (documented in comments). |
| `include/kosmos_link.hpp` | A small C++17 wrapper. |
| `src/kosmos_link.c` | The client implementation; no dependencies. |
| `CMakeLists.txt` | Builds the `kosmos::link` library; use it with `add_subdirectory`. |
| `examples/` | `hello_props.c`, `speed_mfd.cpp`, and a pure-Python client with `speed_hold.py`. |
| `template/` | An addon folder to copy: `addon.json`, a CMake project and `src/main.cpp`. |
| `tools/kosmos_link_sim.py` | A mock Kosmos for developing without the game. |
| `docs/PROTOCOL.md` | The wire protocol, for other languages. |

## Your first native addon

1. Copy `template/` somewhere and rename it. Edit `addon.json` so `id` matches your program (letters, digits, `-` and `_`).
2. Build it: `cmake -S . -B build && cmake --build build`. The program is written to `bin/`.
3. Copy the whole folder into Kosmos's `addons/` folder, and start Kosmos.
4. Open View > Native addons and tick your addon. Kosmos starts the program and connects it.

### The native block in addon.json

```json
{
  "id": "my-native-addon",
  "name": "My native addon",
  "version": "0.1",
  "native": {
    "run": { "linux": "bin/my-native-addon", "windows": "bin/my-native-addon.exe" },
    "capabilities": ["read", "draw", "keys"],
    "autostart": true,
    "restart": true
  }
}
```

- `run` is the program for each platform, relative to the addon folder. It must stay inside the folder.
- `capabilities` is the most your addon may ever have (below). Leave it out to allow whatever the program asks for.
- `autostart` (default true) starts the program when Kosmos starts, if the player has allowed it.
- `restart` restarts it if it exits with a failure, up to three times.

An addon with no `run` entry is for programs the player starts by hand. Tick "Listen for programs started outside Kosmos" in the Native addons window (or start Kosmos with `--link`).

Kosmos gives your program two environment variables: `KOSMOS_LINK=127.0.0.1:<port>` and `KOSMOS_LINK_TOKEN`. `kl_options_from_env()` reads them. Connect to the numeric address `127.0.0.1`, never `localhost`.

## Permissions

The player decides. Nothing connects usefully until the player ticks the addon in **View > Native addons**. Until then every request is refused with a *denied* error, and the welcome message grants nothing. The capabilities are:

| Capability | Lets the program |
|---|---|
| `read` | get and subscribe to any property |
| `controls` | set `/controls/...` and its own `/addons/<id>/...` properties |
| `write` | set any property |
| `spawn` | spawn ships, release ordnance, give equipment |
| `draw` | add MFD pages and draw on them |
| `keys` | define key actions |

What you receive is what you asked for, narrowed by the manifest. Check `kl_granted()` and handle the `granted` event, because the player can change the choice while you run.

Be honest with players about what the program does. The link accepts connections from this computer only, but any program running as the same user could connect and claim an addon id. The token Kosmos supplies is a diagnostic, not a secret. Approval is by addon id, so only tick addons you trust.

## Reading and writing properties

Properties are the same tree as the F4 browser and the Lua `props` table; see the modders manual's Property reference. Ships publish under `/ships/ship[N]/`, and `/sim/active-ship` is the flown one.

```c
kl_subscribe(c, "/ships/ship[0]/altitude", 10);  /* at most 10 updates a second; 0 = every change */
kl_set_double(c, "/controls/throttle", 0.8);
kl_value v;
if (kl_get(c, "/sim/paused", &v) == KL_OK) { ... }
```

Subscription updates arrive in the handler you set with `kl_set_value_handler`, from inside `kl_poll()`. Call `kl_poll(c, timeout_ms)` regularly. Keep it quick: Kosmos does not wait for you.

`/controls/pitch`, `yaw` and `roll` are rewritten every frame from the player's keyboard and joystick, so a program cannot drive them without the player giving up the stick. Throttle, gear, lights, SAS and target attitudes keep what you set.

## Keys

```c
kl_define_key(c, "ping", "Say hello", "My addon", "letter_g", /*shift*/0, /*hold*/0);
kl_subscribe(c, "/addons/my-addon/keys/ping", 20);
```

Key names are simlib's (`letter_g`, `digit_3`, as in the modders manual's Key actions); an empty name leaves the action unbound. The action appears in View > Keyboard under your group, where the player can rebind it. Pressing it sets the property `/addons/<id>/keys/<action>`; a toggle key flips it and a hold key follows the key.

## Spawning, ordnance and equipment

```c
unsigned handle;
kl_spawn_ship(c, "Shuttle-A", 0, 0, -200, &handle); /* metres in the flown ship's frame: x right, y up, z forward */
kl_release(c, -1);                                    /* release from the flown ship's arsenal */
kl_give(c, "tricorder", 1);                           /* add equipment to the person's pack */
```

Names are those in the ship and equipment lists. A spawned ship copies the flown ship's velocity. There is no call to remove one again.

## MFD pages

```c
kl_define_mfd(c, "main", "MY PAGE");      /* once, after connecting; appears in the MFD mode list */
/* in the event handler, when name is "mfd" and arg is the page id: */
kl_draw_begin(c);
kl_draw_colour(c, 120, 255, 140);
kl_draw_text(c, 200, 30, "HELLO", 1);     /* align: 0 left, 1 centre, 2 right */
kl_draw_end(c, arg);
```

The page is 400 by 400 units. Kosmos sends an `mfd` event about 20 times a second while the page is on screen, so draw a complete frame each time. The page is a list of commands (line, rect, fillrect, circle, disc, tri, text, image), up to 2000 per frame. An image path is relative to your addon folder. If you stop drawing for a second the page shows NO SIGNAL.

## Events

`ship` (the flown ship changed; the argument is its name), `mfd`, `granted` (the new capability mask), `shutdown` (Kosmos is closing). Set them with `kl_set_event_handler`.

## Errors

Calls return `KL_OK` or a negative `KL_ERR_*` code, and `kl_error()` describes the last failure. Requests that expect an answer (get, ping, spawn, define) wait up to five seconds. Fire-and-forget calls (set, draw, subscribe, log) report failures to the handler from `kl_set_error_handler`.

Kosmos closes a connection that sends a malformed frame, or no hello within five seconds.

## C API reference

All functions are in `kosmos_link.h`. Functions that talk to Kosmos return `KL_OK` (0) or a negative `KL_ERR_*`. A `kl_client` is not thread safe: use it from one thread.

| Function | Purpose |
|---|---|
| `kl_options_from_env(&o)` | Fill host, port and token from `KOSMOS_LINK` and `KOSMOS_LINK_TOKEN`. Set `addon_id`, `name`, `version` and `capabilities` yourself. |
| `kl_connect(&o, err, size)` | Connect and say hello. Returns NULL and writes the reason to `err` on failure. |
| `kl_close(c)` | Disconnect and free the client. |
| `kl_granted(c)`, `kl_sim_version(c)`, `kl_error(c)` | Capabilities you hold, the Kosmos version, the last failure text. |
| `kl_set_value_handler`, `kl_set_event_handler`, `kl_set_error_handler` | Callbacks for subscription updates, events and refused fire-and-forget requests. |
| `kl_poll(c, ms)` | Wait up to `ms` for messages and run the handlers. Returns the number handled, or `KL_ERR_IO` once disconnected. |
| `kl_subscribe(c, path, hz)` / `kl_unsubscribe(c, path)` | Updates at most `hz` times a second (0 = every change). The current value arrives at once. At most 512 subscriptions. |
| `kl_get(c, path, &v)` | Read one property now. `v.type` is `KL_NONE` if it does not exist. |
| `kl_set`, `kl_set_bool`, `kl_set_int`, `kl_set_double`, `kl_set_string` | Write a property without waiting. |
| `kl_define_key(c, id, label, group, key, shift, hold)` | Add a rebindable key action; see Keys. |
| `kl_spawn_ship(c, name, x, y, z, &handle)` | Spawn a ship; `handle` is its index in `/ships/ship[N]`. |
| `kl_release(c, hardpoint)`, `kl_give(c, id, count)` | Release ordnance; give equipment. |
| `kl_define_mfd(c, id, title)` | Add an MFD page. |
| `kl_draw_begin`, `kl_draw_colour`, `kl_draw_line`, `_rect`, `_fillrect`, `_circle`, `_disc`, `_tri`, `_text`, `_image`, `kl_draw_end(c, id)` | Collect draw commands and send them as one page. |
| `kl_log(c, text)`, `kl_ping(c)` | Print to the Lua console window; round trip. |

A `kl_value` has a `type` (`KL_NONE`, `KL_BOOL`, `KL_INT`, `KL_DOUBLE`, `KL_STRING`) and the matching `boolean`, `integer`, `real` or `string` member. Strings are cut to 255 bytes.

Error codes: `KL_ERR_IO` (connection lost), `KL_ERR_TIMEOUT`, `KL_ERR_BAD_REQUEST`, `KL_ERR_UNKNOWN`, `KL_ERR_DENIED` (not allowed by the player or manifest), `KL_ERR_FAILED`, `KL_ERR_VERSION`, `KL_ERR_ARGUMENT`.

A minimal program:

```c
#include "kosmos_link.h"
#include <stdio.h>

int main(void)
{
    kl_options o;
    char err[256];
    kl_options_from_env(&o);
    o.addon_id = "hello-props";
    o.name = "Hello props";
    o.version = "1.0";
    o.capabilities = KL_CAP_READ;
    kl_client *c = kl_connect(&o, err, sizeof err);
    if (!c) { fprintf(stderr, "%s\n", err); return 1; }
    kl_value v;
    if (kl_get(c, "/ships/ship[0]/name", &v) == KL_OK)
        printf("flying %s\n", v.string);
    kl_close(c);
    return 0;
}
```

## C++ wrapper

`kosmos_link.hpp` is a header-only class, `kosmos::Link`. The constructor connects using the environment Kosmos sets and throws `std::runtime_error` on failure. Set `on_value` and `on_event` to `std::function` handlers, then call `poll()` in a loop.

| Member | Purpose |
|---|---|
| `Link(id, name, capabilities, version)` | Connect. |
| `on_value(path, value)`, `on_event(name, arg)` | Handlers. |
| `poll(timeout_ms)` | Returns false once the connection is gone. |
| `subscribe(path, hz)` | Subscribe. |
| `get(path, fallback)`, `get_string(path)` | Read a number or a string. |
| `set(path, double / bool / string)` | Write. |
| `log(text)`, `granted()`, `raw()` | `raw()` gives the `kl_client *` for keys, MFDs, spawning and drawing. |

## Python client

`examples/kosmos_link.py` is a single file with no dependencies. `Link(addon_id, name, capabilities, version, address, token)` reads `KOSMOS_LINK` and `KOSMOS_LINK_TOKEN` when the address is left out. It has `poll`, `get`, `set`, `subscribe`, `define_key`, `spawn_ship`, `release`, `give`, `define_mfd`, `draw` and `log`; assign `on_value` and `on_event` for callbacks. `speed_hold.py` is a small autopilot that holds 150 m/s through `/controls/throttle`.

## Useful properties

Browse the full tree with F4 in the game. Some that addons commonly use:

| Path | Meaning |
|---|---|
| `/sim/active-ship` | Index of the flown ship. |
| `/sim/paused`, `/sim/warp-index`, `/sim/target` | Pause, time warp step, selected target. |
| `/ships/ship[N]/name`, `speed`, `altitude` | Name, speed (m/s) and altitude of ship N. |
| `/ships/ship[N]/gear-position`, `landed`, `crashed` | Gear (0 up, 1 down), on the ground, destroyed. |
| `/ships/ship[N]/stage`, `stage-count` | Current stage and the number of stages. |
| `/controls/throttle`, `/controls/gear` | Flown ship controls you may set with the `controls` capability. |
| `/addons/<id>/...` | Your own properties. You can always write these with `controls`. |

## Troubleshooting

- **Every request fails with *denied*.** The player has not ticked your addon in View > Native addons, or the manifest's `capabilities` do not include what you asked for. Check `kl_granted()`.
- **Connect fails.** Kosmos is not running, the port differs (`--link-port`), or a program started by hand needs "Listen for programs started outside Kosmos". Use `127.0.0.1`, not `localhost`.
- **The connection drops after five seconds.** The first message must be *hello*.
- **The MFD page says NO SIGNAL.** Your program stopped drawing. Draw a full frame on every `mfd` event.
- **Throttle moves but pitch and roll do not.** Kosmos rewrites those every frame from the player's inputs.
- **My program is not started.** It needs a `run` entry for the platform, the file must exist inside the addon folder, and the addon must be ticked.
- **No game handy.** Run `python3 tools/kosmos_link_sim.py` for a mock Kosmos.

## Python and other languages

`examples/kosmos_link.py` is a complete client in one file, built on `docs/PROTOCOL.md`. Run `python3 tools/kosmos_link_sim.py` to get a mock Kosmos on port 9420 that grants everything, answers `get` and `set`, and prints what your program asks for.

## Limits

- Local TCP only. Android is not supported yet; the C API is portable, and the transport will be revisited then.
- There is no lockstep mode: your program sees the simulation slightly behind real time and cannot pause a frame for itself.
- Spawned ships cannot be removed individually.
- The protocol is version 1. Kosmos refuses a client that speaks a newer one.

## Releasing

Ship your addon folder with `bin/` for each platform you built. Players tick it in View > Native addons the first time. Do not ask players to disable their firewall: the link never leaves the computer.

## Appendix: the wire protocol (version 1)

Most addons should use the C library. This appendix is for writing a client in another language; the same text is in `docs/PROTOCOL.md`.

### Transport and frames

Kosmos listens on `127.0.0.1:9420` (loopback only, TCP). Connect to the numeric address, not `localhost`. When Kosmos starts your program it sets `KOSMOS_LINK=127.0.0.1:<port>` and `KOSMOS_LINK_TOKEN=<token>`. All integers are little-endian.

```
u32  length      bytes that follow this field (at least 6, at most 1 MiB)
u16  type
u32  request id  chosen by the client; replies carry the same id; pushed messages use 0
...  payload
```

A malformed frame, a frame larger than 1 MiB or a missing hello within 5 seconds closes the connection. Strings are `u16 byte-length` followed by UTF-8 bytes. A value is a `u8` tag (0 none, 1 bool, 2 int32, 3 double, 4 string) followed by `u8` / `i32` / `f64` / string.

### Client to Kosmos

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

Draw kinds: 0 line, 1 rect, 2 fillrect, 3 circle, 4 disc, 5 tri, 6 text, 7 image. Coordinates are in MFD units, 0-400 on both axes. At most 2000 commands per page.

### Kosmos to client

| Type | Name | Payload |
|---|---|---|
| 101 | welcome | `u16 protocol`, str Kosmos version, `u32 granted` |
| 102 | error | `u16 code`, str message |
| 103 | value | str path, value (reply to get, or a subscription update with request id 0) |
| 104 | event | str name, str argument |
| 105 | ok | `u32 handle` (the new ship's index for spawn_ship, otherwise 0) |
| 106 | pong | none |

Error codes: 1 bad request, 2 unknown, 3 denied, 4 failed, 5 version. Events: `ship`, `mfd` (sent at 20 Hz while the page is on screen), `granted`, `shutdown`. Capability bits: read 1, controls 2, write 4, spawn 8, draw 16, keys 32.

