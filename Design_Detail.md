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

### Decision: Config vs scenario file split

- **Choice:** Two YAML files with distinct responsibilities.
  - `config.yaml` — hardware constants and policy defaults (SSD/NPU/bus
    specs, LOD sizes, NPU core count, scheduler policy selection).
  - `scenarios/*.yaml` — per-experiment variables (agent placement,
    traffic timeline, run duration).
- **Reason:** Hardware spec changes rarely; scenarios change every run.
  Mixing them means every experiment carries a copy of the spec, which
  drifts. Splitting also lets a single scenario run against multiple
  hardware configs cleanly.
- **Alternatives considered:**
  - Single combined YAML — rejected: drift risk and bloated diffs.
- **Implications:** Simulator CLI takes both: `sim --config config.yaml
  --scenario scenarios/foo.yaml`. Loaders are independent.
- **Date:** 2026-05-05

---

## Phase 0 — Bootstrap

### Decision: License — MIT

- **Choice:** MIT License at repo root.
- **Reason:** Maximally permissive; standard for portfolio repos;
  removes friction for any reviewer who wants to clone and run.
- **Alternatives considered:**
  - Apache-2.0 — rejected: patent clause adds noise for a sim project
    with no patentable IP.
  - No license — rejected: defaults to "all rights reserved" which
    discourages a reviewer from running the code.
- **Implications:** None beyond shipping `LICENSE`.
- **Date:** 2026-05-05

### Decision: CI matrix — Ubuntu/GCC + macOS/Clang

- **Choice:** GitHub Actions runs on `ubuntu-latest` (GCC) and
  `macos-latest` (Apple Clang). Build + run smoke test on each.
- **Reason:** macOS is the primary dev environment; Ubuntu is the
  reviewer's likely environment. Covers the two compiler families that
  matter without paying the MSVC tax.
- **Alternatives considered:**
  - Ubuntu only — rejected: dev-time regressions on macOS would be
    invisible until local rebuild.
  - Add Windows/MSVC — rejected: C++20 quirks in MSVC are a known time
    sink; not worth it for a single-developer portfolio piece.
- **Implications:** Code must stay portable between libstdc++ and
  libc++. No GCC-specific extensions.
- **Date:** 2026-05-05

---

## Phase 1 — Simulation Core

### Decision: Event cancellation via tombstones

- **Choice:** `EventQueue` exposes a `cancel()` API, but cancellation
  is implemented by flipping an `alive` flag on the event entry rather
  than removing it from the heap. The main loop checks the flag on pop
  and skips dead entries.
- **Reason:** Real cancellation (heap-remove) needs O(log n) erase by
  arbitrary handle and bookkeeping for index-in-heap. Tombstones get
  the same external behavior with a `std::shared_ptr<bool>` and a few
  extra bytes per event. Industry-standard pattern in DES libraries.
- **Use cases this enables:** predictor invalidating stale prefetches
  on view-frustum change, LOD upgrade overriding a pending lower-tier
  load, timeout alarms cancelled when the load completes early.
- **Alternatives considered:**
  - True heap-remove via `std::set` keyed on `(time, id)` — rejected:
    higher constant factor and more complex API.
  - No cancellation at all — rejected: forces awkward workarounds in
    Phases 6 (predictor) and 8 (degradation).
- **Implications:** Cancelled events still occupy heap memory until
  popped. If a future profiler shows zombie buildup, swap the impl
  without changing the API.
- **Date:** 2026-05-05

### Decision: Time type — `int64_t` microseconds

- **Choice:** `using Time = std::int64_t;` representing microseconds.
  No `std::chrono` types in the public API.
- **Reason:** Trace records, log lines, and YAML inputs all express
  time as plain integers; `chrono` adds compile cost and verbose
  printing for no semantic gain at simulator scope.
- **Alternatives considered:**
  - `std::chrono::microseconds` — rejected: harder to print, harder
    to serialize, no type-safety benefit since the whole sim is in μs.
  - `double` seconds — rejected: floating-point ordering hazards in
    the priority queue.
- **Implications:** A signed 64-bit μs counter wraps after ~292,000
  years. Negative times are reserved for "invalid"/sentinel values.
- **Date:** 2026-05-05

### Decision: Config loader — fail-fast on missing fields

- **Choice:** YAML loader throws `std::runtime_error` (or a typed
  `ConfigError`) on any missing or malformed field. No silent
  defaults inside the loader.
- **Reason:** Silent defaults hide config drift bugs that are nearly
  impossible to debug from KPI noise. If a default is desired, it
  goes into the shipped `config.yaml` explicitly so it is reviewable.
- **Alternatives considered:**
  - Default-on-missing — rejected: hides typos, hides forgotten
    fields, hides schema drift between branches.
- **Implications:** Schema changes require updating both the loader
  and any test fixtures in lockstep. Failure messages must name the
  missing field path.
- **Date:** 2026-05-05

### Decision: Trace output — binary + CSV

- **Choice:** Trace logger emits both a compact binary stream (primary)
  and a CSV (human-readable). CSV is the default for short runs; binary
  is the format kept for long runs.
- **Reason:** Long scenarios (multi-minute open-world traversal) will
  produce trace files that are awkward to handle as CSV. Binary keeps
  storage and write cost bounded; CSV stays available for quick eyeball
  review and external analysis.
- **Alternatives considered:**
  - CSV only — rejected: file size and write throughput become
    bottlenecks on long runs.
  - Binary only — rejected: loses easy inspection during development.
- **Implications:** Need a small `trace_dump` utility (C++) to convert
  binary → CSV on demand. Schema must be versioned in the binary header
  so old traces stay readable as the schema evolves.
- **Date:** 2026-05-05

### Decision: Trace record schema v1

- **Choice:** Fixed 32-byte record:
  ```
  int64  timestamp_us
  uint32 source_id          // injector or scheduler entity id
  uint8  event_type         // enum (issue / arrive / complete / drop / ...)
  uint8  qos_tag            // critical / high / normal
  uint16 _reserved
  uint32 size_bytes
  uint32 latency_us         // 0 for issue events
  uint64 transaction_id     // monotonically assigned, joins issue↔complete
  ```
  Binary file starts with a `TraceHeader { magic[4]="NSTR"; uint32 version=1;
  uint32 record_size=32; uint32 _reserved=0 }`.
- **Reason:** `transaction_id` lets analysis tools join issue and
  completion records to derive end-to-end latency. Fixed-size records
  are append-only friendly and trivial to mmap or parse.
- **Alternatives considered:**
  - Variable-size records with TLV — rejected: unnecessary at this
    schema simplicity; can be added in v2 if string fields appear.
- **Implications:** Schema version bumps require updating the header,
  the writer, and any reader. Analysis assumes little-endian host
  (true on x86_64 and arm64).
- **Date:** 2026-05-05

### Decision: Streaming trace writes

- **Choice:** Trace records are written to disk as they are produced,
  not accumulated in memory until shutdown.
- **Reason:** A multi-minute open-world scenario can emit millions of
  records; buffering them all would push RAM into multi-GB territory
  and lose data on a crash.
- **Alternatives considered:**
  - Batch dump at end-of-run — rejected: RAM pressure and crash loss.
- **Implications:** `TraceWriter` owns the output streams via RAII;
  flush-on-destroy is sufficient for normal shutdown. Tests that
  inspect a trace must close the writer before reading.
- **Date:** 2026-05-05

---

## Phase 2 — Traffic Injectors + Dumb Predictor

### Decision: Injectors use the push model

- **Choice:** Each injector is an event-driven actor. On `start()` it
  schedules its first event into the shared `EventQueue`; each handler
  produces a `Transaction`, hands it to a `TransactionSink`, and
  schedules its successor.
- **Reason:** Aligns with the discrete-event core — injectors are
  peers to every other event-producing entity. Burst behavior maps to
  "schedule the next event sooner". Multi-injector concurrency is
  trivially independent. Phase 3 preemption is just "insert a higher-
  priority event ahead of the queued ones", which falls out of the
  same model.
- **Alternatives considered:**
  - Pull model (driver polls each injector for `peek_next()` /
    `take_next()`) — rejected: forces a parallel driver loop, awkward
    multi-injector tie-breaking, and an additional state machine per
    injector for chained behavior. The "fewer events in heap" win is
    immaterial at our scale.
- **Implications:** Sinks must accept transactions synchronously
  (cannot block the injector). Backpressure, if ever needed, lives
  inside the sink as queueing — not in the injector.
- **Date:** 2026-05-05

### Decision: TransactionSink interface decouples producers from scheduler

- **Choice:** Phase 2 ships with a `TransactionSink` abstract base
  with one method `accept(const Transaction&)`. A `TraceSink`
  implementation writes each transaction as an `Issue` record. Phase 3
  swaps the sink for a real scheduler without touching injectors.
- **Reason:** Lets Phase 2 demonstrate end-to-end traffic flow before
  the scheduler exists, and freezes the contract that Phase 3 must
  honor.
- **Alternatives considered:**
  - Inject directly into a global queue — rejected: ties injectors to
    a specific scheduler implementation.
- **Implications:** All injectors take `Sink&` at start. Tests use a
  recording sink that captures transactions in a vector for
  assertion.
- **Date:** 2026-05-05

### Decision: Mixed scenario time model — declarative streams + absolute events

- **Choice:** Scenario YAML has two sections.
  - **Declarative streams** for steady traffic: `audio: { enabled: true }`
    runs from 0 to `duration_ms` at the rate set in `config.yaml`.
  - **Absolute-time event lists** for triggered behavior:
    `texture_bursts: [{ at_ms, rate_mbps, duration_ms }]` and
    `weight_prefetches: [{ at_ms, npc_id, lod }]`.
- **Reason:** Steady streams are tedious to express as event lists;
  one-shot bursts are tedious to express as steady streams. Splitting
  the two keeps each YAML section readable.
- **Alternatives considered:**
  - All-declarative — rejected: bursts become awkward.
  - All-event — rejected: audio would need thousands of entries.
- **Implications:** Scenario loader handles both shapes. Scenario
  schema is documented in `docs/scenario-schema.md`.
- **Date:** 2026-05-05

### Decision: Scripted predictor in Phase 2

- **Choice:** The Phase 2 `Predictor` reads `weight_prefetches` from
  the scenario and schedules each as a one-shot weight request via
  `AIWeightInjector`. No spatial reasoning yet.
- **Reason:** Lets Phase 3–5 run with realistic weight traffic
  patterns. The frustum-aware predictor in Phase 6 simply replaces
  the implementation behind the same interface.
- **Alternatives considered:**
  - Defer all prediction to Phase 6 — rejected: leaves Phase 3–5
    without weight traffic.
- **Implications:** `Predictor` is an abstract base; both the
  scripted impl (Phase 2) and the spatial impl (Phase 6) realize it.
- **Date:** 2026-05-05

### Decision: Scenario loader is fail-fast, like config

- **Choice:** Missing required fields in scenario YAML throw
  `ScenarioError`. The loader uses the same `require()` / `as<T>()`
  pattern as `load_config()`.
- **Reason:** Consistency with config loader. Silent defaults mask
  experiment drift between scenarios.
- **Alternatives considered:**
  - Optional fields with defaults — rejected: same reasoning as
    config decision.
- **Implications:** Each scenario must be complete on its own. The
  test suite includes a malformed-scenario fixture verifying the
  failure mode.
- **Date:** 2026-05-05

### Decision: Texture streaming block size — 256 KB, in `config.yaml`

- **Choice:** Texture bursts emit 256 KB blocks. The size lives in
  `config.yaml` as `texture.block_bytes` because it represents a
  hardware-level streaming granularity, not an experiment knob.
- **Reason:** With 500 MB/s × 100 ms = 50 MB per burst, 256 KB blocks
  yield ~190 transactions per burst — enough to exercise the
  scheduler without flooding the trace.
- **Alternatives considered:**
  - 64 KB — rejected: ~760 events per burst, noisy traces.
  - 1 MB — rejected: ~50 events per burst, scheduler decisions
    happen too coarsely.
- **Implications:** All texture transactions carry size = 256 KB.
  Audio packets remain 256 B; weight transactions remain LOD-sized
  blobs.
- **Date:** 2026-05-05

---

## Phase 3 — Virtual AXI Bus Scheduler

### Decision: Quantum-based service model, 100 μs quantum

- **Choice:** Bus services one transaction at a time but in fixed
  100 μs slices. After each slice the scheduler re-arbitrates. A
  transaction smaller than one slice completes early.
- **Real-system reference:** AXI bursts are themselves uninterruptible,
  but production DMA engines (incl. PS5's I/O complex) issue rapid
  short bursts (~64–256 B each) so the arbiter re-picks frequently.
  100 μs quantum models the "effective re-arbitration granularity"
  this produces, without simulating individual bursts.
- **Alternatives considered:**
  - Whole-transaction service (no preemption) — rejected: a 100 MB
    weight (12.5 ms @ 8 GB/s) would block audio entirely. Inflates
    baseline badness 5–10× vs reality.
  - Beat-level (RTL) — rejected: violates L2 abstraction; simulates
    pipeline / OoO completion irrelevant to protocol design.
- **Implications:** Quantum boundary is the only preemption point.
  All transactions carry `bytes_remaining`; partial-service is
  re-queued. P99 audio latency error vs real PS5 expected < 20%.
- **Date:** 2026-05-06

### Decision: Two-tier QoS — strict critical + DRR bulk

- **Choice:** Two-tier scheduling.
  - **Tier 1 (Critical):** strict priority, but rate-limited to 5 % of
    bus bandwidth via a token bucket. Cannot starve Tier 2 because
    the limit caps it.
  - **Tier 2 (Bulk):** weighted fair queueing across `high` and
    `normal` classes. Picks the class with smallest
    `bytes_served / weight` (virtual-time). Default weights
    `high : normal = 2 : 1`.
- **Real-system reference:** ARM AMBA QoS guidance, Intel LTR, and
  modern NoC designs all warn that pure strict priority starves; the
  industry pattern is "strict for tiny rate-limited critical class +
  fair-share for bulk". PS5's 6-level I/O priority operates the same
  way (audio/control vs. asset streaming).
- **Weight rationale:** 2:1 reflects "weight loads have soft deadlines,
  texture is best-effort". Numbers derived from "guarantee high ≥ 1
  GB/s, normal ≥ 4 GB/s of remaining bus" rather than picked at
  random — see Phase 9 for tuning.
- **Alternatives considered:**
  - B1 strict priority only — rejected: starvation risk; not what
    real silicon does.
  - B2 pure DRR / WFQ — rejected: critical audio packets would queue
    behind bulk transactions and blow their deadlines.
- **Implications:** `config.yaml` `scheduler.qos_weights` replaced
  with `critical_rate_limit_pct` + `bulk_weights {high, normal}`.
  Token bucket state lives inside `QoSScheduler`.
- **Date:** 2026-05-06

### Decision: Single serialized bus

- **Choice:** One logical bus, one transaction in service at a time
  at full `total_bandwidth_mbps`. No parallel lanes in Phase 3.
- **Real-system reference:** PS5 I/O complex bottleneck is the SSD
  read channel; downstream fan-out (GDDR6, NPU, coherency) is wider
  but the ingress is single-channel. Modeling the bottleneck is what
  matters.
- **Alternatives considered:**
  - Parallel R/W lanes — rejected for Phase 3 (read-dominated
    traffic). Tracked in PROJECT_PLAN.md backlog for Phase 5 pickup.
  - N independent lanes — rejected: hides bandwidth contention,
    making Baseline look better than it is.
- **Implications:** Service time = `size / bus_bandwidth`. No
  read/write distinction yet. Phase 5 revisits.
- **Date:** 2026-05-06

### Decision: Trace milestone events — 5 types

- **Choice:** Extend `EventType` to {Issue, Arrive, ServiceStart,
  Complete, Drop}. No per-quantum events. Each transaction's life
  produces 3–4 records (Issue→Arrive→ServiceStart→Complete, or
  Issue→Arrive→Drop).
- **Real-system reference:** Linux blktrace records Q/I/D/C, ARM
  CoreSight does similar. This is the standard IOPS-profiling
  vocabulary; scheduling KPIs (queueing latency, service time,
  end-to-end) all derive from joining these milestones by
  `transaction_id`.
- **Alternatives considered:**
  - Issue + Complete only — rejected: cannot distinguish "blocked in
    queue" from "slow transfer".
  - Per-quantum records — rejected: 100 MB weight = 125 quantums =
    trace bloat; quantum-level data not used by any KPI.
- **Implications:** `Drop` only emitted for transactions whose
  `deadline > 0` and is missed (audio). KPI extraction logic in
  Phase 9 joins by `transaction_id`.
- **Date:** 2026-05-06

### Decision: A/B comparison via `--policy` CLI flag

- **Choice:** `neurostream` accepts `--policy {fifo|qos}`. Same
  scenario can be run with each policy producing separate trace
  files. A `--ab` flag runs both back-to-back and prints a KPI diff
  table.
- **Real-system reference:** Production schedulers ship with
  multiple policies behind a runtime knob (`/sys/block/.../queue/scheduler`
  on Linux). Same pattern.
- **Alternatives considered:**
  - Two binaries — rejected: code drift risk.
  - Compile-time policy template — rejected: kills A/B ergonomics.
- **Implications:** `Scheduler` is an abstract base. `FIFOScheduler`
  and `QoSScheduler` implement it. Selected at runtime.
- **Date:** 2026-05-06

---

## Phase 4 — Weight LOD Manager

### Decision: LOD semantics — function tiers, not quality tiers

- **Choice:** The three LOD tiers represent **different categories of
  AI model**, not different quality grades of the same model. Real
  industry direction (NVIDIA ACE, Inworld AI) splits NPC intelligence
  into multiple specialized models loaded by proximity:
  - **LOD0 (100 MB)** — dialogue LLM; load when player can converse (<10 m)
  - **LOD1 (30 MB)** — behavior / reaction NN; load when interaction is
    likely (combat range, <30 m)
  - **LOD2 (10 MB)** — bark / expression model; ambient verbal and
    facial reactions (<100 m)
  - **No LOD (0 MB)** — pure FSM, no neural inference; background NPCs
- **Reason:** Current games use LLMs only for dialogue (user's
  observation). Treating LOD as quality grades of one giant
  "everything" LLM was inaccurate and would not match where real AI
  pipelines are heading. Function tiers correctly explain why streaming
  is needed: a 50-NPC scene cannot pre-load every functionality for
  every NPC, so models swap in on demand by interaction relevance.
- **Alternatives considered:**
  - Single-LLM proximity swap (binary load/unload) — rejected: simpler
    but kills the LOD pillar; project drops from 5 to 4 pillars.
  - Quality-tier LLM — rejected: technically misleading, doesn't match
    industry direction, weaker SIE narrative.
- **Implications:** Distance bands, hysteresis, and tick mechanics are
  unchanged from the original plan. Only the *meaning* of each LOD
  number changes, which is captured in docs and YAML comments.
- **Date:** 2026-05-16

### Decision: Distance bands — discrete with forward-looking schema

- **Choice:** LOD is selected by discrete distance bands defined in
  `config.yaml`:
  ```yaml
  lod_manager:
    bands:
      - { lod: 0, max_distance_m: 10 }
      - { lod: 1, max_distance_m: 30 }
      - { lod: 2, max_distance_m: 100 }
  ```
  NPCs beyond 100 m get no LOD (FSM-only, no weight load).
- **Reason:** AI weights have no meaningful interpolation between
  tiers (you cannot average a 100 MB LLM and a 10 MB bark model).
  Matches Unreal AI Significance Manager and Unity LOD Group, both of
  which use discrete bands. Schema reserves keys for `npc_priority`
  and 2D `position` to support Phase 6 (frustum / quest-NPC override)
  without rewrites.
- **Alternatives considered:**
  - Continuous mapping — rejected: meaningless for discrete model
    artifacts.
  - Five+ bands — rejected: more switches without proportional value
    until per-NPC priority is added in Phase 6.
- **Implications:** Phase 4 ignores `priority` and `position` fields
  if present in scenarios; Phase 6 activates them.
- **Date:** 2026-05-16

### Decision: Hysteresis — 20% deadband with conservative cold-start

- **Choice:** Each band has an asymmetric `enter`/`leave` threshold
  with a 20% deadband (`leave = max_distance × 1.2`). On scenario
  start, an NPC currently inside a deadband initializes to the
  **larger** (more conservative) LOD number — i.e. assume "further
  away" until evidence proves otherwise.
- **Reason:** A 60 Hz oscillation across a hard boundary would burn
  ~7.8 GB/s of bus bandwidth on swap traffic alone — exceeds raw SSD
  rate, deadlocks the system in simulation. 20% matches Unreal's
  default texture-pool deadband. Conservative cold-start avoids
  starting NPCs at LOD0 and immediately downgrading.
- **Alternatives considered:**
  - Dwell-time hysteresis (must stay N ms in new band) — rejected for
    Phase 4: slower reaction to legitimate large motion (player
    teleport, fast vehicles). Revisit in Phase 6 with velocity-aware
    deadband.
  - No hysteresis — rejected: boundary thrash destroys the demo.
- **Implications:** Velocity-aware deadband is explicit Phase 6
  follow-up. Boundary NPCs at scenario start get LOD1/2 not LOD0.
- **Date:** 2026-05-16

### Decision: NPC motion — scripted waypoints with linear interpolation

- **Choice:** Scenarios describe NPC motion as a list of `(at_ms,
  distance_m)` waypoints per NPC. Between waypoints, distance is
  linearly interpolated. Schema reserves `position: {x, y}` and a
  top-level `player_position` for Phase 6; Phase 4 reads only
  `distance_m`.
- **Reason:** Reproducibility is the basis for A/B comparison —
  random motion produces unreproducible drop counts and latency
  distributions. Linear interpolation is the simplest model that
  matches Unreal Sequencer / Unity Timeline scrubbing semantics.
- **Alternatives considered:**
  - Velocity-vector model (initial pos + velocity) — rejected: hard
    to express "player turns around" or sudden direction change.
  - Random walk — rejected: not reproducible.
  - Acceleration model — rejected: complexity without Phase 4 payoff;
    linear is enough to test LOD transitions.
- **Implications:** Velocity at waypoint boundaries is discontinuous;
  Phase 6 spatial predictor must handle this or scenarios must use
  finer waypoints around prediction-sensitive moments.
- **Date:** 2026-05-16

### Decision: LOD Manager tick rate — 60 Hz

- **Choice:** LOD Manager re-evaluates every NPC's required LOD every
  16,667 µs (60 Hz). Configurable via `lod_manager.tick_us`.
- **Reason:** Matches console game frame rate; aligns LOD decisions
  with player-visible frame boundaries; 5-second scenario produces
  300 tick events × N NPCs — manageable trace size.
- **Alternatives considered:**
  - 1 ms tick — rejected: 5,000 events per 5 s, noisy trace, no
    benefit (LOD changes happen on hundred-ms scale).
  - Event-driven (compute next threshold crossing time) — rejected:
    complex with linear waypoints, marginal saving.
  - Per-NPC stagger — deferred to Phase 8 (when NPC count is high
    enough to matter).
- **Implications:** All NPCs evaluated on the same tick — Phase 8 may
  add stagger if simultaneous prefetch storms become a problem.
- **Date:** 2026-05-16

### Decision: In-flight tracking; full cache interface preserved

- **Choice:** Phase 4 maintains a set of `(npc_id, lod)` pairs that
  have been issued and not yet completed (in-flight). A second LOD
  decision that produces an already-in-flight request is suppressed.
  The `LodPredictor` exposes a `CacheLookup` interface — methods
  `on_complete(npc_id, lod)`, `on_evict(npc_id, lod)`,
  `is_resident(npc_id, lod)` — but Phase 4 treats the resident set as
  unbounded (no evictions are issued by Phase 4 code). Phase 8 will
  implement bounded cache with distance-weighted LRU behind the same
  interface.
- **Reason:** Without in-flight suppression, boundary jitter (even
  with hysteresis) can still issue duplicate prefetches when the
  first hasn't completed. Cache hit/miss observability must exist now
  so KPI reporting in Phase 9 has stable schema; only the **policy**
  (eviction) is deferred.
- **Alternatives considered:**
  - No tracking at all — rejected: visible duplicate prefetches in
    demo, audience asks "why".
  - Full bounded cache now — rejected: crosses Phase 8 scope, design
    will get rewritten.
- **Implications:** Phase 8 implements the eviction policy and bounded
  capacity; the interface stays the same.
- **Date:** 2026-05-16

### Decision: Migrate scenarios — replace `weight_prefetches` with `npcs`

- **Choice:** `Scenario.weight_prefetches` is removed from the schema.
  `demo.yaml` and `stress.yaml` are rewritten to use `npcs:` waypoint
  syntax. `ScriptedPredictor` is retained as a baseline that just
  loads every NPC at LOD0 ignoring distance (the "no-LOD" control
  group for A/B); it no longer reads `weight_prefetches`.
- **Reason:** A single scenario schema is easier to teach and
  document. The old field becomes dead immediately after Phase 4 —
  every future scenario will be written in `npcs` form. Keeping both
  doubles documentation and creates ambiguous interaction rules when
  both are present.
- **Alternatives considered:**
  - Keep both schemas — rejected: dead schema rots, schema bloat.
  - Drop `ScriptedPredictor` entirely — rejected: lose the no-LOD
    baseline in the 2×2 A/B (scripted/lod × fifo/qos).
- **Implications:** Both demo scenarios are rewritten this phase.
  `ScriptedPredictor` is updated to traverse `npcs` and issue LOD0
  prefetches for every NPC at scenario start (or first waypoint).
- **Date:** 2026-05-16

---

## Phase 5 — Zero-Copy Neuro DMA

### Decision: Path naming — `neuro_dma` (not `p2p`)

- **Choice:** The dedicated SSD→NPU weight-loading path is named
  `neuro_dma` in `config.yaml` and source. The bounce path keeps the
  name `bounce`. Industry uses generic terms ("P2PDMA", "GPUDirect
  Storage", "SmartAccess Storage"); `neuro_dma` is a project-local
  name that pins the target to NPU and avoids confusion with NPU-
  internal DMA.
- **Reason:** "P2P DMA" or "NPU DMA" both invite ambiguity — the
  former is generic, the latter clashes with the NPU's internal DMA
  engines (which exist but are out of scope here). `neuro_dma`
  unambiguously refers to "the path that streams neural-network
  weights from SSD into NPU memory bypassing CPU".
- **Alternatives considered:**
  - `p2p` — rejected: generic, doesn't pin to NPU target.
  - `npu_dma` — rejected: collides with intra-NPU DMA terminology.
  - `weight_dma` — rejected: less aligned with project name.
- **Implications:** `config.dma.path` accepts `"bounce"` or
  `"neuro_dma"`. Code/comments/docs use this name consistently.
- **Date:** 2026-05-16

### Decision: Bounce-path service model — three-segment formula

- **Choice:** Bounce-path service time is modeled as
  `size/bus + size/memcpy + size/bus`, not a flat `2 × size/bus`.
  Three separate stages: SSD→DRAM (bus), CPU memcpy (DRAM bandwidth),
  DRAM→NPU (bus). The `memcpy_bandwidth_mbps` is a tunable config
  defaulting to 12 GB/s, representative of typical console-class
  single-thread memcpy (Zen 2 / Zen 3 with DDR4 or GDDR6, lmbench
  STREAM Triad in the 10–15 GB/s range).
- **Reason:** Flat `2×` is between completely-serialized and
  perfectly-pipelined; the three-segment fully-serialized formula is
  closer to the worst-case real measurement (~20 ms vs 12.5 ms vs
  8 ms for 100 MB), aligns with the "we are pessimistic about
  baseline" narrative, and surfaces CPU memcpy as a first-class cost
  the user can vary.
- **Alternatives considered:**
  - Flat `2 × size/bus` — rejected: too optimistic, underestimates
    bounce path's actual cost by ~30 %.
  - Fully-pipelined `max()` — rejected: too optimistic, real systems
    rarely achieve full overlap due to cache pressure and coherency.
- **Implications:** `config.dma.bounce.memcpy_bandwidth_mbps` exists
  and must be set. Tests assert the three-segment service time.
- **Date:** 2026-05-16

### Decision: CPU cycle accounting via per-completion increment

- **Choice:** A new KPI counter `cpu_cycles_used` accumulates per
  completion based on path:
  - `bounce` path: `size_bytes × cycles_per_byte` (default 3 cycles/B,
    representing CPU memcpy + L3 cache pressure)
  - `neuro_dma` path: `setup_cost_cycles` only (default 1,000 cycles
    per transaction for SGL descriptor build)
- **Reason:** Standard practice (Linux perf counter, ARM PMU). The
  zero-copy win is "millions of cycles versus a few thousand" — that
  ratio is the headline KPI for Pillar C.
- **Alternatives considered:**
  - Model CPU as a queueing resource — rejected: out of scope, we
    don't simulate game-logic CPU load.
  - Don't model cycles at all — rejected: loses the main story.
- **Implications:** `Scheduler::Kpi` grows one field. Phase 9 report
  formats this as "MB-equivalent" or "ms-of-CPU-time" for readability.
- **Date:** 2026-05-16

### Decision: Path selection is global per run (not per-transaction)

- **Choice:** `config.dma.path` selects `bounce` or `neuro_dma` for
  the entire run. A/B is achieved by running the simulator twice with
  different paths. Per-transaction path selection is **backlog**.
- **Reason:** Same shape as `scheduler.policy` A/B (Phase 3). Lets
  reports diff two complete traces cleanly. Per-transaction would
  require additional decision logic (size threshold, NPU memory
  pressure, etc.) that belongs in a future smart-path predictor.
- **Alternatives considered:**
  - Per-transaction — deferred to backlog; needs intent/state
    awareness that Phase 5 doesn't have.
- **Implications:** Backlog entry "Per-transaction DMA path
  selection" is added to `PROJECT_PLAN.md`. CLI gains `--dma`.
- **Date:** 2026-05-16

### Decision: SGL is conceptual + drives quantum on neuro_dma path

- **Choice:** `neuro_dma` path's quantum equals the SGL entry size
  (default 1 MB → 62.5 µs at 16 GB/s). `bounce` path keeps the original
  100 µs quantum. Transactions are **not** physically split into child
  sub-transactions — `sgl_entries` is a trace label = `size /
  sgl_entry_bytes`, recorded for narrative purposes only.
- **Reason:** Smaller quantum = finer preemption granularity =
  better audio latency tail. Implementing real SGL splitting would
  require parent-child transaction relationships throughout the
  scheduler (Phase 3 architecture rewrite) for a single-channel bus
  with marginal additional gain. The variable quantum captures 80 %
  of the benefit at 5 % of the implementation cost.
- **Alternatives considered:**
  - Real SGL splitting (parent transaction → N child transactions)
    — rejected: large Phase 3 refactor; benefit only visible with
    multi-channel bus (out of scope).
  - Concept-label only (no quantum change) — rejected: leaves a
    measurable improvement on the table.
- **Implications:** `Scheduler` reads quantum from `cfg.dma.path` not
  `cfg.scheduler.quantum_us` for `neuro_dma`. Trace schema v2 adds
  `dma_path` byte (replacing one of the v1 reserved bytes) and
  `sgl_entries` count is reportable from CSV.
- **Date:** 2026-05-16

### Decision: Only weight transactions use the DMA-path switch

- **Choice:** Audio and texture transactions always use the existing
  service model (single bus pass at quantum 100 µs). Only weight
  transactions branch on `cfg.dma.path`.
- **Reason:** Audio and texture have their own DMA engines in real
  hardware (audio mixer DMA, graphics DMA) — neither involves the
  SSD→NPU path. Putting them on `neuro_dma` would be physically
  meaningless and would muddy the A/B comparison.
- **Alternatives considered:**
  - All traffic switches — rejected: physically wrong.
  - Texture also branches — deferred to Phase 7 when decompressor
    is added (texture also goes through Kraken).
- **Implications:** `Transaction::source == Weight` is the branch
  condition. Audio/texture tests are unaffected.
- **Date:** 2026-05-16

---

## Phase 8 — Multi-core NPU, Eviction, Degradation

### Decision: NPU core count default = 4

- **Choice:** `config.yaml` ships with `npu.cores: 4` as the default.
- **Reason:** N=2 is too small to surface meaningful contention on the
  shared cache; N≥8 produces noisy reports without proportional insight.
  N=4 is the sweet spot for demonstrating eviction-policy behavior and
  per-core fairness without overwhelming the visualization.
- **Alternatives considered:**
  - N=2 — rejected: contention story too weak.
  - N=8 — rejected: report clutter, diminishing return.
- **Implications:** All Phase 8 KPIs report per-core breakdown; tests
  parameterize on N to ensure correctness at boundary values (1, 4, 8).
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
