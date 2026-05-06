#include "neurostream/predictor.hpp"

namespace neurostream {

void ScriptedPredictor::start(Clock& clock, EventQueue& q,
                              AIWeightInjector& weights, TransactionSink& sink) {
    for (const auto& spec : scenario_.weight_prefetches) {
        Time at_us = static_cast<Time>(spec.at_ms) * 1000;
        weights.schedule_prefetch(clock, q, sink, at_us, spec.npc_id, spec.lod);
    }
}

}
