# Design Detail Log

Locked design decisions, recorded in chronological order under the phase
they belong to. See `CLAUDE.md` for the entry format.

---

## Phase -1 — Foundation

Cross-cutting decisions made before any phase implementation begins.

### Decision: Time model — discrete-event simulation

- **Choice:** Discrete-event simulator. Time advances by jumping to the
  next scheduled event, not by fixed ticks.
- **Reason:** A 60s game-time run with 1 μs ticks is 60M iterations of
  mostly-empty work. Event-driven keeps work proportional to actual
  transactions and matches how a real bus arbiter reasons.
- **Alternatives considered:**
  - Fixed-tick (1 μs) — rejected: ~100× slower, no fidelity gain at L2.
- **Implications:** Need a stable priority-queue event loop. All
  components publish future events rather than poll.
- **Date:** 2026-05-05

### Decision: Language and toolchain

- **Choice:** C++20, CMake, `doctest` for tests, `yaml-cpp` for config
  and scenarios. No Python, no Boost, no SystemC.
- **Reason:** C++20 gives `std::span`, concepts, `<chrono>` literals —
  enough expressiveness without heavy deps. SystemC would consume ~⅓ of
  project time for fidelity we don't need at L2. Python would split the
  build story.
- **Alternatives considered:**
  - SystemC — rejected: RTL-grade fidelity, wrong layer for protocol design.
  - Rust — rejected: no project benefit, narrower interview surface for SIE.
  - C++ + Python plotting — rejected per user direction; CSV output is
    consumed externally if needed.
- **Implications:** Visualization is deferred / out-of-build. Results
  are CSV; any plotting happens outside the repo.
- **Date:** 2026-05-05

### Decision: Bus abstraction layer — L2 (transaction-level)

- **Choice:** Model bus traffic as transactions carrying
  `{address, length, qos_tag, deadline}`. Do **not** model AXI
  beat-level handshakes (AR/R/AW/W/B channels).
- **Reason:** L2 is the lowest layer that still tells the QoS-arbitration
  story. L3 (beat-level) is RTL territory and adds no narrative value
  for a system-architecture portfolio. L1 (bandwidth-only) cannot
  express priority preemption.
- **Alternatives considered:**
  - L1 bandwidth-budget only — rejected: too abstract, can't show QoS.
  - L3 full AXI5 channels — rejected: high cost, wrong audience.
- **Implications:** Scheduler reasons about transactions, not beats.
  Latency is `size / bandwidth + queueing_delay`, no per-beat modeling.
- **Date:** 2026-05-05

### Decision: System parameters live in `config.yaml`

- **Choice:** All system constants (SSD bandwidth, NPU memory size, bus
  bandwidth, audio/texture/weight profiles, LOD sizes) are defined in
  a top-level `config.yaml`. The simulator reads this at startup; no
  numeric constants in C++ source.
- **Reason:** Enables A/B sweeps and "what-if" experiments without
  recompiling. Also keeps the source readable — tuning lives in data.
- **Initial values (subject to tuning):**
  - SSD raw bandwidth: 5.5 GB/s; effective post-decompression: 8–9 GB/s
  - NPU dedicated memory: 512 MB
  - Shared bus bandwidth: 16 GB/s
  - Audio: 5 MB/s, 256 B packets, 1 ms deadline
  - Texture burst: up to 500 MB/s sustained 100 ms
  - AI weight LODs: LOD0=100 MB / LOD1=30 MB / LOD2=10 MB
- **Alternatives considered:**
  - Hardcoded constants — rejected: kills experimentation.
  - CLI flags — rejected: too many knobs for argv ergonomics.
- **Implications:** A `Config` loader is a Phase 1 deliverable. Tests
  must be able to inject a synthetic config.
- **Date:** 2026-05-05

### Decision: Scenarios in YAML

- **Choice:** Each test scenario (agent placement, traffic pattern,
  duration) is a YAML file under `scenarios/`. Simulator takes a
  scenario path as its primary argument.
- **Reason:** Decouples experiments from code. Lets a reviewer change
  scene parameters live during a demo without rebuilding.
- **Alternatives considered:**
  - JSON via nlohmann/json — rejected: less readable for human-authored
    scenes; user picked YAML.
  - Hardcoded scenarios in C++ — rejected: poor demo ergonomics.
- **Implications:** `yaml-cpp` is in the build. Scenario schema needs
  documenting in `docs/scenario-schema.md` (Phase 2 deliverable).
- **Date:** 2026-05-05

### Decision: Strategy pattern for scheduler / DMA paths

- **Choice:** Scheduler and DMA-path are runtime-swappable strategies.
  A single `Simulator` runs the same scenario under different policies
  to produce comparable A/B traces.
- **Reason:** Forces clean separation between policy and mechanism, and
  makes baseline-vs-NeuroStream comparisons trivially reproducible.
- **Alternatives considered:**
  - Compile-time policy via templates — rejected: harder to A/B at
    runtime, slower iteration.
  - Two separate binaries — rejected: drift risk between baselines.
- **Implications:** All policy classes implement narrow virtual
  interfaces. Per-run policy is selected via config or CLI flag.
- **Date:** 2026-05-05

### Decision: Phase reordering — predictor split

- **Choice:** A "dumb" scripted predictor lives in Phase 2 alongside
  traffic injectors so Phases 3–5 have a request source. The real
  frustum-aware predictor stays in Phase 6 as an upgrade.
- **Reason:** Without any predictor, scheduler/LOD/DMA phases have
  nothing meaningful to schedule. The dumb version unblocks early
  phases without prejudging the real predictor's design.
- **Alternatives considered:**
  - Keep predictor entirely in Phase 6 — rejected: earlier phases would
    need throwaway stubs anyway.
- **Implications:** The predictor interface is fixed in Phase 2; Phase
  6 only swaps the implementation behind it.
- **Date:** 2026-05-05

### Decision: Multi-core NPU is in scope

- **Choice:** Model the NPU as N independent execution units sharing a
  single weight cache. N is set in `config.yaml` (default: 4).
- **Reason:** A single-queue NPU hides interesting contention — e.g.
  one agent's inference stalling another's weight load. Multi-core
  exposes the eviction policy to real pressure and makes the
  distance-weighted LRU story credible.
- **Alternatives considered:**
  - Single-core NPU — rejected per user direction; loses the
    contention story.
  - Per-core private cache — deferred: more complex, can be a future
    extension; shared cache is the more interesting default for the
    eviction-policy narrative.
- **Implications:** NPU model needs a per-core request queue plus a
  shared cache with the eviction policy from Pillar F. KPIs must
  report per-core stats and aggregate.
- **Date:** 2026-05-05
