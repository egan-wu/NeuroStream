# `world.yaml` — Open-world traversal

## Story

The player walks east 50 m over 5 seconds. 10 NPCs are positioned at
varied distances and angles: 2 along the player's path (close), 3 in
combat range (mid), 5 far in the periphery. A texture burst hits at
t=2.5 s.

## Tests

| Pillar | What's exercised |
|--------|------------------|
| A — QoS scheduler | Audio survives the texture burst |
| B — LOD manager (intent) | Distance-LOD split: 2 LOD0, 3 LOD1, 5 LOD2 |
| C — Zero-copy DMA | Weight transfers cleanly on the dedicated path |
| D — Intent predictor | Frustum + velocity → no unnecessary LOD0 |
| E — Decompressor | Compression halves bus traffic |
| F — Cache eviction | Not triggered (cache fits all close NPCs) |
| G — Degradation | Not triggered (no interactions) |

## Expected KPI shape (qos + neuro_dma + inline_hw)

- Audio P99: ~1 µs
- Weight max: ~13 ms
- CPU cycles: ~14 K
- Audio drops: 0
- Cache evictions: 0
- Total weight bytes: ~240 MB (intent predictor)

## Reproduce

```bash
./build/neurostream --config config.yaml \
  --scenario scenarios/world.yaml --ab
# → results/world/summary.md
```

## Headline number

**45× audio P99 improvement** going from baseline (`fifo+bounce+none`)
to full PS5 stack (`qos+neuro_dma+inline_hw`).
