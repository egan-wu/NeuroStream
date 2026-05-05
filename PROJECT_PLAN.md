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

### Phase 1 — Simulation Core `[ ]`
- [ ] Discrete-event simulator skeleton (event-driven, μs resolution)
- [ ] `Clock`, `Event`, `EventQueue` primitives
- [ ] `Config` loader (YAML → typed struct, injectable for tests)
- [ ] Trace logger (binary + CSV exporter)
- [ ] Unit-test harness (`doctest`)

### Phase 2 — Traffic Injectors + Dumb Predictor `[ ]`
- [ ] `AudioTrafficGen` — steady 5 MB/s, 256B packets, hard deadline
- [ ] `TextureTrafficGen` — bursty up to 500 MB/s, large blocks
- [ ] `AIWeightRequest` — on-demand prefetch, variable size
- [ ] Each tagged with `{priority, deadline, size}`
- [ ] `Predictor` interface + scripted/dumb implementation (real one in Phase 6)
- [ ] Scenario YAML schema documented in `docs/scenario-schema.md`

### Phase 3 — Pillar A: Virtual AXI Scheduler `[ ]`
- [ ] Baseline FIFO scheduler (control group)
- [ ] QoS-aware weighted round-robin
- [ ] Priority preemption with deadline awareness
- [ ] Bandwidth budget per QoS class
- [ ] Compare-mode switch (FIFO vs QoS) for A/B reports

### Phase 4 — Pillar B: Weight LOD Manager `[ ]`
- [ ] Define LOD tiers (LOD0=100MB, LOD1=30MB, LOD2=10MB)
- [ ] Distance → LOD mapping function
- [ ] LOD switch hysteresis (avoid thrashing on boundary)
- [ ] Mock NPC scene with N agents at varying distances

### Phase 5 — Pillar C: Zero-Copy P2P DMA `[ ]`
- [ ] Model two transfer paths: `bounce-buffer` vs `p2p-dma`
- [ ] Scatter-Gather List builder
- [ ] Account for CPU cycles consumed in each path
- [ ] Verify NPU receives identical bytes in both paths (correctness)

### Phase 6 — Extended Pillar D: Spatial Predictor `[ ]`
- [ ] Player kinematics model (position, velocity, view frustum)
- [ ] Predicted-intersection horizon (e.g. 2s lookahead)
- [ ] Prefetch queue feeding LOD Manager
- [ ] Eviction signal when NPC leaves predicted set

### Phase 7 — Extended Pillar E: Decompressor `[ ]`
- [ ] Inline decompression stage between DMA and NPU cache
- [ ] Configurable ratio (2:1) and per-block latency
- [ ] Optional CPU-fallback path to show contrast

### Phase 8 — Multi-core NPU, Eviction, Degradation `[ ]`
- [ ] Multi-core NPU model: N execution units, per-core request queue
- [ ] Shared weight cache across cores
- [ ] Distance-weighted LRU on NPU cache
- [ ] Timeout-driven fallback to previous LOD tier
- [ ] "Never block the frame" guarantee — assert in tests
- [ ] Per-core and aggregate KPI reporting

### Phase 9 — Reporting `[ ]`
- [ ] CSV schema for per-event trace
- [ ] KPI summary written to markdown after each run
- [ ] Side-by-side baseline vs NeuroStream report (CSV diff helper in C++)

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
