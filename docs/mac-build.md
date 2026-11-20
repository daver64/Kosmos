# macOS Build Guide (Draft)

Experimental source-build notes for macOS. These are not a released macOS package; results and tool support may vary by machine.

---

## Homebrew packages required

```sh
brew install cmake ninja pkgconf molten-vk vulkan-loader glslang \
    sdl2 sdl2_image sdl2_mixer sdl2_ttf \
    ffmpeg libsndfile portaudio lua \
    zlib libpng sqlite openexr openjpeg webp boost openssl@3
```

Homebrew resolves transitive dependencies. For the optional GTK tools, install `gtk+3` and `libepoxy` as well; the optional launcher needs `wxwidgets`. The exact requirements depend on the targets and renderer enabled.

**Key dependencies:**

| Category | Packages | Purpose |
|----------|----------|---------|
| Build system | `cmake`, `ninja` | CMake + Ninja build backend |
| Vulkan/MoltenVK | `molten-vk`, `vulkan-loader`, `glslang` | Vulkan rendering on macOS (MoltenVK translates Vulkan to Metal) |
| SDL2 | `sdl2`, `sdl2_image`, `sdl2_mixer`, `sdl2_ttf` | Windowing, audio, image loading, fonts |
| Media | `ffmpeg`, `libsndfile`, `portaudio` | Video playback, audio |
| Images | `libpng`, `openexr`, `openjpeg`, `webp` | Image format support |
| Misc | `zlib`, `sqlite`, `pkgconf`, `boost`, `openssl@3` | Compression, database, package discovery and networking dependencies |

## Building simlib first

Kosmos depends on [simlib](https://github.com/daver64/rsimlib). Check out the commit recorded in Kosmos's `SIMLIB_COMMIT` before building it. The example installs into `~/opt` rather than a system directory:

```sh
cd ~/Documents/Projects/rsimlib   # or wherever your simlib checkout lives
cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$HOME/opt" \
    -DCMAKE_PREFIX_PATH="$(brew --prefix)" \
    '-DSIMLIB_RENDERERS=gl;vulkan' \
    -DSIMLIB_VALIDATE_SHADERS=ON
cmake --build build -j4
cmake --install build
```

The quoted renderer list enables OpenGL and Vulkan; shader validation must remain enabled to generate Vulkan shaders. Vulkan uses MoltenVK, which the Vulkan loader must be able to find. If CMake cannot find dependencies, check their Homebrew prefixes and loader configuration.

## Building Kosmos

```sh
cd ~/src/projects/Kosmos          # or wherever your Kosmos checkout lives
cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$HOME/opt;$(brew --prefix)"
cmake --build build -j4
```

Run from the `build/` directory:

```sh
cd build
./kosmos --ship "CVEL Deltaglider" --orbit
```

Useful flags: `--cockpit`, `--cockpit3d`, `--hide-info`, `--system N`, `--set /path=VALUE`.

## SDK tools on macOS

| Tool | Runs? | Notes |
|------|-------|-------|
| `kosmosmap` | Reported working | GTK tool; requires GTK 3 and zlib |
| `planetbake` | Reported working | CLI tool |
| `kosmoscad`, `kosmosmission` | Limited | GTK GLArea windows have been reported blank on macOS |
| `kosmosav` | Depends | Requires GTK, libcurl and the KosmosAV network service |

## Known issues

### GTK+GLArea blank windows (kosmoscad, kosmosmission)

GTK3 `GtkGLArea` windows have been reported blank with Homebrew's macOS Cocoa backend. Workarounds being considered:
- Environment variable to force non-GL mode
- Accept compile-only validation for macOS SDK tools
- Replace GTK GLArea with SDL2/GLFW for 3D viewports

### Vulkan on Apple Silicon

Vulkan uses MoltenVK's Vulkan-to-Metal translation. Loader and ICD configuration can vary with the installation and architecture.

---

*This is a draft. Will be refined with exact versions, troubleshooting steps, and CI/CD notes.*
