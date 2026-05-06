#pragma once
#include "neurostream/clock.hpp"
#include "neurostream/event_queue.hpp"
#include "neurostream/injector.hpp"
#include "neurostream/scenario.hpp"
#include "neurostream/transaction.hpp"

namespace neurostream {

class Predictor {
public:
    virtual ~Predictor() = default;
    virtual void start(Clock& clock, EventQueue& q,
                       AIWeightInjector& weights, TransactionSink& sink) = 0;
};

// Phase 2 implementation: schedules every weight_prefetch entry from the
// scenario at its absolute time. Phase 6 will replace this with a
// frustum/velocity-aware predictor behind the same interface.
class ScriptedPredictor : public Predictor {
public:
    explicit ScriptedPredictor(const Scenario& s) : scenario_(s) {}
    void start(Clock& clock, EventQueue& q,
               AIWeightInjector& weights, TransactionSink& sink) override;

private:
    Scenario scenario_;
};

}
