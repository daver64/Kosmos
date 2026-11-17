# macOS Build Guide (Draft)

Quick reference for building Kosmos on macOS. Will be refined later.

---

## Homebrew packages required

```sh
brew install cmake ninja molten-vk vulkan-loader glslang \
    sdl2 sdl2_image sdl2_mixer sdl2_ttf \
    ffmpeg libsndfile portaudio lua \
    zlib png openexr openjpeg webp boost openssl@3
```

That's it — Homebrew resolves transitive dependencies automatically. `giflib` and `libpng` are pulled in by `openjpeg`/`webp`; `xz`, `zstd`, `lz4` come via `boost`/`ffmpeg`. The only standalone packages you must list explicitly are `zlib` and `png` (not a dependency of anything else).

**Key dependencies:**

| Category | Packages | Purpose |
|----------|----------|---------|
| Build system | `cmake`, `ninja` | CMake + Ninja build backend |
| Vulkan/MoltenVK | `molten-vk`, `vulkan-loader`, `glslang` | Vulkan rendering on macOS (MoltenVK translates Vulkan to Metal) |
| SDL2 | `sdl2`, `sdl2_image`, `sdl2_mixer`, `sdl2_ttf` | Windowing, audio, image loading, fonts |
| Media | `ffmpeg`, `libsndfile`, `portaudio` | Video playback, audio |
| Images | `png`, `openexr`, `openjpeg`, `webp` | Image format support (transitive deps resolved automatically) |
| Misc | `zlib`, `boost`, `openssl@3` | Compression, networking deps |

## Building simlib first

Kosmos depends on [simlib](https://github.com/kerbal-x/simlib). If you haven't built it yet:

```sh
cd ~/Documents/Projects/rsimlib   # or wherever your simlib checkout lives
mkdir -p build && cd build
cmake .. -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DSIMLIB_RENDERERS=gl;vulkan \
    -DSIMLIB_VALIDATE_SHADERS=OFF \
    -DSIMLIB_BUILD_SHADER_COMPILER=ON
ninja -j4
ninja install   # installs to /opt/homebrew (default Homebrew prefix)
```

**Note:** `SIMLIB_RENDERERS=gl;vulkan` enables both OpenGL fallback and Vulkan. On macOS, Vulkan uses MoltenVK under the hood — no extra config needed. The ICD JSON at `/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json` is installed automatically by the `molten-vk` Homebrew formula.

## Building Kosmos

```sh
cd ~/src/projects/Kosmos          # your Kosmos checkout
mkdir -p build && cd build
cmake .. -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DSIMLIB_DIR=/opt/homebrew/lib/cmake/simlib  # if simlib isn't in the default CMake path
ninja -j4
```

Run from the `build/` directory:

```sh
cd build
./kosmos --ship Deltaglider --orbit "Low Earth Orbit"
```

Useful flags: `--cockpit`, `--cockpit3d`, `--hide-info`, `--system N`, `--set /path=VALUE`.

## SDK tools on macOS

| Tool | Runs? | Notes |
|------|-------|-------|
| `kosmosmap` | ✅ Yes | Plain GTK, no OpenGL dependency |
| `planetbake` | ✅ Yes | CLI tool — verified end-to-end |
| `shipedit` | ⚠️ Partial | Compiles fine; window display fails (see known issues). **`--shot` mode works** for batch rendering. |
| `kosmoscad` | ❌ No | GTK+GLArea doesn't work on macOS Cocoa |
| `kosmosmission` | ❌ No | GTK+GLArea doesn't work on macOS Cocoa |
| `kosmosav` | ⚠️ Depends | Compiles; needs KosmosAV network service |

## Known issues

### GTK+GLArea blank windows (shipedit, kosmoscad, kosmosmission)

GTK3's `GtkGLArea` widget has known issues with the macOS Cocoa backend via Homebrew's `gtk+3`. Windows open but are completely blank. The rendering code itself works — `shipedit --shot` produces valid PNGs using the same OpenGL/Cairo pipeline. Workarounds being considered:
- Environment variable to force non-GL mode
- Accept compile-only validation for macOS SDK tools
- Replace GTK GLArea with SDL2/GLFW for 3D viewports

### Vulkan on Apple Silicon

MoltenVK's `is_portability_driver` flag in the ICD config (`/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json`) enables automatic Vulkan-to-Metal translation for Apple Silicon. No additional environment variables or configuration needed — the Vulkan loader finds MoltenVK automatically via the ICD mechanism.

---

*This is a draft. Will be refined with exact versions, troubleshooting steps, and CI/CD notes.*
