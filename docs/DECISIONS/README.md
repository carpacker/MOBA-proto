# Architecture Decision Records (ADRs)

Each ADR captures one significant, hard-to-reverse decision: the context, the
choice, and its consequences. They exist so settled decisions are not
re-litigated and so the *why* survives long after the *what* is code.

Format per file: `NNNN-short-slug.md` with **Status / Context / Decision /
Consequences**. Once accepted, an ADR is immutable — supersede it with a new ADR
rather than editing it (note the supersession in both).

## Index

| ADR | Title | Status |
|---|---|---|
| 0011 | [Server-authoritative netcode](0011-server-authoritative-netcode.md) | Accepted |

### Planned (to be written in Phase 0 · M0.1 — see [ROADMAP](../ROADMAP.md))

These cross-cutting contracts are decided once, before any subsystem code:
`0001` tick rate · `0002` fixed-point format · `0003` handle ABI ·
`0004` Vulkan loader (hand-load) · `0005` platform seam · `0006` module layout ·
`0007` sim RNG · `0008` shader build · `0009` error handling · `0010` asset id scheme.

> Numbering note: `0011` exists ahead of `0001–0010` because the lockstep →
> server-authoritative change was made during the design phase (it reverses an
> assumption the first design draft baked in). The lower numbers are the
> foundational contracts the roadmap schedules for Phase 0.
