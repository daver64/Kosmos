# KosmosMap

A moving map for Kosmos, in the spirit of FlightGear's Atlas. It is an ordinary GTK 3 program written in C++ that talks to the game over KosmosLink; it does not use simlib. It runs on Linux and Windows. This file has two parts: [using it](#using-kosmosmap) for players, and [how it is put together](#how-it-is-put-together) for anyone who wants to modify it or write a program like it.

## Using KosmosMap

### Starting it
1. In Kosmos open **View > Native addons** and tick **KosmosMap**. It only needs the read capability: it cannot move or control anything.
2. Press **Restart** if it has not opened a window. The map opens beside the game and follows the flown ship.
3. Untick the addon to close it.

It is not started automatically. The map uses installed Orbiter scenery in `import/orbiter` or the bundled Orbiter defaults. Without elevation data it shows imagery unshaded.

### What you see
- **The ground** of the selected body, as a flat map. While following, the map switches body by itself when the flown ship changes sphere of influence, for example on arrival at the Moon.
- **Body selector**: choose a body in the current system to browse it. This turns Follow off; select Follow to return to the flown ship and resume automatic body changes. On its own the selector lists the solar system from `data/sol.json`. When connected, it lists the current system's bodies; the list may take a little time to populate.
- **Relief** (the default) shades Orbiter's elevation data with a hillshade. Earth uses hypsometric colours, with sea in blue and land running from green through brown to snow; the Moon is grey and Mars rust. **Imagery** (the drop-down in the header bar) uses Orbiter's surface tiles at the appropriate zoom, with the same relief shading. Missing tiles fall back to a coarser tile or the global picture. Detail depends on the installed scenery.
- **Your ship** is the large yellow aircraft symbol pointing along its heading; **other ships** on the same body are smaller and blue, and get a name label once you zoom in (about 50 pixels per degree).
- **Your track** is drawn behind the ship. It is forgotten if the ship jumps more than 5 degrees, and old points are dropped after 6,000.
- **Airports and bases** are dots (blue for airports, yellow for other facilities) with their ICAO code and name. Zoomed in you also see the runways to scale and navaids (pink, with their identifiers).
- **Graticule and scale bar** show latitude, longitude and distance.
- **The panel at top left** gives the body and mode, then the flown ship's name, position (N/S, E/W), altitude in metres, heading and speed in knots. A line below it shows the position, and the ground height in metres, under the mouse pointer. "Drawing..." appears while a new picture is being made.
- **The status line** at the bottom right says where the data came from (for example `Orbiter elevation, image earth.jpg`).

### Controls
| Control | Action |
|---|---|
| Drag with the left button | Pan (turns Follow off) |
| Mouse wheel | Zoom about the pointer |
| `+` / `-` (also the keypad) or the header buttons | Zoom in and out about the centre |
| `F` or the **Follow** button | Keep the map centred on the flown ship |
| `C` or **Clear track** | Forget the track |
| **Body** drop-down | Browse a different body; Follow returns to the ship |
| **Relief / Imagery** drop-down | Choose the map style |

Zoom runs from the whole body down to about two metres a pixel.

### Without the game
KosmosMap also works as a plain map browser:

```
kosmosmap --body Moon --lat 10 --lon 20 --ppd 12
```

| Option | Meaning |
|---|---|
| `--body NAME` | Show this body (default Earth when there is no game) |
| `--lat DEG`, `--lon DEG` | Centre of the view |
| `--ppd N` | Zoom, in pixels per degree of latitude |
| `--imagery` | Start with the surface picture |
| `--root DIR` | The Kosmos folder (the one holding `data/`). It is otherwise found from `KOSMOS_DATA`, by walking up from the program, or from the current folder |
| `--shot FILE.png [--size WxH]` | Write one picture (default 1100x720) and exit; no window is opened |
| `--no-link` | Do not try to connect to Kosmos |

### Troubleshooting
- **No window** after ticking it: View > Native addons shows whether the program is connected. The program must have been built (see below); `addons/kosmosmap/bin/kosmosmap` (or `kosmosmap.exe` on Windows) has to exist.
- **"Waiting for the flown ship"**: Kosmos is connected but no ship is flown yet (for example while a scenario loads).
- **Flat grey or no relief**: the Orbiter elevation data for that body is missing; the status line says what was found.
- **"cannot find the Kosmos data folder"**: pass `--root DIR` or set `KOSMOS_DATA`.

## How it is put together

### Building
KosmosMap is part of the SDK (`sdk/kosmosmap`). The game's `cmake --build build` builds it on Linux when GTK 3 and zlib are installed, writing `addons/kosmosmap/bin/kosmosmap`; the Windows tools build supplies `kosmosmap.exe`. The manifest `addons/kosmosmap/addon.json` launches it with capability `read`, no autostart and no restart. Elsewhere, build the SDK folder with CMake and copy `addon.json` and the program into an addon folder. Building the SDK source also requires nlohmann's single-header `json.hpp` for the offline body list; set `KOSMOSMAP_JSON_INCLUDE` to its directory if CMake cannot find it.

### Files
| File | Role |
|---|---|
| `src/main.cpp` (about 930 lines) | The program: state, KosmosLink polling, drawing, input and start-up |
| `src/terrain.{h,cpp}` | Reads Kosmos's elevation and picture data. No windows and no link (it uses gdk-pixbuf only to decode pictures) |
| `src/render.{h,cpp}` | Turns a view of the ground into pixels. No windows, no link |
| `src/body_catalog.{h,cpp}` | Reads the standalone solar-system body list and radii |
| `CMakeLists.txt` | Finds GTK 3, gdk-pixbuf and zlib, links the SDK's `kosmos::link` client library, sets the output folder |
| `addon.json` | The addon manifest (copied to `addons/kosmosmap` for the game) |

The split is deliberate: `terrain` and `render` know nothing about the game or the window (they need gdk-pixbuf and zlib, not the GTK widgets), so `--shot` renders with them alone and they can be reused in a different front end.

### Data flow
```
Kosmos ---KosmosLink (loopback)---> on_tick (every 200 ms) ---> App state ---> paint() ---> GTK drawing area
                                                                   |
                          view change ---> start_render (worker thread) ---> kmap::render ---> cached surface
```

1. **Link.** `connect_link` makes a `kosmos::Link("kosmosmap", "KosmosMap", KL_CAP_READ, "0.1")`. If the game is not there it carries on as a browser. `on_tick` runs every 200 ms (`g_timeout_add`): it calls `link->poll(0)` (and quits if Kosmos has gone), then `link->ships(0.0, false)` for every ship with its body, latitude, longitude, altitude, heading, speed and flown, landed and crashed flags. The flown ship is stored separately, the others kept only if they are on the same body.
2. **Facilities.** `update_facilities` asks `link->facilities(lat, lon, 0, "", 200, "")` for up to 200 airports and bases near the view centre, at most every two seconds and only if the view moved enough. Runways and navaids come with them.
3. **Body changes.** While following, when the flown ship's body differs from the loaded ground, `set_ground` loads the new body's data, the view is recentred and zoomed, and a render is requested. Manual selection pauses this until Follow is re-enabled.
4. **Track.** Points are appended when the ship has moved a little; the list is capped.

The body selector loads the current system's names, parents and radii over KosmosLink in batches. The solar-system file supplies the standalone list and an initial list while the connected solar-system catalogue loads. Manual selection stops Follow; re-enabling Follow returns to the flown ship.

### Ground data (`terrain.cpp`)
- `Tree` reads Orbiter's planetary quadtree archives (`.tree`): a header, an array of nodes holding file position, size and four child indices, and zlib-compressed tiles. It is guarded by a mutex because worker threads read it.
- `Elevation` decodes the `Elev` tiles into `Tile`s (a 257 x 257 grid of 16-bit samples plus a one sample border, with a scale and offset, or a flat tile) and caches them by node. `height(lat, lon, metres_per_pixel, radius, cursor)` picks the quadtree level that suits the zoom and samples it bilinearly. Each thread passes its own `Cursor` (last node and tile) so most lookups skip the shared cache.
- `Surface` selects and caches `Surf` tiles by zoom, reads their compressed DDS colour blocks with the shared SDK decoder, and samples them bilinearly. Missing or unreadable tiles fall back to an ancestor and then the global picture.
- `Picture` loads an equirectangular JPEG or PNG (north at the top, longitude -180 at the left) with gdk-pixbuf and gives `colour(lat, lon)`.
- `Ground::load(root, body, radius)` looks for `Elev.tree` and `Surf.tree` in each body's `Archive` directory under `import/orbiter`, `import/orbiter/MinorBodies`, and the bundled `data/orbiter-defaults` scenery. It also looks for a global picture in `data/textures/planets/`, `data/textures/`, `import/celestia/medres/` and `import/celestia/lores/`, and fills `source`, the status text.
- `find_root` finds the Kosmos folder: the `--root` hint, then `KOSMOS_DATA`, then by walking up from the program and from the working directory (a folder is the Kosmos folder if it holds `data/`).

### Rendering (`render.cpp`)
A `View` is a plate carree centred on a latitude and longitude, with `ppd` pixels per degree of latitude; east is scaled by the cosine of the centre latitude so the map looks right locally. `to_geo` and `to_pixel` convert, taking longitudes the short way round so a view across the date line still lines up.

`render(ground, view, current, generation)` fills a w x h ARGB32 buffer. It samples heights on a grid with a one pixel border (so slopes are right at the edges), colours them (`earth_colour` for Earth, a tint across the height range for other bodies, or imagery), and applies a hillshade whose slope is exaggerated as you zoom out so relief still shows when a pixel is kilometres wide. Rows are split across up to four threads (`std::thread`). It checks `current` against `generation` as it goes and returns an empty vector if a newer render has been requested, so panning and zooming never wait for stale work.

### Drawing and threads (`main.cpp`)
- `App` holds all state: the link, the shared `Ground`, the `View`, a `Rendered` cache (a cairo surface plus the view it was drawn for), the flown ship, others, facilities, track and flags. There are no globals.
- The cached picture is rendered **larger than the window** (`margin_factor` 1.4), so small pans only move the cached image and need no new render. `cache_stale` decides when a new one is needed; `request_render(delay)` debounces with a timer.
- `start_render` bumps `generation`, copies the shared pointers into a `Job` and renders on a worker thread. `finish_job` is queued back to the GTK main thread, which discards the result if the generation no longer matches and otherwise turns the pixels into the new cairo surface. **GTK and cairo are only touched on the main thread**; worker threads only touch `Ground` (immutable once loaded, shared through `shared_ptr`) and their own buffers.
- `paint` draws, in order: the cached picture offset to the current view, the graticule, facilities and runways (`draw_facilities`), the track, other ships, the flown ship, the scale bar and the info panel. `write_shot` renders synchronously and paints into an image surface for `--shot`.
- Input: `on_press`, `on_release`, `on_motion` (pan and cursor read-out), `on_scroll` (zoom about the pointer, `zoom_to`) and `on_key` (F, C, +, -). The header-bar buttons use `on_follow`, `on_mode`, `on_zoom_in`, `on_zoom_out` and `on_clear`.

### Extending it
- **A new layer** (for example weather, or a flight plan): add the data to `App`, fill it in `on_tick`, and draw it in `paint` before the ships. Remember to add the capability it needs to both manifests; this one only asks for `read`.
- **A new colour scheme**: add a function like `earth_colour` or `world_colour` in `render.cpp` and select it in the colouring step. Use `Ground::name` to pick it per body.
- **A new body**: it needs `Elev.tree` and/or `Surf.tree` in its Orbiter `Archive` folder, or a picture named after the body in one of the texture folders; no code changes.
- **A different front end** (another toolkit, a web page): reuse `terrain` and `render` as they are, and write the equivalent of `on_tick` and `paint`.
- **Another language**: see the SDK manual and `docs/PROTOCOL.md`; the calls used here are `ships` and `facilities` plus `poll`.

Keep the render path free of GTK and of the link, and keep GTK calls on the main thread.
