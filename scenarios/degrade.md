# `degrade.yaml` — Frame deadline preservation

## Story

An interaction fires at t=0 (the instant the scenario starts). The NPU
cache is empty. There is no LOD available for the targeted NPC. The
predictor must emit a `Degrade` event with `no_weight` status — and
the frame must continue without blocking audio.

This is the Pillar G test: **"never block the frame"**.

## Tests

| Pillar | What's exercised |
|--------|------------------|
| G — Degradation | Empty-cache interaction → no-weight degrade |
| A — QoS | Audio still flows even when AI fails entirely |

## Expected KPI shape

- `degradations_no_weight`: 1
- Audio drops: **0** ← this is the entire point
- Audio P99: < 100 µs
- Frame stays smooth

## Reproduce

```bash
./build/neurostream --config config.yaml \
  --scenario scenarios/degrade.yaml \
  --policy qos --dma neuro_dma --compression inline_hw
```

Inspect the trace:

```bash
head /tmp/p8d.csv | grep degrade
```

## Headline number

**1 degradation event, 0 audio drops**. The AI subsystem failed —
fully — and the audio subsystem didn't notice. That's what "never
block the frame" looks like in numbers.
