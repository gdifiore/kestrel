# Kestrel

Fast regex filter for logs. C++20, [vectorscan](https://github.com/VectorCamp/vectorscan) for regex, [Dear ImGui](https://github.com/ocornut/imgui) + [GLFW](https://github.com/glfw/glfw) for UI.

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

- `imgui/`      — ocornut/imgui
- `glfw/`       — glfw/glfw
- `vectorscan/` — VectorCamp/vectorscan

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

**Note:** vectorscan translation units can consume 2–4 GB RAM each. Keep `-j` small (4 or lower) on machines with ≤32 GB RAM to avoid OOM lockups.

## Run

```bash
./build/kestrel
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

Example log taken from [logpai/loghub](https://github.com/logpai/loghub).