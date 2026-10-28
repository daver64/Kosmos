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
| `examples/` | `hello_props.c`, `speed_mfd.cpp`, `weather_mfd.cpp` (a weather page), and a pure-Python client with `speed_hold.py` and `nearest_runway.py`. |
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
| `world` | change the weather, and place and remove ships (protocol 2) |
| `ui` | show windows and notices (protocol 3) |
| `vessel` | take over a ship's controls and apply forces to it (protocol 3) |
| `draw3d` | draw lines, markers and labels in the 3D view (protocol 3) |

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
kl_fire(c, -1, "primary", 1);                         /* pull the trigger of the flown ship's primary weapons */
```

`kl_fire` pulls or releases the trigger of a weapon group on a ship (`-1`: the flown ship). The groups are `primary` and `secondary`, or any name a ship's `weapons` block defines; see the modders manual, Ship weapons. A beam pulses once per pull and then recharges, a volley fires to the end, and a held trigger repeats. Players pull the same triggers with keypad 1 and 7, so a program can fight alongside them. Without `weapons` fitted the call returns an error.

Names are those in the ship and equipment lists. A spawned ship copies the flown ship's velocity. Ships your program spawned can be removed again with `kl_remove_ship` (see Ships and the world below).

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

`ship` (the flown ship changed; the argument is its name), `mfd`, `granted` (the new capability mask), `shutdown` (Kosmos is closing) always arrive. Set a handler with `kl_set_event_handler`.

Protocol 2 adds events you ask for with `kl_subscribe_event(c, name)` (needs `read`); until you do, Kosmos sends you none of them and does no work for them:

| Event | Argument | When |
|---|---|---|
| `pause` | `1` or `0` | the player pauses or resumes |
| `warp` | the new time-warp factor | the warp step changes |
| `scenario` | the scenario id | a scenario starts |
| `soi` | body name | the flown ship enters another body's sphere of influence |
| `crash` | ship name | the flown ship is destroyed |
| `touchdown` | ship name | the flown ship lands |
| `weather` | the METAR, if any | the weather at the flown ship changes noticeably |
| `ships` | the ship count | a ship is added or removed |
| `tick` | the Julian date | once a second |

```c
kl_subscribe_event(c, "tick");
/* in the event handler: if (strcmp(name, "tick") == 0) { re-read what you need } */
```

## Weather

```c
kl_weather w;
if (kl_weather_at(c, "", 51.5, -0.1, 300.0, &w) == KL_OK)   /* body "" = the flown ship's; metres above the ground */
    printf("wind %03.0f at %.0f m/s, visibility %.0f m\n", w.wind_from, w.wind_speed, w.visibility);
kl_set_weather(c, "preset", "storm");                        /* needs the world capability */
kl_set_weather(c, "metar", "EGLL 011200Z 27012KT 9999 FEW040 15/09 Q1013");
kl_set_weather(c, "auto", "");                               /* back to the simulated weather */
```

`kl_weather_at` returns the weather the flown ship would feel at any place (wind grows with height, and there is none above the atmosphere). It fails for a body without an atmosphere. `kl_weather` holds wind east and north, speed, the direction it blows from, gust, rain, thunder, cloud cover (0 to 1), cloud base and top, visibility (m), temperature (C) and pressure (hPa); `station` and `metar` are filled in only for the flown ship's own body. Setting the weather overrides it everywhere until the player picks something else in the weather settings; presets are `clear`, `scattered`, `overcast`, `rain`, `fog` and `storm`.

## Ships and the world

```c
kl_ship_info ships[32];
int n;
kl_list_ships(c, 5000.0, 0, ships, 32, &n);                  /* within 5 km of the flown ship; 0 = all */
kl_place_ship(c, ships[0].index, "Earth", 64.0, -22.0, 2000.0, 90.0, 120.0); /* lat, lon, height m, heading, speed m/s; height 0 = on the ground */
kl_remove_ship(c, handle);                                    /* only ships a link program spawned */
```

`kl_ship_info` gives the ship's index, name, body, latitude, longitude, altitude, heading, speed, vertical speed, distance from the flown ship and flags (`KL_SHIP_FLOWN`, `KL_SHIP_LANDED`, `KL_SHIP_CRASHED`, `KL_SHIP_FROM_LINK`). Indices are positions in the ship list: removing a ship moves every later ship down one place, so list again afterwards. The flown ship cannot be removed, but it can be placed. In multiplayer, other players do not see a placement.

## Facilities

```c
kl_facility airports[5];
int n;
kl_list_facilities(c, "airport", "", lat, lon, 300.0, airports, 5, &n);   /* within 300 km, nearest first */
for (int i = 0; i < n; ++i)
    printf("%s %s %.0f km, %d runway(s)\n", airports[i].icao, airports[i].name, airports[i].distance / 1000, airports[i].runway_count);
```

`kind` is `airport` (bases with runways), `base` (without) or empty for both. Each `kl_facility` has the id, name, ICAO code, body, position, elevation, heading, distance and up to eight runways (name, centre, heading, length, width, ILS frequencies) and sixteen navaids (type, id, name, position, frequency, range). Positions of runways and navaids are worked out from the base's frame, so they are accurate to a few metres.

## Windows, lockstep, vessels and 3D (protocol 3)

These need protocol 3. All of the calls below are in `kosmos_link.h` (C) and `kosmos_link.py`; the C++ wrapper has the plain calls and `raw()` for the builders. Examples: `ui_panel.py`, `lockstep_hold.py`, `waypoints3d.py`, `vessel_hover.py`.

**Windows (`ui`).** Build a window with `kl_ui_begin`, add `kl_ui_label/button/checkbox/slider/text/separator`, then `kl_ui_show`. Kosmos draws it as an ordinary window. Clicks and edits arrive as a `ui` event whose argument is `window widget value`; `kl_parse_ui_event` splits it. Closing the window sends the event with an empty widget and the value `closed`. `kl_ui_close` removes a window, and `kl_ui_notify` shows a short notice. Windows are removed when the program disconnects. Limits: 8 windows per program, 64 widgets per window.

**Lockstep (`controls`).** `kl_lockstep(c, 1, max_wait_ms, fixed_dt)` makes the simulation wait for you each step. Kosmos sends a `step` event (`frame dt simtime`), you read, decide and set controls, then call `kl_step_done(frame)`. If you do not answer within `max_wait_ms` the step goes ahead without you and the miss is counted. `fixed_dt` of 0 keeps the game's step; otherwise 0.001 to 0.1 s. Time warp is held at 1x while lockstep is on, and the game does not wait while paused. Only one program at a time may hold lockstep, and it is released when the program disconnects.

**Vessels (`vessel`).** `kl_claim_ship(c, index, passthrough)` makes a program own a ship's behaviour. Its controls are then read from `/ships/ship[N]/module/<addon>/controls/...`, which you write with `kl_ship_value(c, index, "controls/throttle", 1.0)`. Other names are published under `/ships/ship[N]/module/<addon>/`. `kl_ship_force` applies a force and torque (ship axes as above, newtons and newton metres) every step until you change it, clamped to the ship's limits. The flown ship can be claimed only if the player turns on *Allow claiming the flown ship* in View > Native addons; with `passthrough` the player keeps flying it. A ship spec may name its module in an optional block, `"module": {"addon": "id", "max_force": N, "max_torque": N}`. The block only records the addon and the limits: the game does not launch the program. Claims end on disconnect.

**3D drawing (`draw3d`).** `kl_3d_begin(layer)`, then `kl_3d_line/marker/label/ring/sphere` and `kl_3d_end` send a named layer of up to 2000 primitives that replaces the previous layer of that name. Positions are in one of three frames: `KL_FRAME_LLA` (latitude, longitude in degrees, altitude in metres above the mean radius of the body you are near), `KL_FRAME_BODY` (metres in the body-fixed frame) or `KL_FRAME_SHIP` (metres in a ship's own axes, around its centre of mass: x right, y up, and z aft, so forward is -z, as in `ship.json`). With depth on (the default), a primitive is hidden when the planet or a ship within 20 km is in front of it. Ships count as bounding spheres sized from their stages, so this is approximate; scenery (terrain relief, buildings, trees) does not hide anything. Pass depth off to draw over everything. An empty layer removes it. Up to 8 layers per program. Kosmos draws them with simlib's line, circle and text calls, so this is a way to mark things, not a renderer.

**Settings.** *Ping timeout* (`/sim/settings/link-timeout`, default 15 s) in View > Native addons: a protocol 3 program that is silent for that long is pinged, and dropped if it does not answer within as long again. The SDK answers pings itself. Programs speaking protocol 1 or 2 are never timed out.

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
| `kl_fire(c, ship, group, on)` | Pull or release a ship weapon trigger (`-1`: the flown ship). C++ `fire()`, Python `fire()`. Needs `spawn`. |
| `kl_define_mfd(c, id, title)` | Add an MFD page. |
| `kl_draw_begin`, `kl_draw_colour`, `kl_draw_line`, `_rect`, `_fillrect`, `_circle`, `_disc`, `_tri`, `_text`, `_image`, `kl_draw_end(c, id)` | Collect draw commands and send them as one page. |
| `kl_weather_at(c, body, lat, lon, alt, &w)` | Weather at a place; see Weather. |
| `kl_set_weather(c, mode, arg)` | `metar`, `preset` or `auto`. Needs `world`. |
| `kl_subscribe_event(c, name)` / `kl_unsubscribe_event(c, name)` | Opt in to or out of `pause`, `warp`, `scenario`, `soi`, `crash`, `touchdown`, `weather`, `ships`, `tick`. |
| `kl_list_ships(c, radius, buildings, out, max, &n)` | Ships near the flown one. |
| `kl_remove_ship(c, index)` / `kl_place_ship(c, index, body, lat, lon, alt, heading, speed)` | Remove a link-spawned ship; move any ship. Need `world`. |
| `kl_list_facilities(c, kind, body, lat, lon, radius_km, out, max, &n)` | Airports and bases near a point. |
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
| `weather_at(lat, lon, alt, w, body)`, `set_weather(mode, arg)`, `subscribe_event(name)` | Weather and events. |
| `ships(radius, buildings)`, `facilities(lat, lon, radius_km, kind, max, body)` | Return `std::vector`s of `kl_ship_info` and `kl_facility`. |
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
| `/ships/ship[N]/shield`, `health` | Shield level and hull health, 0 to 1 (`shield` is 0 on ships without one). |
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
- Without lockstep your program sees the simulation slightly behind real time; use lockstep when a controller must answer every step.
- 3D drawing hides behind the planet and ship bounding spheres only, not scenery, and the ship `module` block does not launch your program.
- Only ships a link program spawned can be removed. Weather can be read anywhere but set only as one global override.
- The protocol is version 3. Kosmos accepts versions 1 to 3 and refuses a client that speaks a newer one.

## Releasing

Ship your addon folder with `bin/` for each platform you built. Players tick it in View > Native addons the first time. Do not ask players to disable their firewall: the link never leaves the computer.

## Appendix: the wire protocol (version 3)

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
| 107 | weather | `f64` wind east, wind north, wind speed, wind from, gust, rain, thunder, cloud cover, cloud base, cloud top, visibility, temperature, pressure; str station; str METAR |
| 108 | ship_list | `u32 count`, then per ship: `u32 index`, str name, str body, `f64` latitude, longitude, altitude, heading, speed, vertical speed, distance, `u8 flags` (1 flown, 2 landed, 4 crashed, 8 spawned by a link) |
| 109 | facility_list | `u32 count`, then per base: str id, name, icao, body; `f64` latitude, longitude, elevation, heading, distance; `u16 runways` each: str name, `f64` latitude, longitude, heading, length, width, `u8 ils` then `f64` frequencies; `u16 navaids` each: str type, id, name, `f64` latitude, longitude, frequency, range |

Error codes: 1 bad request, 2 unknown, 3 denied, 4 failed, 5 version. Events: `ship`, `mfd` (sent at 20 Hz while the page is on screen), `granted`, `shutdown`. Capability bits: read 1, controls 2, write 4, spawn 8, draw 16, keys 32, world 64 (change the weather, place and remove ships), ui 128, vessel 256, draw3d 512.

Protocol 2 adds requests 14 to 21 and replies 107 to 109. Kosmos accepts a hello carrying 1 or 2, and a version 1 client simply never sends the new requests. An older Kosmos (protocol 1) refuses a version 2 hello with error 5; retry with 1 if you want to support it. Weather values are SI units except temperature (Celsius) and pressure (hPa); wind east and north are the way the air moves, wind from is the compass direction it blows from. The weather the game shows is a global override while `set_weather` is in force: it lasts until the player (or another addon) changes it, and `auto` returns to simulated weather. `remove_ship` works only on ships link addons spawned and never on the flown ship; the ships after it move down one place, so list again afterwards. `place_ship` may move any ship including the flown one (in multiplayer the other players will not follow). List replies are capped (2000 ships, 200 facilities) to stay under the frame limit.

Opt-in events, sent only after `subscribe_event`: `pause` (argument 1 or 0), `warp` (the new factor), `scenario` (the scenario id, when one starts), `soi` (the flown ship entered a new body's sphere of influence; the name), `crash` and `touchdown` (the flown ship's name), `weather` (conditions changed noticeably; the METAR if any), `ships` (the number of ships changed), `tick` (once a second; the Julian date). Always-on events are unchanged.

### Protocol 3

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
| 32 | fire | `u32 ship` (`0xFFFFFFFF`: the flown ship), str group (`primary`, `secondary` or a group the ship defines), `u8 on`. Pulls or releases a weapon trigger. Needs `spawn` |

New events: `step` (to the lockstep program only; argument `frame dt simtime`) and `ui` (to the window's owner; argument `window widget value`; a closed window sends an empty widget and `closed`). Kosmos sends a `ping` frame (type 13) to a protocol 3 client that has been silent for the timeout; any frame from the client, such as a `ping` back, resets the timer, and a client silent for twice the timeout is dropped. Protocol 1 and 2 clients are not timed out. Limits: 8 windows, 64 widgets, 8 layers and 2000 primitives per client. A program's windows, layers, claims and lockstep end when it disconnects.
