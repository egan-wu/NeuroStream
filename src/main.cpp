#include "neurostream/clock.hpp"
#include "neurostream/config.hpp"
#include "neurostream/event_queue.hpp"
#include "neurostream/injector.hpp"
#include "neurostream/predictor.hpp"
#include "neurostream/scenario.hpp"
#include "neurostream/trace.hpp"
#include "neurostream/trace_sink.hpp"
#include <cstdio>
#include <string>

using namespace neurostream;

int main(int argc, char** argv) {
    std::string config_path   = "config.yaml";
    std::string scenario_path = "scenarios/demo.yaml";
    std::string trace_bin     = "run.bin";
    std::string trace_csv     = "run.csv";

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--config" && i + 1 < argc) config_path = argv[++i];
        else if (a == "--scenario" && i + 1 < argc) scenario_path = argv[++i];
        else if (a == "--trace-bin" && i + 1 < argc) trace_bin = argv[++i];
        else if (a == "--trace-csv" && i + 1 < argc) trace_csv = argv[++i];
    }

    Config   cfg;
    Scenario scn;
    try {
        cfg = load_config(config_path);
        scn = load_scenario(scenario_path);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "load error: %s\n", e.what());
        return 1;
    }

    std::printf("NeuroStream — Phase 2 traffic\n");
    std::printf("  scenario: %s (%d ms)\n", scn.name.c_str(), scn.duration_ms);
    std::printf("  npu cores: %d, scheduler: %s\n",
                cfg.npu.cores, cfg.scheduler.policy.c_str());

    Clock      clock;
    EventQueue q;
    TraceWriter tw(trace_bin, trace_csv);
    TraceSink   sink(tw);

    AudioTrafficGen   audio(cfg.audio);
    TextureTrafficGen texture(cfg.texture);
    AIWeightInjector  weights(cfg.ai_weights);

    Time stop_at_us = static_cast<Time>(scn.duration_ms) * 1000;

    if (scn.audio_enabled) audio.start(clock, q, sink, stop_at_us);
    for (const auto& b : scn.texture_bursts) {
        texture.schedule_burst(clock, q, sink,
                               static_cast<Time>(b.at_ms) * 1000,
                               b.rate_mbps, b.duration_ms);
    }
    ScriptedPredictor predictor(scn);
    predictor.start(clock, q, weights, sink);

    while (q.pop_and_run(clock)) {}

    std::printf("emitted %llu transactions over %lld us\n",
                (unsigned long long)sink.count(), (long long)clock.now());
    std::printf("trace: %s + %s\n", trace_bin.c_str(), trace_csv.c_str());
    return 0;
}
