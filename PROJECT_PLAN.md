# NeuroStream — Project Plan

> A behavioral simulator demonstrating SSD↔NPU co-design for seamless AI model streaming in a console-class game environment.

## Vision

In a resource-constrained console (PS5-class) environment, prove that AI model weights can be hot-swapped at runtime **without** stealing bandwidth from audio, textures, or input — by treating the I/O subsystem as a QoS-managed network rather than a dumb pipe.

## Target Audience / Why It Matters

Designed as a portfolio piece for **Sony Interactive Entertainment (SIE)** and similar console / system-architecture roles. Demonstrates:

- Cross-layer optimization (driver ↔ scheduler ↔ application)
- Awareness of PS5's custom I/O complex (DMA, Kraken decompression, coherency engines)
- Ability to translate hardware capability into a software protocol

---

## Architecture Overview

```
[Game Logic Sim]
       │
       ▼
[Spatial Predictor] ──► [LOD Manager] ──► [Priority Tagger]
                                                  │
[Audio Traffic]    ──┐                            ▼
[Texture Traffic]  ──┼──────────────► [Virtual AXI Scheduler (QoS)]
[AI Weight Traffic]──┘                            │
                                                  ▼
                                       [DMA Engine + Decompressor]
                                                  │
                                                  ▼
                                       [Multi-core NPU + Shared Cache + Eviction]
                                                  │
                                                  ▼
                                       [Metrics Collector → CSV]
```

---

## Three Core Pillars (Original)

| Pillar | Role | Key Concept |
|--------|------|-------------|
| **A. Virtual AXI Bus Scheduler** | Traffic Cop | QoS tags, bandwidth arbitration, priority preemption |
| **B. Weight LOD Manager** | Smart Librarian | Distance-based weight resolution, quantized variants |
| **C. Zero-Copy P2P DMA** | Express Lane | Scatter-Gather List, CPU-bypass, direct SSD→NPU |

## Extended Pillars (Added in Review)

| Pillar | Role | Why |
|--------|------|-----|
| **D. Spatial Predictor** | Crystal Ball | Prefetch by frustum + velocity, not just current distance |
| **E. On-the-fly Decompressor** | Kraken-class block | Realistic SSD→NPU path; otherwise zero-copy is a lie |
| **F. Cache Eviction Policy** | Bouncer | Distance-weighted LRU on NPU cache |
| **G. Graceful Degradation** | Safety Net | Fallback to stale LOD on timeout — never block frame |

---

## KPIs (Success Metrics)

Each experiment must report:

- **P99 audio packet latency** (μs) — must stay under 1ms
- **Frame time variance** (ms) — target < 0.5ms stddev
- **AI weight swap-in latency** (ms) — measure cold vs warm
- **Bus utilization** (%) — peak and sustained
- **CPU cycles saved** by zero-copy vs baseline copy path
- **Cache hit rate** on NPU weight cache

A run is "successful" if NeuroStream-mode beats baseline-mode on audio P99 **without** regressing AI swap latency by more than 25%.

---

## Phase Plan

Status legend: `[ ]` not started · `[~]` in progress · `[x]` done

### Phase 0 — Bootstrap `[x]`
- [x] Repo init, license, `.gitignore`, `README.md` skeleton
- [x] Build system (CMake, C++20, `yaml-cpp`, `doctest`)
- [x] CI stub (GitHub Actions: build + run smoke test)
- [x] Folder layout: `src/`, `include/`, `sim/`, `scenarios/`, `docs/`, `scripts/`
- [x] Top-level `config.yaml` with initial system parameters

### Phase 1 — Simulation Core `[x]`
- [x] Discrete-event simulator skeleton (event-driven, μs resolution)
- [x] `Clock`, `Event`, `EventQueue` primitives (with tombstone cancellation)
- [x] `Config` loader (YAML → typed struct, fail-fast on missing fields)
- [x] Trace logger (binary + CSV, schema v1, streaming writes)
- [x] Unit-test harness (`doctest`)

### Phase 2 — Traffic Injectors + Dumb Predictor `[x]`
- [x] `AudioTrafficGen` — steady 5 MB/s, 256B packets, hard deadline
- [x] `TextureTrafficGen` — bursty up to 500 MB/s, 256 KB blocks
- [x] `AIWeightInjector` — on-demand prefetch, LOD-sized
- [x] Each tagged with `{priority, deadline, size}`
- [x] `Predictor` interface + `ScriptedPredictor` (real one in Phase 6)
- [x] Scenario YAML schema documented in `docs/scenario-schema.md`

### Phase 3 — Pillar A: Virtual AXI Scheduler `[x]`
- [x] Baseline FIFO scheduler (control group)
- [x] QoS-aware weighted round-robin (virtual-time WFQ on bulk classes)
- [x] Priority preemption with deadline awareness (quantum-based, 100 μs)
- [x] Bandwidth budget per QoS class (token-bucket on Critical, 5%)
- [x] Compare-mode switch via `--policy` and `--ab` for A/B reports

### Phase 4 — Pillar B: Weight LOD Manager `[x]`
- [x] Define LOD tiers (LOD0/1/2 as **function tiers**, not quality grades)
- [x] Distance → LOD mapping function (discrete bands in `config.yaml`)
- [x] LOD switch hysteresis (20% deadband + conservative cold-start)
- [x] Mock NPC scene with N agents at varying distances (`scenarios/world.yaml`)
- [x] In-flight tracking + cache hit/miss interface (eviction deferred to Phase 8)
- [x] `Predictor` factory + `predictor.policy` config switch (`scripted`/`lod`)
- [x] Migrate scenarios: replace `weight_prefetches` with `npcs:` waypoints

### Phase 5 — Pillar C: Zero-Copy Neuro DMA `[x]`
- [x] Model two transfer paths for weights only:
      `bounce` (3-segment: bus + memcpy + bus → effective 4.8 GB/s)
      vs `neuro_dma` (single-pass full 16 GB/s + SGL quantum)
- [x] Variable quantum: bounce = 100 µs; neuro_dma = 62 µs (1 MB SGL)
- [x] CPU cycles KPI: `cpu_cycles_used` per completion
- [x] Trace schema v2: `dma_path` byte + `sgl_entries` field
- [x] `--dma bounce|neuro_dma` CLI flag; A/B is now 2×2 (policy × dma)
- [x] Audio/texture unaffected (`SourceKind` branch in scheduler)

### Phase 6 — Extended Pillar D: Intent-Aware Predictor `[x]`
> Broader than originally framed. Distance alone is a weak interaction
> signal — see `Analysis_Report.md` §5b. Phase 6 replaces distance-only
> LOD with intent-aware prediction.
- [x] Player kinematics: 2D position + velocity + facing in scenario v3
- [x] Hierarchical 8-rule cascade (replaces probability blend per
      industry convention: state machines, not weighted sums)
- [x] Velocity look-ahead (500 ms) — uses min(current_dist, future_dist)
- [x] Frustum FOV cone (not raw facing dot product)
- [x] Explicit interact event with `duration_ms` (commit-driven LOD0)
- [x] Per-NPC priority override (`quest` → always LOD0)
- [x] NPC velocity derived from waypoints; used in rule 5 (approaching)
- [x] Scenario schema v3 with explicit `schema_version` field; v1/v2
      rejected with migration hints
- [x] New `town.yaml` scenario demonstrating §5b fix
- [x] Extra scenarios for coverage: `look_around` (rotation), `mixed`
      (all 8 cascade rules), `backward` (velocity reversal),
      `interrupted` (limitation doc)
- [x] Boundary unit tests (close/near/visible distance edges, frustum
      half-angle, stopped speed inclusivity, interaction window edges)
- [ ] Eviction signal when NPC leaves predicted set — deferred to Phase 8

### Phase 7 — Extended Pillar E: Decompressor `[x]`
- [x] `compression.path: none | cpu | inline_hw` as independent axis
- [x] Configurable ratios (weight_ratio, texture_ratio); default 2.0
- [x] CPU fallback path with explicit `decompress_bandwidth_mbps` cap
      (~1.5 GB/s software Zstd) and `decompress_cycles_per_byte` for KPI
- [x] Hardware inline decompressor with `decompressor_bw_mbps` cap
      (Kraken-class) and setup_cost_cycles
- [x] `decompress_cycles_used` KPI counter (separate from cpu_cycles_used)
- [x] CLI `--compression` flag; 5-level improvement ladder in `--ab`
- [x] Audio explicitly unaffected (own codec path)

### Phase 8 — Multi-core NPU, Eviction, Degradation (Pillar F/G) `[x]`
- [x] Bounded `NpuCache` with capacity from `npu.shared_cache_mb`
- [x] `EvictionPolicy` interface + `DistanceLruPolicy` (distance descending,
      LRU tiebreak)
- [x] Pin/unpin refcount on cache entries — interaction-active LODs survive
      eviction
- [x] N-slot multi-core model (`npu.cores`); 5th simultaneous interaction
      triggers `CoreSaturation` event
- [x] Graceful degradation: best-resident-LOD fallback on miss; emits
      `Degrade` event; if no LOD at all → `degradations_no_weight++` (frame
      still continues — Pillar G promise verified)
- [x] Trace `EventType` extended with `Evict` (6) and `Degrade` (7); CSV
      gains corresponding strings; schema layout unchanged (still v2)
- [x] `PredictorKpi` struct with `cache_hits/misses/evictions/refusals`
      and `degradations_total/no_weight` and `core_saturation_events`
- [x] `scenarios/cache_pressure.yaml` + `scenarios/degrade.yaml` for
      Pillar F/G demos

### Phase 9 — Reporting `[x]`
- [x] `results/<scenario>/` directory layout with summary.md / summary.csv
- [x] `ReportWriter` module — markdown + CSV from Scheduler::Kpi / PredictorKpi
- [x] Improvement multipliers vs baseline (1× / Nx better / -Nx)
- [x] `--diff a.csv b.csv` subcommand
- [x] P99 storage: `vector<Time>` → 50-bucket log-spaced histogram
- [x] Per-scenario sibling README (`scenarios/<name>.md`) × 6
- [x] Tests for report formatting + histogram (report_test, 8 cases)

### Phase 10 — Scenarios & Demo `[ ]`
- [ ] Scenario 1: Quiet world (sanity check)
- [ ] Scenario 2: Combat burst (textures spike + weight swap)
- [ ] Scenario 3: Open-world traversal (sustained prefetch churn)
- [ ] Recorded demo run with annotated charts in `docs/results/`

### Phase 11 — Documentation & Polish `[ ]`
- [ ] Architecture doc with diagrams (`docs/architecture.md`)
- [ ] Design rationale doc — why each policy chosen (`docs/rationale.md`)
- [ ] One-page summary tailored to SIE audience (`docs/sie-pitch.md`)
- [ ] Final README with quickstart and result snapshots

---

## Backlog / Deferred Enhancements

Items raised during design that we decided **not** to do now but want
revisited later. Each entry names the phase that would pick it up.

- **Separate read / write channels on the bus** — real AXI/CHI runs
  read and write on independent channels (so concurrent R+W don't
  block each other). Phase 3 ships with a single serialized bus
  because Phase 3 traffic is read-dominated. Phase 5 (Zero-Copy DMA)
  introduces the SSD→NPU path, at which point splitting AR/AW
  channels becomes meaningful for the zero-copy story. **Pickup:
  Phase 5.**

- **Per-transaction DMA path selection** — Phase 5 ships with global
  `dma.path` (one path per run, A/B by re-running). A real system
  picks per-transaction based on size, NPU memory pressure, and
  application hints (e.g. tiny weights → bounce because setup
  overhead dominates; large weights → neuro_dma). Needs an intent
  layer that overlaps with Phase 6 predictor work. **Pickup: Phase 6
  or later.**

- **Physical SGL splitting (parent → N child transactions)** — Phase 5
  uses variable quantum (62.5 µs on neuro_dma) to approximate the
  preemption benefit of SGL granularity without restructuring
  scheduler internals. Real SGL splitting becomes valuable once a
  multi-channel bus is in place (one channel per SGL entry can run
  in parallel). **Pickup: alongside multi-channel bus work.**

---

## Out of Scope (Explicit Non-Goals)

- Real kernel driver code — this is a behavioral simulator
- Real NPU inference — weights are opaque blobs sized realistically
- GPU rendering — texture traffic is modeled as bandwidth only
- Networking / multiplayer
- Per-core private NPU caches (shared cache only; private caches deferred)
- Python tooling in the build (CSV may be analyzed externally)

---

## Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| Simulator becomes a toy that proves nothing | Lock KPIs in Phase 1; every PR must move a KPI |
| Over-engineering the event system | Start with simplest priority queue; only optimize if profiling demands it |
| Results look good but aren't reproducible | Seed all RNG; commit scenario configs alongside results |
| "It's just a sim" objection in interviews | Document every assumption against published PS5 I/O complex talks |

---

## Repository

`https://github.com/egan-wu/NeuroStream.git`
