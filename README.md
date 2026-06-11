# MOBA (working title)

A multiplayer online battle arena built **from scratch in C++**, on a custom
RTS-class game engine.

> 🚧 **Status:** Early development. **Phases 0–1 complete; Phase 2 underway (M2.0 +
> M2.1 done).** Build spine, ADRs, Win32 window, memory arenas, float/fixed-point
> math, containers, and a self-registering test harness on CTest + a pre-push gate
> (determinism golden across `/fp:precise` + `/fp:fast`), plus GitHub Actions CI
> (Windows MSVC, `/WX`, Debug + Release).
> **The renderer draws its first triangle:** a hand-loaded raw-Vulkan renderer
> (ADR-0004) on **dynamic rendering + synchronization2** (the hard minimum spec,
> ADR-0012) with offline-compiled SPIR-V (ADR-0008), an on-disk pipeline cache, and
> an in-process readback — `sandbox --screenshot out.bmp` captures what it rendered,
> **validation-clean** (verified on a GTX 1070). **Next: M2.2 — textured quad**
> (buffers, image upload, descriptors).
> Run it: `build-ci\tools\sandbox\Debug\sandbox.exe`.
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
- The **LunarG Vulkan SDK** (sets `VULKAN_SDK`) — needed for the real renderer and
  the shader build (`glslc`). Without it the build still works but produces the
  **null render backend** (blank window; this is what CI builds for now).

**Build** — `cl`, `cmake`, and `ninja` must be on `PATH`, so build from a *Developer*
shell (or `call vcvars64.bat` first):

```bat
cmake --preset dev                          :: configure (Ninja Multi-Config)
cmake --build build --config Debug          :: or RelWithDebInfo / Release
```

`--preset ci` builds with warnings-as-errors (`/WX`). Configs: **Debug** (daily),
**RelWithDebInfo** (profiling / the build you play), **Release** (`/O2 /GL`+`/LTCG`).
See `docs/DECISIONS/` for the build contracts (ADR-0004/0006/0008/0009).

## Testing

Tests use a small self-registering harness (`tests/test.h`: `TEST()`/`CHECK`, no
exceptions/STL) and run under CTest:

```bat
cmake --preset ci                                   :: /WX build dir
cmake --build build-ci --config Debug
ctest --test-dir build-ci -C Debug --output-on-failure
```

Suites are `mem`, `math`, `containers`, `platform` (file I/O), `render` (pipeline-
cache blob checker — Vulkan-free, runs headlessly), and the determinism golden hash
built twice (`det_precise` `/fp:precise` + `det_fast` `/fp:fast` — both must match).
To run one binary directly: `engine_tests.exe --suite math` (or `--filter`, `--list`).

A **pre-push hook** runs the same `/WX` build + `ctest` and blocks the push on red.
Activate it once per clone (it shells out to `vcvars` so it works from any shell):

```bat
git config core.hooksPath tools/hooks
```

## Layout

```
engine/        the engine, one static lib per module (the CMake link graph = the architecture)
  core/        arenas, containers, handle.h, sim_config.h, log/assert  (leaf)
  math/        fix.h (Q16.16), rng.h, vec/mat/quat                     (leaf)
  platform/    the OS seam (Win32): window, input, timing, files, sockets, Vulkan surface
  render/      raw Vulkan behind a thin renderer seam; GLSL sources in render/shaders/
  (serialize / assets / sim / net arrive in their phases)
cmake/         CompilerWarnings, EngineOptions, CompileShaders helpers
game/ tools/   the game exe, sandbox, asset cooker
assets/ tests/
docs/          ARCHITECTURE.md, ROADMAP.md, DECISIONS/ (ADRs)
```

