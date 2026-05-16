#include "neurostream/clock.hpp"
#include "neurostream/config.hpp"
#include "neurostream/event_queue.hpp"
#include "neurostream/injector.hpp"
#include "neurostream/predictor.hpp"
#include "neurostream/scenario.hpp"
#include "neurostream/scheduler.hpp"
#include "neurostream/trace.hpp"
#include <algorithm>
#include <cstdio>
#include <string>

using namespace neurostream;

namespace {

struct RunResult {
    Scheduler::Kpi kpi;
    Time           wall_clock_us = 0;
};

RunResult run_one(const Config& cfg_in, const Scenario& scn,
                  const std::string& policy,
                  const std::string& dma_path,
                  const std::string& compression_path,
                  const std::string& trace_bin, const std::string& trace_csv) {
    Config cfg = cfg_in;
    cfg.scheduler.policy = policy;
    if (!dma_path.empty())         cfg.dma.path         = dma_path;
    if (!compression_path.empty()) cfg.compression.path = compression_path;

    Clock       clock;
    EventQueue  q;
    TraceWriter tw(trace_bin, trace_csv);

    auto sched = make_scheduler(cfg, clock, q, &tw);

    AudioTrafficGen   audio(cfg.audio);
    TextureTrafficGen texture(cfg.texture);
    AIWeightInjector  weights(cfg.ai_weights);

    Time stop_at_us = static_cast<Time>(scn.duration_ms) * 1000;

    if (scn.audio_enabled) audio.start(clock, q, *sched, stop_at_us);
    for (const auto& b : scn.texture_bursts) {
        texture.schedule_burst(clock, q, *sched,
                               static_cast<Time>(b.at_ms) * 1000,
                               b.rate_mbps, b.duration_ms);
    }
    auto predictor = make_predictor(cfg.predictor.policy, scn, cfg);
    predictor->start(clock, q, weights, *sched);
    sched->set_completion_observer(
        [&predictor](std::uint32_t npc_id, int lod) {
            predictor->on_complete(npc_id, lod);
        });

    while (q.pop_and_run(clock)) {}

    RunResult r;
    r.kpi           = sched->kpi();
    r.wall_clock_us = clock.now();
    return r;
}

Time p99(std::vector<Time>& v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    std::size_t i = static_cast<std::size_t>(0.99 * static_cast<double>(v.size()));
    if (i >= v.size()) i = v.size() - 1;
    return v[i];
}

void print_kpi(const char* label, Scheduler::Kpi kpi) {
    Time p = p99(kpi.audio_lat_samples);
    std::printf("  [%s] issued=%llu completed=%llu dropped=%llu | "
                "audio_completed=%llu audio_dropped=%llu | "
                "audio_lat_us mean=%.1f max=%lld p99=%lld | "
                "weight_lat_us max=%lld | "
                "cpu_cycles=%llu decompress_cycles=%llu sgl_entries=%llu\n",
                label,
                (unsigned long long)kpi.issued,
                (unsigned long long)kpi.completed,
                (unsigned long long)kpi.dropped,
                (unsigned long long)kpi.audio_completed,
                (unsigned long long)kpi.audio_dropped,
                kpi.audio_lat_mean,
                (long long)kpi.audio_lat_max,
                (long long)p,
                (long long)kpi.weight_lat_max,
                (unsigned long long)kpi.cpu_cycles_used,
                (unsigned long long)kpi.decompress_cycles_used,
                (unsigned long long)kpi.sgl_entries_total);
}

}

int main(int argc, char** argv) {
    std::string config_path   = "config.yaml";
    std::string scenario_path = "scenarios/demo.yaml";
    std::string trace_bin     = "run.bin";
    std::string trace_csv     = "run.csv";
    std::string policy        = "";   // empty = use config; "fifo"/"qos" overrides; "ab" runs both
    std::string dma           = "";   // empty = use config
    std::string compression   = "";   // empty = use config
    bool        ab_mode       = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--config" && i + 1 < argc) config_path = argv[++i];
        else if (a == "--scenario" && i + 1 < argc) scenario_path = argv[++i];
        else if (a == "--trace-bin" && i + 1 < argc) trace_bin = argv[++i];
        else if (a == "--trace-csv" && i + 1 < argc) trace_csv = argv[++i];
        else if (a == "--policy" && i + 1 < argc) policy = argv[++i];
        else if (a == "--dma" && i + 1 < argc) dma = argv[++i];
        else if (a == "--compression" && i + 1 < argc) compression = argv[++i];
        else if (a == "--ab") ab_mode = true;
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

    std::printf("NeuroStream — Phase 7 (scheduler × dma × compression)\n");
    std::printf("  scenario: %s (%d ms), npu cores: %d\n",
                scn.name.c_str(), scn.duration_ms, cfg.npu.cores);

    if (ab_mode) {
        // 5-level "improvement ladder" for the PS5 story:
        //   1. fifo  + bounce    + none       (baseline)
        //   2. qos   + bounce    + none       (+QoS scheduler)
        //   3. qos   + neuro_dma + none       (+zero-copy DMA)
        //   4. qos   + neuro_dma + cpu        (+compression, software)
        //   5. qos   + neuro_dma + inline_hw  (+hardware decompressor = full PS5)
        std::printf("A/B comparison — 5-level improvement ladder:\n");
        struct Step { const char* pol; const char* dma; const char* cmp; };
        const Step ladder[] = {
            {"fifo", "bounce",    "none"},
            {"qos",  "bounce",    "none"},
            {"qos",  "neuro_dma", "none"},
            {"qos",  "neuro_dma", "cpu"},
            {"qos",  "neuro_dma", "inline_hw"},
        };
        for (const auto& s : ladder) {
            std::string suffix = std::string(".") + s.pol + "." + s.dma + "." + s.cmp;
            auto r = run_one(cfg, scn, s.pol, s.dma, s.cmp,
                             trace_bin + suffix, trace_csv + suffix);
            char label[48];
            std::snprintf(label, sizeof(label), "%s+%s+%s", s.pol, s.dma, s.cmp);
            print_kpi(label, r.kpi);
        }
    } else {
        std::string p = policy.empty() ? cfg.scheduler.policy : policy;
        std::string d = dma;
        std::string c = compression;
        auto r = run_one(cfg, scn, p, d, c, trace_bin, trace_csv);
        std::printf("policy=%s dma=%s compression=%s wall=%lld us\n",
                    p.c_str(),
                    (d.empty() ? cfg.dma.path.c_str() : d.c_str()),
                    (c.empty() ? cfg.compression.path.c_str() : c.c_str()),
                    (long long)r.wall_clock_us);
        print_kpi(p.c_str(), r.kpi);
        std::printf("trace: %s + %s\n", trace_bin.c_str(), trace_csv.c_str());
    }
    return 0;
}
