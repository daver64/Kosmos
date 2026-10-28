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

## Python and other languages

`examples/kosmos_link.py` is a complete client in one file, built on `docs/PROTOCOL.md`. Run `python3 tools/kosmos_link_sim.py` to get a mock Kosmos on port 9420 that grants everything, answers `get` and `set`, and prints what your program asks for.

## Limits

- Local TCP only. Android is not supported yet; the C API is portable, and the transport will be revisited then.
- There is no lockstep mode: your program sees the simulation slightly behind real time and cannot pause a frame for itself.
- Spawned ships cannot be removed individually.
- The protocol is version 1. Kosmos refuses a client that speaks a newer one.

## Releasing

Ship your addon folder with `bin/` for each platform you built. Players tick it in View > Native addons the first time. Do not ask players to disable their firewall: the link never leaves the computer.
