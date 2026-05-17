# `cache_pressure.yaml` — NPU cache thrashing

## Story

10 seconds. NPC 1 and NPC 2 are pinned by interactions for the entire
duration. NPCs 3-5 are close at start but walk away. NPCs 6-10 each
arrive sequentially every second, replacing the cycling residents.

NPU cache is 512 MB (default), holding up to 5 simultaneous LOD0
weights. With 8 NPCs cycling through proximity, eviction must fire.

## Tests

| Pillar | What's exercised |
|--------|------------------|
| F — Eviction | Distance-LRU picks farthest first |
| F — Pinning | NPCs 1, 2 survive throughout despite pressure |
| G — Degradation | Not triggered here (interactions start late enough) |

## Expected KPI shape (qos + neuro_dma + inline_hw)

- Evictions: ~1,200
- Cache hits: 2 (one per pinned interaction)
- Admission refusals: 0 (always something evictable)
- Audio drops: 0
- Audio P99: ~60 µs (slightly worse than no-pressure — cache work has
  some real cost in the trace volume)

## Reproduce

```bash
./build/neurostream --config config.yaml \
  --scenario scenarios/cache_pressure.yaml \
  --policy qos --dma neuro_dma --compression inline_hw
```

## Headline number

**1,238 evictions in 10 seconds — yet zero audio drops** and the two
interaction-pinned NPCs stay resident throughout. The cache thrashes
loudly but the frame stays smooth.
