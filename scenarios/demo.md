# `demo.yaml` — Health check

## Story

A minimal 5-second scenario: player walks east, 2 NPCs, one texture
burst. This is the first thing to run after building. If `demo.yaml`
doesn't produce sensible numbers, something is broken.

## Tests

Smoke-level coverage of all pillars without specifically stressing
any of them.

## Reproduce

```bash
./build/neurostream --config config.yaml --scenario scenarios/demo.yaml
```

## Headline number

There isn't one — this scenario exists so we can confirm the system
runs end-to-end with realistic numbers, not to prove any particular
claim.
