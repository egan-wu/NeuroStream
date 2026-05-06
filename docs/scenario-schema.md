# Scenario YAML Schema

A scenario describes one experiment: how long to run, which traffic streams
are active, and what one-shot events the predictor should fire. Hardware
constants (bandwidths, sizes, policies) live in `config.yaml` and are not
repeated here.

The scenario loader is **fail-fast**: any missing required field throws
`ScenarioError`.

## Top-level fields

| Field | Type | Required | Notes |
|-------|------|----------|-------|
| `name` | string | yes | Human label for the scenario, also used in result filenames. |
| `duration_ms` | int | yes | Total simulated time in milliseconds. |
| `audio` | object | yes | Audio stream control. See below. |
| `texture_bursts` | sequence | no | Zero or more burst events. |
| `weight_prefetches` | sequence | no | Zero or more scripted weight prefetches. |

## `audio`

| Field | Type | Required | Notes |
|-------|------|----------|-------|
| `enabled` | bool | yes | If `true`, generates audio packets at the rate set in `config.yaml` from t=0 to `duration_ms`. |

## `texture_bursts[i]`

| Field | Type | Required | Notes |
|-------|------|----------|-------|
| `at_ms` | int | yes | Burst start time in ms. |
| `rate_mbps` | int | yes | Sustained rate during the burst. |
| `duration_ms` | int | yes | Burst length. |

Block size is taken from `config.texture.block_bytes`.

## `weight_prefetches[i]`

| Field | Type | Required | Notes |
|-------|------|----------|-------|
| `at_ms` | int | yes | Prefetch issue time. |
| `npc_id` | uint32 | yes | Identifier carried in `Transaction.source_inst`. |
| `lod` | int | yes | One of 0 (full), 1 (mid), 2 (quantized). |

Sizes per LOD come from `config.ai_weights.lod{0,1,2}_mb`.

## Example

```yaml
name: combat_burst
duration_ms: 5000

audio:
  enabled: true

texture_bursts:
  - at_ms: 1000
    rate_mbps: 500
    duration_ms: 100

weight_prefetches:
  - at_ms: 800
    npc_id: 5
    lod: 0
  - at_ms: 2500
    npc_id: 7
    lod: 2
```
