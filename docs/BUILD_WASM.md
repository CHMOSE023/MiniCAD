# MiniCAD WebAssembly Build Guide

This document describes how to build and run the browser canvas version of MiniCAD without Codex.

## Prerequisites

- Windows 10/11
- CMake 3.16 or newer
- Emscripten SDK installed at `D:\dev\emsdk`
- Ninja

This repository has been tested with the Ninja executable bundled with Visual Studio:

```powershell
C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe
```

If `ninja.exe` is already in `PATH`, you do not need to pass `-DCMAKE_MAKE_PROGRAM`.

## Configure

From the repository root:

```powershell
cd D:\code\MiniCAD
cmd /c "D:\dev\emsdk\emsdk_env.bat && emcmake cmake -S . -B out\web -G Ninja -DCMAKE_MAKE_PROGRAM=""C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"""
```

If Ninja is in `PATH`, this shorter command is enough:

```powershell
cmd /c "D:\dev\emsdk\emsdk_env.bat && emcmake cmake -S . -B out\web -G Ninja"
```

## Build

```powershell
cmd /c "D:\dev\emsdk\emsdk_env.bat && cmake --build out\web --target MiniCADWeb"
```

Expected output files:

```text
out\web\MiniCADWeb\index.html
out\web\MiniCADWeb\index.js
out\web\MiniCADWeb\index.wasm
```

## Run Locally

WebAssembly must be served over HTTP. Do not open `index.html` directly from the filesystem.

```powershell
python -m http.server 8088 --bind 127.0.0.1 --directory out\web\MiniCADWeb
```

Open:

```text
http://127.0.0.1:8088/index.html
```

Use `Ctrl+F5` after rebuilding to avoid stale browser cache.

## Browser Controls

- `Line`: start the line drawing tool.
- Left click once: set the line start point.
- Left click again: commit the line end point.
- Middle mouse drag: pan.
- Mouse wheel: zoom.
- Left click near an entity: pick/select.
- `Undo` or `Ctrl+Z`: undo.
- `Redo` or `Ctrl+Y`: redo.
- `Esc`: cancel the current tool.

## Troubleshooting

### `emcmake: no compatible cmake generator found`

Install Ninja or pass Visual Studio's bundled Ninja path:

```powershell
-DCMAKE_MAKE_PROGRAM="C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
```

### `PermissionError: emsdk_set_env.bat`

`emsdk_env.bat` writes `emsdk_set_env.bat` under `D:\dev\emsdk`. Run the shell with permission to write that directory.

### Canvas looks scaled or mouse coordinates are wrong

Rebuild the latest `MiniCADWeb` target and hard refresh the browser. The web entry maps browser canvas coordinates back to the canvas backing size before dispatching events to `Document`.

### Button clicks do nothing

Open DevTools and check the console. The page must use `Module.ccall("MiniCAD_StartLine", ...)` and must not overwrite `Module._MiniCAD_StartLine`, because `_MiniCAD_*` names are the raw wasm exports.
