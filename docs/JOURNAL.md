# Development Journal

A running log of work sessions — what was built, what was decided, what was learned.
Newest session first. Architectural decisions live in `docs/DECISIONS/` (ADRs); this
is the narrative record.

---

## Session 02 — 2026-06-06 — M1.4: test harness + CTest + pre-push gate (Phase 1 done)

**Scope:** Finish Phase 1 by replacing the precursor test helpers with a real,
self-registering harness, wire it into CTest behind a headless aggregate target, and
stand up the pre-push gate that makes the whole suite a push-blocking contract.
**Outcome:** **M1.4 complete → Phase 1 complete.** `ctest` is green in ~0.5s; the gate
was proven to block on a deliberately-injected failing `CHECK` and pass once reverted.
Working tree has the harness + hook changes staged for commit (not yet committed).

### What was built

| Piece | Detail |
|---|---|
| `tests/test.h` | Self-registering `TEST(suite, name)` + `CHECK`/`CHECK_APPROX`. No exceptions, no STL. ~150 lines. |
| `tests/test_main.cpp` | Shared `main()` → `test::run_all` with `--suite` / `--filter` / `--list`. |
| 4 suites refactored | `mem` (6 tests), `math` (12), `containers` (14), `det` (12). `int main()` + `section()` → `TEST()` blocks. |
| `engine_core_group` | INTERFACE aggregate (`core+math+platform`) the test binary links — the headless target. |
| CTest | One entry **per suite** (`--suite` filter) so a red test names its module: `mem`, `math`, `containers`, `det_precise`, `det_fast`. |
| `tools/hooks/pre-push` + `ctest-gate.bat` | sh shim → `cmd` → vcvars + `/WX` build + `ctest`. Activated via `core.hooksPath`. |

### Harness design — the two landmines handled

- **Static-init order across TUs is unspecified.** The intrusive-list head is a
  function-local `static` (`registry()`, construct-on-first-use), so a `Registrar` in
  any TU can link itself regardless of init order. Registrars append at the tail →
  cases list in source order (verified via `--list`).
- **The linker drops unreferenced object files**, which would silently delete the
  registrars ("my tests don't run"). This only bites when test objects live in a
  *static library*, so each `*_tests.cpp` is compiled **straight into the exe**, never
  into a lib. Contract is commented in both `test.h` and `tests/CMakeLists.txt`.
- `CHECK` attribution is by snapshotting the global `fails()` counter before/after each
  case, so the assert macros stayed byte-identical to the M1.0 versions (low-risk
  refactor; failures still self-report `file:line`).

### Verification

- `ci` (`/WX`) build clean; **`ctest` 5/5 green in ~0.5s.** Totals: `engine_tests`
  32 tests / 3,095 checks; `det_tests` + `det_tests_fastfp` 12 tests / **84,672 checks
  each** — golden `0x1808f09365745d5a` identical across `/fp:precise` + `/fp:fast`.
- **Gate proof:** injected `CHECK(1 == 2)` → `det`/`math`/`containers` stayed green,
  only `mem` went red, `ctest` failed, gate exit code **8** (push would be blocked).
  Reverted → green, exit 0. Ran the hook the way git invokes it (args + stdin) to
  confirm the sh→cmd→bat handoff.

### Notes / gotchas

- **`ctest-gate.bat` paren trap:** `)` inside an `echo` text *inside* an `if (…)` block
  closes the block early (`. was unexpected`). Rewrote with `goto :fail` + a single
  `%MSG%` echo, and grouped `if cond ( set … & goto … )` (bare `&` runs the next
  command unconditionally — must be inside the parens).
- The `'vswhere.exe' is not recognized` line is emitted from **inside** `vcvars64.bat`
  itself, not our script (we call vswhere by full path); the build/tests still run.
- **`engine_core_group` includes `eng_platform`** for the page-allocator backend only
  (`plat_mem_*`, the one OS dep of core's arenas — ADR-0005). Its window/input code is
  linked-but-unused in tests. Splitting the page backend out of the Win32 window TU so
  the group is *literally* window-free is a deferred cleanup (see backlog).
- Hook activated locally via `git config core.hooksPath tools/hooks`. When committing,
  mark the hook executable (`git update-index --chmod=+x tools/hooks/pre-push`) for
  non-Windows clones.

### Deferred backlog (carried forward)

- Split `plat_mem_*` out of `win32_platform.cpp` so `engine_core_group` is truly
  window-free (the only impurity in the headless aggregate).
- clang-cl + UBSan determinism run asserting the same golden (clang-cl not installed).
- Poison/real-free allocator memory-safety run (Debug-ASan preset; arena poison hooks
  stubbed).
- `*_free` vs `*_free_all` naming standardization; `eng_sim` float/hashmap grep-fence
  (lands with the sim lib in Phase 3, M3.4).
- **Install the LunarG Vulkan SDK before Phase 2** (`find_package(Vulkan)` becomes
  `REQUIRED`).

### Where we are / next

**Phase 1 is done** — the dependency-free core is built, tested, and gated. **Next is
Phase 2: raw Vulkan bring-up** (M2.0 instance→device→swapchain→clear, the project's #1
technical-risk cluster). **Install the Vulkan SDK first** so `find_package(Vulkan)` can
flip to `REQUIRED`.

---

## Session 01 — 2026-06-05 — Foundations (Phase 0 + Phase 1 core)

**Scope:** From an empty repository to a deterministic, tested engine core — build
system, platform window, memory, math, fixed-point determinism, and containers.
**Outcome:** Phase 0 complete; Phase 1 milestones M1.0–M1.3 complete (M1.4 remains).
Nine commits on `main` (`be86d5d` → `339defe`), all pushed. Every layer builds clean
in Debug/RelWithDebInfo/Release and under `/WX`, with ~88k test checks passing.

### The locked stack

| Axis | Decision | Recorded in |
|---|---|---|
| Engine scope | Pure from scratch (own Win32, math, raw Vulkan, parsers, ECS/net) | design docs |
| Platform | Windows-first, clean portability seam | ADR-0005 |
| Language | C-style / data-oriented **C++17**, no exceptions/RTTI | ADR-0009 |
| Build | CMake + Ninja Multi-Config + MSVC | ADR-0006 |
| Graphics | Raw Vulkan, **hand-loaded** loader (own dispatch table) | ADR-0004 |
| Sim numbers | **Fixed-point Q16.16**, fully deterministic, 30 Hz tick | ADR-0001/0002 |
| Handles | 32-bit generational, **18 index + 14 generation** | ADR-0003 |
| Netcode | **Server-authoritative** (predict + reconcile; real fog) | ADR-0011 |
| Genre | MOBA/RTS hybrid (more units/projectile churn than a typical MOBA) | project note |

### Timeline

| Commit | Milestone | Summary |
|---|---|---|
| `be86d5d` | init | repo scaffolding (.gitignore/.gitattributes/README) |
| `be3e123` | design | `docs/ARCHITECTURE.md` (14 §) + `docs/ROADMAP.md` (Phases 0–8+) + ADR-0011 |
| `b6d5c93` | M0.1 | foundational ADRs 0001–0010 |
| `28096af` | M0.2 | CMake build spine: `eng_*` module link graph, compiler posture |
| `202f073` | M0.3 | Win32 window + clean message loop (`tools/sandbox`) |
| `7b7e703` | M1.0 | memory: arenas + allocator interface (high-water, scratch, TempMemory) |
| `f2f117a` | M1.1 | float math: vec/mat/quat + the Vulkan projection (Y-flip/[0,1]) |
| `ff723e8` | M1.2 | determinism bedrock: Q16.16 `fix` + PCG32 + golden-hash test |
| `339defe` | M1.3 | containers + handle ABI: Array/InlineArray/Str/Pool/HandlePool/HashMap |

### How it was built — multi-agent workflows

Substantial design and verification used parallel subagent workflows; the adversarial
reviews repeatedly caught real, subtle bugs **before** commit:

- **Architecture design** (15 agents): 10 subsystem designs → 3 review lenses →
  synthesis. Produced ARCHITECTURE.md + ROADMAP.md. Caught (and fixed) a 30 Hz/60 Hz
  tick contradiction, a fixed-point format that used `__int128` (which MSVC can't
  compile), and a classic Vulkan acquire/present sync hazard.
- **Netcode revision** (4 agents): reworked the design from lockstep →
  server-authoritative when that decision was made, handling the partial-state
  prediction subtleties.
- **M1.2 determinism review** (6 agents): independently re-derived the PCG32 stream
  (KAT verified) and found **4 signed-overflow UB sites** — the golden hash had been
  baked over UB. All fixed.
- **M1.3 container review** (6 agents): confirmed the HashMap Robin-Hood/backward-shift
  core correct (a reviewer ran a standalone 5,000-trial adversarial-collision harness)
  and HandlePool staleness sound; found a `str_append` `u32` overflow (heap-overflow
  path) and missing trivially-copyable guards. All fixed.

### Engineering notes & gotchas (for future sessions)

- **Build environment:** `cl`/`cmake`/`ninja` are **not** on the global PATH. Build
  from a VS Developer shell, i.e. `call vcvars64.bat` (at
  `C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat`)
  then `cmake --preset dev` / `cmake --build build --config Debug`.
- **Presets:** `dev` (lenient) and `ci` (`MOBA_WERROR=ON` → `/WX`). Always validate
  under `ci` before committing.
- **Determinism is real, not aspirational:** the M1.2 golden hash
  (`0x1808f09365745d5a`) is verified identical across `{/fp:precise, /fp:fast} ×
  {Debug, Release}`. Keep the float ban in sim code.
- **Two design corrections made during implementation:** (1) the docs had a latent
  `eng_core` ↔ `eng_platform` dependency cycle — broken by injecting a *commit
  callback* into the Arena so core stays a pure leaf; (2) the handle split changed
  24+8 → **18+14** (ADR-0003) once the hybrid genre was clarified, and §2.3 was
  updated to match.

### Test status

`tests/test.h` shared harness (precursor to M1.4's self-registering version). Suites,
all green in Debug + Release + `/WX`:

| Suite | Checks |
|---|---|
| `mem_tests` | 18 |
| `math_tests` | 297 |
| `det_tests` (×2: `/fp:precise` + `/fp:fast`) | 84,672 each |
| `container_tests` | 2,767 |

### Deferred backlog (pick up at M1.4 / CI)

- `tests/test.h` self-registering `TEST()` harness + **CTest** + `eng_core_group`
  headless aggregate; wire the `pre-push` hook to run `ctest` (this *is* M1.4).
- clang-cl + UBSan determinism runs asserting the same golden (clang-cl not installed).
- Poison/real-free allocator memory-safety run (alongside Debug-ASan, arena poison
  hooks already stubbed).
- `*_free` vs `*_free_all` naming standardization; eng_sim grep-fence for hashmap
  internals.
- Install the **LunarG Vulkan SDK** before Phase 2 (`find_package(Vulkan)` becomes
  `REQUIRED` there).

### Where we are / next

The dependency-free core (`eng_core`, `eng_math`) is essentially done and tested.
**Next: M1.4** finishes Phase 1 (test harness + CTest). **Then Phase 2** — the first
long pole: raw Vulkan bring-up (instance → device → swapchain → clear → triangle →
instanced meshes). Install the Vulkan SDK first.
