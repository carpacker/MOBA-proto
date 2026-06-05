# MOBA (working title)

A multiplayer online battle arena built **from scratch in C++**, on a custom
RTS-class game engine.

> 🚧 **Status:** Early development. **Phase 0 complete** (build spine, ADRs, Win32
> window) and **Phase 1 core in progress** — memory arenas, float math, fixed-point
> deterministic sim math, and containers are done and tested; the test harness is
> next. Builds clean under `/WX`; ~88k test checks green across Debug/Release.
>
> See [`docs/JOURNAL.md`](docs/JOURNAL.md) for the session log,
> [`docs/ROADMAP.md`](docs/ROADMAP.md) for the plan, and
> [`docs/DECISIONS/`](docs/DECISIONS/) for the architecture decisions.

## Goals

- A custom C/C++ game engine sized for an RTS/MOBA: many units, deterministic
  simulation, top-down 3D rendering, and competitive multiplayer.
- Built deliberately over the long term, learning and owning each layer.

## Building

**Prerequisites**
- Visual Studio 2026 with the **Desktop development with C++** workload (provides
  MSVC, plus bundled CMake ≥ 3.28 and Ninja). Standalone CMake + Ninja also work.
- The **LunarG Vulkan SDK** — not needed yet (the `render` module is an empty
  skeleton), but **required from Phase 2** (Vulkan bring-up). Install it so
  `VULKAN_SDK` is set before then.

**Build** — `cl`, `cmake`, and `ninja` must be on `PATH`, so build from a *Developer*
shell (or `call vcvars64.bat` first):

```bat
cmake --preset dev                          :: configure (Ninja Multi-Config)
cmake --build build --config Debug          :: or RelWithDebInfo / Release
```

`--preset ci` builds with warnings-as-errors (`/WX`). Configs: **Debug** (daily),
**RelWithDebInfo** (profiling / the build you play), **Release** (`/O2 /GL`+`/LTCG`).
See `docs/DECISIONS/` for the build contracts (ADR-0004/0006/0008/0009).

## Layout

```
engine/        the engine, one static lib per module (the CMake link graph = the architecture)
  core/        arenas, containers, handle.h, sim_config.h, log/assert  (leaf)
  math/        fix.h (Q16.16), rng.h, vec/mat/quat                     (leaf)
  platform/    the OS seam (Win32): window, input, timing, files, sockets, Vulkan surface
  render/      raw Vulkan behind a thin renderer seam
  (serialize / assets / sim / net arrive in their phases)
cmake/         CompilerWarnings, EngineOptions, CompileShaders helpers
game/ tools/   the game exe, sandbox, asset cooker
shaders/ assets/ tests/
docs/          ARCHITECTURE.md, ROADMAP.md, DECISIONS/ (ADRs)
```

