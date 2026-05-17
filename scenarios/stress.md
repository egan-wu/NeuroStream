# `stress.yaml` — Combat ambush (convergent motion)

## Story

The player stands still in the center. 10 NPCs converge from all
directions, all reaching ~5 m distance by t=500 ms. Average approach
velocity: 150 m/s. A 500 ms texture burst overlaps the convergence.

This is the adversarial case that broke Phase 4's LodPredictor (cold-
start LOD2 → LOD1 → LOD0 stair-climbing wasted 40% of bandwidth).
Phase 6's velocity look-ahead fixes it.

## Tests

| Pillar | What's exercised |
|--------|------------------|
| A — QoS | Audio survives the spike — without QoS, baseline drops audio |
| B — LOD | Stair-climb avoidance via look-ahead |
| C — DMA | Burst transfer benefits from neuro_dma |
| D — Intent | The Phase 4 stress regression we fixed |
| E — Decompressor | Bus compression keeps audio P99 low under spike |

## Expected KPI shape

Full stack (`qos+neuro_dma+inline_hw`):
- Audio drops: 0
- Audio P99: ~34 µs
- Weight max: ~20 ms

Baseline (`fifo+bounce+none`):
- Audio drops: ~129 ← **the bug we're showing off fixing**
- Audio P99: ~675 µs

## Reproduce

```bash
./build/neurostream --config config.yaml \
  --scenario scenarios/stress.yaml --ab
```

## Headline number

**129 audio drops → 0** between baseline and full stack. This is the
visceral demonstration: real games would have audible glitches in
the baseline configuration.
