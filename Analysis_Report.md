# NeuroStream — Analysis Report

> Snapshot as of Phase 4 completion (commit `1c93a48`).
> This document consolidates project intent, planned trajectory, completed
> work, decision rationale, and per-phase results in a single place.
> Companion files: [`PROJECT_PLAN.md`](PROJECT_PLAN.md) (live roadmap),
> [`Design_Detail.md`](Design_Detail.md) (decision log).

---

## 1. Project Purpose

### One-Sentence Statement

> **Prove that AI model weights can be hot-swapped at runtime in a
> console-class system without disturbing audio, textures, or input —
> through software-hardware co-design of the I/O subsystem.**

### The Problem

Next-generation console games want to integrate AI (NPC dialogue, behavior,
animation). AI models are large (10 MB – 100 MB+) and live on SSD. The
console's bus, SSD, and NPU memory are already saturated by audio
streaming, texture loading, and input. Naively loading AI weights causes:

- Audio dropouts (humans hear < 1 ms gaps)
- Frame-time spikes (visible stutter)
- Bus contention with texture streaming

NeuroStream models the I/O subsystem behaviorally and demonstrates a
protocol stack — QoS-aware scheduling, function-tier LOD, predictive
prefetch, zero-copy DMA — that solves this problem.

### Relation to PlayStation 5

The PS5's competitive differentiator is its custom I/O complex:

- 5.5 GB/s raw SSD with hardware Kraken decompression
- 6-level priority I/O coprocessor
- Dedicated DMA engines (CPU-bypass)
- Coherency engines

Mark Cerny's *Road to PS5* talk spent 30 minutes on I/O — not GPU, not CPU.
Sony bet that data-movement cost is the next-generation bottleneck.

NeuroStream's message to a Sony Interactive Entertainment audience:

> "I understand why your hardware is shaped this way, and I designed a
> behavioral protocol that lets it be fully exploited under AI workloads
> while guaranteeing no frame is dropped."

| PS5 Hardware | NeuroStream Software Layer |
|--------------|---------------------------|
| 6-level priority I/O | **Pillar A**: Virtual AXI Scheduler (QoS) |
| Kraken decompressor | **Pillar E**: On-the-fly Decompressor model (Phase 7) |
| Dedicated DMA + coherency | **Pillar C**: Zero-Copy P2P DMA (Phase 5) |
| Custom SSD controller | Bus model + bandwidth budgeting |
| *(not in PS5; next-gen)* | **Pillar B/D**: LOD Manager + Spatial Predictor |

---

## 2. Architecture

```
[Game Logic / Scenario]
        │
        ▼
[Spatial Predictor]  ──►  [LOD Manager]  ──►  [Priority Tagger]
                                                    │
[Audio Traffic]    ──┐                              ▼
[Texture Traffic]  ──┼──────────────►  [Virtual AXI Scheduler (QoS)]
[AI Weight Traffic]──┘                              │
                                                    ▼
                                     [DMA Engine + Decompressor]
                                                    │
                                                    ▼
                                     [Multi-core NPU + Shared Cache + Eviction]
                                                    │
                                                    ▼
                                     [Metrics Collector → CSV / Binary Trace]
```

### Pillar Catalog

| Pillar | Role | Phase |
|--------|------|------|
| A. Virtual AXI Bus Scheduler | QoS arbitration, audio protection | 3 ✅ |
| B. Weight LOD Manager | Function-tier model streaming by distance | 4 ✅ |
| C. Zero-Copy P2P DMA | SSD→NPU bypass, CPU-cycle savings | 5 |
| D. Spatial Predictor | Frustum/velocity-aware prefetch | 6 |
| E. On-the-fly Decompressor | Kraken-class compression model | 7 |
| F. Cache Eviction Policy | Distance-weighted LRU on NPU cache | 8 |
| G. Graceful Degradation | Stale-LOD fallback on timeout | 8 |

---

## 3. Phase Progress

| Phase | Title | Status | Completed |
|------:|-------|:------:|-----------|
| 0 | Bootstrap | ✅ | CMake, CI, license, layout, config.yaml |
| 1 | Simulation Core | ✅ | DES, Clock, EventQueue, Config, TraceWriter |
| 2 | Traffic Injectors + Dumb Predictor | ✅ | Audio/Texture/Weight injectors, ScriptedPredictor |
| 3 | Virtual AXI Scheduler (Pillar A) | ✅ | FIFO baseline, QoS two-tier, A/B mode |
| 4 | Weight LOD Manager (Pillar B) | ✅ | LodPredictor, function-tier LOD, hysteresis, in-flight tracking |
| 5 | Zero-Copy P2P DMA (Pillar C) | ⏳ | |
| 6 | Spatial Predictor (Pillar D) | ⏳ | |
| 7 | Decompressor (Pillar E) | ⏳ | |
| 8 | Multi-core NPU + Eviction + Degradation (Pillar F/G) | ⏳ | |
| 9 | Reporting | ⏳ | |
| 10 | Scenarios & Demo | ⏳ | |
| 11 | Documentation & Polish | ⏳ | |

### Current Code Footprint

| Category | Lines |
|---------|------:|
| `include/` + `src/` C++20 | 1,623 |
| `tests/` (doctest) | 835 |
| YAML (scenarios + test fixtures) | 281 |
| **Test cases** | **43** |
| **Assertions** | **614** |
| **Test pass rate** | **100% (8 suites)** |

---

## 4. Decision Catalog

Every architectural choice in NeuroStream was made against a backdrop of
"what do real commercial systems do?" The table below summarizes 36 logged
decisions. Each row links to its full entry in `Design_Detail.md`.

### Phase −1 (Foundation, cross-cutting)

| Decision | Choice | Rejected Alternatives & Why |
|----------|--------|-----------------------------|
| Time model | Discrete-event sim | Fixed 1 µs tick — 100× slower, no fidelity gain at L2 |
| Language | C++20 + CMake + doctest + yaml-cpp | SystemC (RTL-grade, wrong layer); Rust (no project benefit); Python (split build) |
| Bus abstraction | L2 transaction-level | L1 bandwidth-only (cannot show QoS); L3 beat-level (RTL territory) |
| Parameter store | `config.yaml` (hardware constants only) | Hardcoded (kills experimentation); CLI flags (too many knobs) |
| Scenario format | `scenarios/*.yaml` (per-experiment) | JSON (less readable); hardcoded in C++ (poor demo) |
| Policy injection | Strategy pattern, runtime-swappable | Compile-time templates (slower iteration); separate binaries (drift) |
| Predictor phasing | Dumb scripted in Phase 2, smart in Phase 6 | Phase-6-only (Phases 3-5 unblocked needed) |
| Multi-core NPU | In scope (default 4 cores) | Single-core (loses contention story) |
| File split | `config.yaml` vs `scenarios/*.yaml` | Combined YAML (drift risk, bloat) |

### Phase 0 (Bootstrap)

| Decision | Choice | Rejected & Why |
|----------|--------|----------------|
| License | MIT | Apache-2.0 (patent noise); none (deters reviewers) |
| CI matrix | Ubuntu/GCC + macOS/Clang | Ubuntu-only (dev regressions invisible); +Windows/MSVC (C++20 quirks, time sink) |

### Phase 1 (Simulation Core)

| Decision | Choice | Rejected & Why |
|----------|--------|----------------|
| Event cancellation | Tombstone flag on entry | True heap-remove (complex, higher constant factor); no cancellation (forces workarounds Phases 6/8) |
| Time type | `int64_t` µs, no chrono | `chrono::microseconds` (verbose, harder to serialize); `double` seconds (FP ordering hazards) |
| Config loader | Fail-fast, name the missing path | Silent defaults (hides drift bugs) |
| Trace format | Binary 32-byte records + CSV mirror | CSV only (size/throughput); binary only (no eyeballing) |
| Trace schema v1 | Fixed `(ts, src_id, type, qos, size, lat, txn_id)` | Variable-size TLV (unnecessary complexity) |
| Trace I/O | Streaming write via RAII | Buffer-then-dump (RAM pressure, crash loss) |

### Phase 2 (Traffic + Predictor)

| Decision | Choice | Rejected & Why |
|----------|--------|----------------|
| Injector model | **Push** (actor schedules its own events) | Pull (parallel driver loop, awkward concurrency) |
| Sink abstraction | `TransactionSink` interface | Direct enqueue to global queue (ties injectors to scheduler impl) |
| Scenario time | Declarative streams + absolute events | All-declarative (bursts awkward); all-event (audio thousands of entries) |
| Phase 2 predictor | Scripted (reads `weight_prefetches`) | Defer to Phase 6 (leaves 3–5 without weight traffic) |
| Scenario loader | Fail-fast | Optional defaults (same hazards as config) |
| Texture block | 256 KB, in `config.yaml` | 64 KB (~760 events/burst, noisy); 1 MB (~50 events, too coarse) |

### Phase 3 (Pillar A: Scheduler)

| Decision | Choice | Rejected & Why |
|----------|--------|----------------|
| Service model | **Quantum-based, 100 µs** | Whole-transaction non-preemptive (baseline too pessimistic); beat-level (violates L2) |
| QoS policy | **Two-tier**: critical strict + DRR bulk | Strict priority only (starves bulk); pure DRR (audio loses) |
| Bus model | Single serialized channel | Parallel lanes (overly optimistic, hides bottleneck) |
| Trace milestones | Issue / Arrive / ServiceStart / Complete / Drop | More events (clutter); fewer (cannot derive KPIs) |
| A/B mechanism | `--policy fifo\|qos` CLI flag | Two binaries (drift risk) |

### Phase 4 (Pillar B: LOD Manager)

| Decision | Choice | Rejected & Why |
|----------|--------|----------------|
| **LOD semantics** | **Function tiers** (dialogue / behavior / bark NN) | Quality tiers of one big LLM (technically wrong); binary load/unload (kills LOD pillar) |
| Distance mapping | Discrete bands in `config.yaml` | Continuous (weights cannot interpolate); five+ bands (without per-NPC priority, no value) |
| Hysteresis | 20% deadband + conservative cold-start | Dwell-time only (slow for legitimate motion); none (60 Hz oscillation = 7.8 GB/s waste, deadlock) |
| NPC motion | Scripted waypoints + linear interpolation | Random walk (not reproducible); velocity-only (hard to express turns) |
| LOD tick | 60 Hz (16,667 µs) | 1 ms (noisy trace, no benefit); event-driven (complex with linear waypoints) |
| Cache model | In-flight tracking now; bounded eviction in Phase 8 | No tracking (visible duplicate prefetches); full cache now (crosses Phase 8 scope) |
| Scenario migration | Replace `weight_prefetches` with `npcs` | Both schemas (dead schema rots); drop `ScriptedPredictor` (lose baseline) |

### Phase 8 (Multi-core, Pre-locked)

| Decision | Choice | Rejected & Why |
|----------|--------|----------------|
| NPU core count default | 4 | N=2 (no contention story); N=8 (report clutter) |

---

## 5. Per-Phase Results & Analysis

### Phase 0 — Bootstrap

- **Output**: build system + CI green on day one
- **Notable friction**: `yaml-cpp` 0.8.0's `cmake_minimum_required(2.x)`
  was incompatible with new CMake; resolved with
  `CMAKE_POLICY_VERSION_MINIMUM=3.5`
- **Insight**: external dep version policy is a recurring source of CI
  fragility; document it now (in `CMakeLists.txt` comment) saves future
  debugging

### Phase 1 — Simulation Core

- **Output**: ~600 LOC of generic DES infrastructure that every later
  phase reuses
- **Quantitative**: 25 test assertions cover ordering, tie-break,
  cancellation, peek, clock invariants, re-entrant scheduling, config
  validation, trace round-trip
- **Insight**: a small API bug was caught only by writing tests for it:
  `pop_and_run` originally returned `(when, handler)` and the caller
  advanced the clock — but handlers reading `clock.now()` saw stale time.
  The fix (have `pop_and_run` advance the clock *before* invoking the
  handler) is now codified in `tests/event_queue_test.cpp` ("handler
  observes correct clock time when it fires")

### Phase 2 — Traffic Injectors + Dumb Predictor

- **Output**: 5 sec demo scenario produces ≈ 98 k transactions
  (98,039 audio + 191 texture + 2 weight)
- **Verification**: each injector's transaction count matches analytical
  expectation (audio at 51 µs period; texture 256 KB blocks at 500 MB/s)
- **Insight**: the **push model** integration with `EventQueue`
  produced ~13 % of the project's total bus throughput in a single
  one-line `schedule_next` re-arm. Pull would have required a separate
  N-way merge loop.

### Phase 3 — Virtual AXI Scheduler (Pillar A) — *Critical Result*

The **stress scenario** (10 NPCs entering LOD0 simultaneously + sustained
texture burst):

| Policy | Audio Drops | Audio P99 Latency | Audio Mean Latency | Weight Max Latency |
|--------|------------:|------------------:|-------------------:|-------------------:|
| **FIFO (baseline)** | **416** | 995 µs | 36.2 µs | 67,987 µs |
| **QoS (NeuroStream)** | **0** | 86 µs | 3.6 µs | 68,455 µs |

**Reading**:
- QoS eliminates *all* audio drops while sacrificing < 1 % weight latency
- Audio P99 improves 11×; mean improves 10×
- **This validates the headline claim** of the project — that QoS
  scheduling protects audio under bandwidth pressure with negligible
  cost to bulk transfers

**Why FIFO fails (the engineering story)**:
The implementation re-queues unfinished transactions to the tail, so
FIFO behaves like quantum-RR rather than head-of-line-blocking FIFO.
Audio dropouts come not from a single 100 MB transaction starving the
queue, but from **head-of-queue accumulation** — when 10 bulk
transactions arrive at t=500ms, an audio packet arriving at t=520ms
sits behind 10 × 100 µs = 1 ms of bulk service before its 1 ms
deadline. Audio loses by accumulation, not by a single hog.

**Why QoS works**:
Two-tier policy: critical (audio) is strictly preferred but
rate-limited by a token bucket (5 % of bandwidth = 800 B/µs refill,
8 MB capacity). Bulk (weight + texture) shares the remaining bandwidth
via virtual-time WFQ with weights 2:1. In normal audio rates
(5 MB/s = 5 B/µs), tokens refill 160× faster than they are consumed,
so the bucket stays full — the rate-limiter is purely a safety net
against buggy injectors flooding critical class.

### Phase 4 — Weight LOD Manager (Pillar B) — *Critical Result*

The **world scenario** (10 NPCs at varied distances: 2 close, 3 mid,
5 far):

| Predictor | Weight Bytes (QoS run) | LOD0 count | LOD1 count | LOD2 count |
|-----------|----------------------:|-----------:|-----------:|-----------:|
| **ScriptedPredictor** (baseline) | **1,000 MB** | 10 | 0 | 0 |
| **LodPredictor** (NeuroStream) | **450 MB** | 2 | 5 | 10 |

**Reading**: distance-aware prefetch saves **55 % of weight bus
traffic** on a representative open-world scene.

**Why LOD works** (the function-tier insight):

The breakthrough during design discussion was abandoning the
"LOD = quality grade of one giant LLM" framing. Real industry direction
(NVIDIA ACE, Inworld AI) splits NPC intelligence into:
- **LOD0 (100 MB)** — dialogue LLM for conversation-eligible NPCs (<10m)
- **LOD1 (30 MB)** — behavior NN for combat-range NPCs (<30m)
- **LOD2 (10 MB)** — bark / expression model for visible NPCs (<100m)
- **No model** — pure FSM for background NPCs

Most NPCs in any given scene don't need conversation capability. The
bandwidth saving is therefore not a "quality vs. speed" tradeoff but a
realistic reflection of which model is *applicable* at each distance.

**Engineering honesty — the stress scenario shows LOD's cost**:

| Predictor (stress) | Weight Bytes | Audio Drops (FIFO) |
|--------------------|-------------:|-------------------:|
| ScriptedPredictor | 1,000 MB | 0 |
| LodPredictor | 1,400 MB | 416 |

When **all** 10 NPCs converge to LOD0 range, LodPredictor first issues
LOD2 (cold-start, conservative), then LOD1 (intermediate band), then
LOD0 — three superseded loads per NPC. This is the engineering
tradeoff:

- **LOD wins in open-world** (most NPCs stay distant)
- **LOD loses in combat-rush** (all NPCs converge)
- **The fix is Phase 6** — velocity-aware spatial predictor that skips
  intermediate tiers when distance is dropping fast

**This honest tradeoff is the strongest interview signal**: it shows
the engineer thought about *when the design fails*, not just when it
shines.

---

## 6. Extended Thoughts (Per Phase)

### Phase 1 — DES Engine

- **Future risk**: tombstone events consume heap memory until popped. A
  long-running scenario with high cancel rate could see zombie buildup.
  Currently fine (cancel ratio < 1 %); profile if Phase 10 scenarios
  show degradation.
- **Phase 9 implication**: P99 latency tracking stores all audio
  samples in a vector. A 60 s scenario would produce ~2.4 M samples
  (19 MB). Need t-digest or HDR histogram when scenarios grow.

### Phase 2 — Injectors

- **Did not yet model**: SSD seek latency, NAND queue depth, read
  amplification. These are L3 details that only matter once we model
  the SSD as a real entity (Phase 5).
- **Open question**: does the texture injector need cancellation? If a
  texture LOD downgrade occurs mid-burst, ideally remaining blocks are
  not loaded. Currently they are. Defer until Phase 6 produces a
  realistic cancellation source.

### Phase 3 — Scheduler

- **Read/write channel parallelism deferred** — current bus is a
  single serialized channel. Real AXI has separate AR/R/AW/W channels;
  a Phase 5 (Zero-Copy) refactor will likely separate read/write
  bandwidth to model "DMA write to NPU concurrent with audio read from
  SSD" correctly.
- **P99 caveat**: P99 is computed only at end of run. If we want
  rolling P99 (per-second window), the streaming sample reservoir
  needs replacement with a sliding window structure.
- **Quantum boundary effects**: very small audio packets finish well
  within one quantum; the math correctly schedules completion at
  fractional quantum time. But quantum boundaries can introduce 100 µs
  alignment jitter for packets arriving mid-quantum — visible only at
  µs scale, likely irrelevant for the demo.

### Phase 4 — LOD Manager

- **Cold-start conservatism is debatable** — currently "in deadband at
  t=0 → pick larger LOD". Could equally argue for "smaller LOD" or
  "lookup velocity from waypoint slope". Phase 6 with proper velocity
  will revisit.
- **Stagger** — all NPCs evaluated on the same tick. If a 50-NPC
  scene has 20 NPCs cross a threshold on the same tick, 20
  simultaneous prefetches hit the bus in one µs. Real systems stagger
  by NPC ID. Deferred to Phase 8.
- **Function-tier semantics demand functional differentiation in trace
  output** — currently LOD0/1/2 distinguishable only by size_bytes.
  Phase 9 reporting may want an explicit "LOD tier" column for
  readability.
- **Multi-LOD per NPC** — currently the LOD Manager replaces the LOD
  in flight (e.g. LOD2 still loading when LOD1 decision fires). In
  reality, both might need to be resident temporarily (LOD2 for ambient
  bark while LOD1 loads). Deferred until Phase 8 cache model.

### Phase 8 — Multi-core NPU (Locked, Not Yet Implemented)

- **Open**: per-core private cache vs shared cache. Locked decision:
  shared (single eviction policy story). Per-core would model NUMA-like
  effects but is out of scope.
- **Open**: timeout-driven fallback granularity. Should "fall back to
  previous LOD" mean "use stale LOD1 while LOD0 finishes" or "skip the
  prefetch entirely"? Probably the former, but interaction with
  predictor's resident set needs careful design.

---

## 7. Known Limitations & Honest Caveats

| Limitation | Impact | Mitigation Plan |
|-----------|--------|----------------|
| No NPU compute model | Cannot show "weight load competes with inference for NPU memory" | Phase 8 adds multi-core queue model |
| No SSD-side queue model | All SSD reads assumed bandwidth-bound | Phase 5 may add read amplification |
| No frame model | Cannot show "LOD reaction delay = N frames" | Out of scope — frame model is renderer territory |
| Single bus channel | Read and write share bandwidth | Phase 5 likely splits R/W (commented in Phase 3 retrospective) |
| Stress scenario regresses LOD | LodPredictor 40 % worse than scripted on stress | Phase 6 velocity-aware predictor fixes |
| P99 storage scales linearly | 60 s scenario = 19 MB sample vector | Phase 9 swaps in t-digest |
| No real ML inference | Weights are opaque blobs | Out of scope — explicit non-goal |

---

## 8. Future Work — Beyond Phase 11

Documented in `PROJECT_PLAN.md` "Backlog / Deferred Enhancements", in
priority order:

1. **Velocity-aware hysteresis** (Phase 6 extension): use NPC velocity
   to dynamically widen deadbands for fast-moving NPCs
2. **Per-NPC priority override**: quest NPCs always LOD0 regardless of
   distance (`npc.priority` field already reserved in schema)
3. **2D position scenarios**: replace 1D distance with `(x, y)` for
   frustum-aware decisions (`pos_x`/`pos_y` already in schema)
4. **NPC tick stagger**: distribute LOD checks across ticks to avoid
   simultaneous prefetch storms
5. **Read/write channel separation**: realistic AXI-style channel pair
6. **t-digest P99**: replace vector-based percentile for long runs
7. **Python visualization toolchain**: even though we committed to no
   Python in the build, post-hoc analysis via `tools/plot.py` would help
   demo quality

---

## 9. The Closing Story

### What This Project Proves

NeuroStream is **not** a renderer, **not** an ML inference engine,
**not** an emulator. It is a **behavioral protocol simulator** for the
I/O subsystem of a console-class system under AI-streaming workloads.

In ~1,600 lines of C++20 with ~830 lines of test coverage, it
demonstrates:

1. **QoS scheduling prevents audio dropouts under bus contention**
   (Pillar A — Phase 3 quantitatively verified: 416 drops → 0)
2. **Function-tier LOD reduces AI weight bandwidth by ~55 %** in
   open-world scenes (Pillar B — Phase 4 quantitatively verified)
3. **The two pillars are complementary, not redundant** — QoS handles
   *contention*, LOD handles *volume*; together they enable scenes
   that neither solves alone

### What a SIE Interviewer Should See

- Every architectural choice is **defensible against industry practice**
  (CLAUDE.md rule 5): tombstones (ns-2 / OMNeT++), token bucket
  (Linux tc, ARM CHI QoS Regulators), virtual-time WFQ (Cisco IOS),
  LOD bands (Unreal/Unity), 60 Hz LOD tick (game frame rate)
- **Honest about tradeoffs**: LodPredictor regresses on stress — the
  engineer who built this knows where it breaks and why
- **Path to closure**: every "this isn't ideal" has a labeled phase
  number where it gets resolved

### What's Left to Convince an Interviewer

- Phase 5 (Zero-Copy DMA) — demonstrate CPU cycle savings
- Phase 6 (Spatial Predictor) — fix the LOD-on-stress regression
- Phase 8 (Multi-core NPU + Eviction) — show cache pressure as a real
  observable signal
- Phase 9 (Reporting) — produce the side-by-side comparison artifact
  that lands in the portfolio

---

*Generated: 2026-05-16 · Commit: `1c93a48` · Phases 0–4 complete.*
