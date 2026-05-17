# `town.yaml` — Crowded street, no interaction

## Story

The player walks east 80 m through a town of 20 NPCs lining the street
(alternating sides, 3-5 m off the player's path). The player never
stops, never faces any NPC, never interacts.

This scenario demonstrates the "distance-only LOD fallacy" — a system
that loads dialogue weights for every NPC the player passes wastes
massive bandwidth on uninteracted ambient NPCs.

## Tests

| Pillar | What's exercised |
|--------|------------------|
| D — Intent | Frustum + stopped-detection: 0 LOD0 prefetches (no one is
spoken to). Only LOD2 (ambient bark) loads for visible NPCs. |
| B — LOD | The Pillar B / D combined value proposition |

## Expected KPI shape

Weight bytes by predictor (qos run):

| Predictor | Bytes | LOD0 | LOD1 | LOD2 |
|-----------|------:|----:|----:|----:|
| `scripted` (control) | 2,000 MB | 20 | 0 | 0 |
| `lod` (Phase 4) ⚠️ | 2,800 MB | 20 | 20 | 20 |
| **`intent` (Phase 6)** ⭐ | **800 MB** | **0** | **20** | **20** |

## Reproduce

```bash
./build/neurostream --config config.yaml --scenario scenarios/town.yaml
```

## Headline number

**71% less weight bandwidth** vs distance-LOD predictor. **0 LOD0
prefetches**: the player walked past 20 NPCs and none of them had
their dialogue model loaded. This is the "what makes our system
intelligent" demo.
