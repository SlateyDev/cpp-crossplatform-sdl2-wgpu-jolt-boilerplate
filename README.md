# Cross-platform SDL2, WGPU, Jolt boilerplate (C++)

This project initializes:
- **SDL2** for window/canvas creation on both desktop and wasm, can also then be used for input and sound
- **WebGPU** (`wgpu-native` on desktop, browser WebGPU via Emscripten for wasm)
- **Jolt Physics**
- **Recast/Detour navmesh** (basic runtime build + path query)

## Requirements

- CMake 3.24+
- Ninja
- A C++20 compiler:
  - Windows: MSVC (VS 2022 Build Tools)
  - Linux: clang/clang++
  - macOS: Apple clang
- Emscripten SDK (for wasm builds)

## Build (desktop)

### Windows

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
.\build\windows-debug\aberrant_engine.exe
```

This opens an SDL2 window and renders a multicolored triangle via WebGPU.

### Linux

```bash
cmake --preset linux-clang-debug
cmake --build --preset linux-clang-debug
./build/linux-debug/aberrant_engine
```

### macOS

```bash
cmake --preset macos-clang-debug
cmake --build --preset macos-clang-debug
./build/macos-debug/aberrant_engine
```

## Build (wasm)

1. Install and activate Emscripten (`emsdk_env`).
2. Ensure `EMSDK` env var points at your emsdk root (for example `X:/Beef/Beef/wasm/emsdk`).

```bash
cmake --preset wasm-debug
cmake --build --preset wasm-debug
```

Output HTML/wasm files will be in `build/wasm-debug`.

Run from a local web server (required by browsers for WebGPU):

```powershell
python -m http.server -d build/wasm-debug 8000
```

Then open `http://localhost:8000/aberrant_engine.html`.

## Fly camera controls

- **W / A / S / D**: Move forward / left / back / right
- **Q / E**: Move down / up
- **Shift**: Move faster
- **Mouse move**: Look around (mouse-look)
- **Tab**: Toggle mouse-look mode
- **Esc**: Exit mouse-look mode
- **Left click**: Re-enter mouse-look mode (useful in browser builds after pointer-lock restrictions)
- The on-screen debug overlay now also shows `NavMesh` path status.

## Other build presets

```bash
cmake --list-presets=all
```

## Notes

- This boilerplate has been built using GPT-5.3-Codex under GitHub Copilot
  - Linux and MacOSX support has not been tested and will likely not work right now as the CreateSurfaceFromWindow function currently only supports Windows HWND and WASM Canvas.
  - The "plan" is to update this boilerplate without relying on AI to improve it. AI was just a tool to help create the boilerplate, not for prolongued development.
  - I can see it is AI slop, but it is not the final so I don't really care as I use it as the starting point.
  - I couldn't find a boilerplate that included the features I was after (specifically WGPU + Jolt with Win & Web output support)
  - Even most of this readme was created using AI and I have made minor changes at this point
- Desktop builds fetch dependencies with `FetchContent`:
  - `SDL2` from GitHub
  - `JoltPhysics` from GitHub
  - `RecastNavigation` from GitHub
  - `WebGPU-distribution` from GitHub (which fetches prebuilt `wgpu-native` by default)
- If your environment already provides these dependencies, set:

```bash
cmake -DABERRANT_USE_SYSTEM_DEPS=ON ...
```
