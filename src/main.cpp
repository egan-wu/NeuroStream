#include "neurostream/clock.hpp"
#include "neurostream/config.hpp"
#include "neurostream/event_queue.hpp"
#include "neurostream/trace.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace neurostream;

int main(int argc, char** argv) {
    std::string config_path = "config.yaml";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--config" && i + 1 < argc) config_path = argv[++i];
    }

    Config cfg;
    try {
        cfg = load_config(config_path);
    } catch (const ConfigError& e) {
        std::fprintf(stderr, "config error: %s\n", e.what());
        return 1;
    }

    std::printf("NeuroStream — Phase 1 core\n");
    std::printf("  ssd: %d MB/s raw, %d MB/s effective\n",
                cfg.ssd.raw_bandwidth_mbps, cfg.ssd.effective_bandwidth_mbps);
    std::printf("  bus: %d MB/s\n", cfg.bus.total_bandwidth_mbps);
    std::printf("  npu: %d cores, %d MB cache\n",
                cfg.npu.cores, cfg.npu.shared_cache_mb);
    std::printf("  scheduler policy: %s, dma path: %s\n",
                cfg.scheduler.policy.c_str(), cfg.dma.path.c_str());

    Clock      clock;
    EventQueue q;

    int hits = 0;
    auto fire = [&] { hits++; std::printf("  event @ t=%lld\n", (long long)clock.now()); };
    q.schedule(100, fire);
    q.schedule(50,  fire);
    auto cancelled = q.schedule(75, [&] { hits++; });
    EventQueue::cancel(cancelled);
    q.schedule(200, fire);

    while (q.pop_and_run(clock)) {}
    std::printf("ran %d events, final clock=%lld us\n", hits, (long long)clock.now());
    return 0;
}
