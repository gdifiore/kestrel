# ![icon](assets/kestrel-32.png) Kestrel

Interactive regex filter and text file viewer designed for log files.

![demo](repo/demo.gif)

## Features

- Live regex filter (Hyperscan/Vectorscan) with PCRE2 capture-group highlighting
- Match counter (X of Y), next/prev match (`n` / `Shift+N`)
- Filter view: show only matched lines
- Time-range filter for leading ISO-8601 timestamps (including UTC offsets)
- Log-level auto-tint (ERROR / WARN / INFO / DEBUG)
- Synthetic minimap with click-to-jump and viewport indicator
- Light/dark theme; configurable match/cursor colors
- Line-range selection: click + Shift+click (or Shift+arrows / PgUp / PgDn) → `Ctrl+C` to copy
- Go-to-line (`Ctrl+G`), copy/paste search pattern (`Ctrl+C` / `Ctrl+V`)
- Recent-files list, drag-and-drop file open

In-app help: `Help → Keyboard Shortcuts`.

## System dependencies

Debian / Ubuntu / Mint:

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config ragel libboost-dev libsqlite3-dev libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
```

## Submodules

```bash
git submodule update --init --recursive
```

Third-party sources live under `third_party/`:

- `imgui/`           — ocornut/imgui
- `ImGuiFileDialog/` — aiekick/ImGuiFileDialog
- `glfw/`            — glfw/glfw
- `vectorscan/`      — VectorCamp/vectorscan
- `pcre2/`           — PCRE2Project/pcre2
- `doctest/`         — doctest/doctest (vendored header)

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

The first configure downloads spdlog and JetBrains Mono. To install outside
the source checkout (including the icon and font assets):

```bash
cmake --install build
```

## Run

```bash
./build/kestrel
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

For an AddressSanitizer build, configure with `-DKESTREL_ENABLE_ASAN=ON`.

## Releases

Pushing a semantic-version tag (for example, `v1.2.3`) builds, tests, and
publishes Linux and Apple Silicon macOS release archives. Each archive contains
the files that belong under `/usr/local`, including the executable, font, and
icon assets.

```bash
git tag v1.2.3
git push origin v1.2.3
```

To install a downloaded release archive:

```bash
tar -xzf kestrel-<platform>-<version>.tar.gz -C /tmp/kestrel
sudo cp -a /tmp/kestrel/. /usr/local/
kestrel
```

## Bug reports

Logs go to stderr. Capture with debug verbosity and attach to the issue:

```bash
KESTREL_LOG=debug ./build/kestrel 2> kestrel.log
```

Valid `KESTREL_LOG` values: `trace`, `debug`, `info`, `warn` (default), `err`, `off`.

##### Icon Credit (modified by me)
<a href="https://www.flaticon.com/free-icons/american-kestrel-bird" title="american kestrel bird icons">American kestrel bird icons created by Delwar018 - Flaticon</a>
