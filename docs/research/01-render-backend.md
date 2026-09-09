# Decision: SDL3 + SDL_GPU as the graphics backend

Date: 2026-08-25. Status: **accepted**, implemented in `src/gfx`.

## Context

The project's targets are arm64 macOS (the main working platform) and
x86_64 Windows (deferred, but the base has to stay portable). Project
Dalian, the project closest to ours, uses SDL2 + OpenGL.

## Why not OpenGL

On macOS, OpenGL is frozen at version 4.1 and deprecated: no compute
shaders above 4.3, nothing newer, and Apple can drop it at any time. That
would mean building on something dying from day one, on the project's
**main** platform.

## The decision

`SDL_GPU` — the graphics abstraction inside SDL3. The same renderer code
maps onto Metal (macOS), Vulkan and D3D12 (Windows) with no platform
branches in our code. SDL3 also covers the window, input, audio and
timers.

The price: shaders have to be shipped in each backend's format — MSL for
Metal, SPIR-V for Vulkan, DXIL for D3D12. Sooner or later the build will
need shader cross-compilation (SDL_shadercross, or our own step over
DXC + SPIRV-Cross). While there are no shaders, that price is not yet
paid.

The alternative we rejected: bgfx — more mature and cross-platform too,
but it pulls in its own ecosystem and duplicates what SDL3 already gives
us (window, input), while SDL3 is needed either way.

## Verified

```
OpenBattlefield2 | macos/arm64
mod: Game Files/mods/bf2 | archives: 10 | mount points: 11
GPU backend: metal
frames drawn: 60
```
