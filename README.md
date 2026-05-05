# NeuroStream

A behavioral simulator for SSD↔NPU streaming co-design in console-class
systems. NeuroStream demonstrates how AI model weights can be hot-swapped
at runtime without disturbing audio, textures, or input — by treating the
I/O subsystem as a QoS-managed network rather than a dumb pipe.

> Status: **Phase 0 — Bootstrap**. See [PROJECT_PLAN.md](PROJECT_PLAN.md)
> for the roadmap and [Design_Detail.md](Design_Detail.md) for locked
> design decisions.

## Quickstart

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/neurostream
ctest --test-dir build --output-on-failure
```

Requires CMake 3.20+ and a C++20 compiler. `yaml-cpp` and `doctest` are
fetched automatically by CMake.

## Layout

```
src/          simulator sources
include/      public headers
tests/        unit tests (doctest)
scenarios/    per-experiment YAML scenarios (Phase 2)
docs/         architecture and design notes
scripts/      helper scripts
config.yaml   hardware constants and policy defaults
```

## Documents

- [PROJECT_PLAN.md](PROJECT_PLAN.md) — phase roadmap and KPIs
- [Design_Detail.md](Design_Detail.md) — locked design decisions
- [CLAUDE.md](CLAUDE.md) — agent working agreement

## License

[MIT](LICENSE)
