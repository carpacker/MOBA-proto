# ADR 0001 — Simulation tick rate: fixed 30 Hz

- **Status:** Accepted (2026-06-05)

## Context

The simulation must advance at a fixed rate decoupled from render frame rate, so
it is deterministic (for replays and drift-free client prediction) and so gameplay
tuning is frame-rate-independent. Early design drafts disagreed (30 Hz vs 60 Hz),
and worse, the platform loop hard-coded its own rate while sim/net/gameplay assumed
another — a contradiction.

## Decision

**Fixed 30 Hz**, defined once in `engine/core/include/core/sim_config.h`:

```c
#define SIM_HZ            30                       // the one source of truth
#define SIM_DT_SECONDS    (1.0 / (double)SIM_HZ)   // wall-clock accumulator only
#define SIM_DT_FIXED      (FIX_ONE / SIM_HZ)       // fixed-point dt for integration
#define SIM_MAX_CATCHUP_S 0.25                     // accumulator clamp (anti spiral)
```

Both loop owners — the windowed loop (game / listen-server) and the headless loop
(dedicated server) — **read `SIM_HZ`; neither defines its own rate.** The
accumulator is rate-agnostic, so bumping to 60 Hz later is a one-constant change.

## Consequences

- 30 Hz is the proven cadence for this genre and **halves** per-tick sim, snapshot,
  `sim_hash`, and replication cost versus 60 Hz — which matters given the higher
  entity counts of a MOBA/RTS hybrid plus server-authoritative state replication.
- Input-to-effect latency floor is ~33 ms; server-authoritative client prediction
  hides the latency on the player's own actions, so this is acceptable.
- Abilities/movement are tuned in ticks. Revisit 60 Hz only if feel demands it and
  the tick budget allows — the rate-agnostic accumulator keeps that cheap.
