#pragma once
#include "neurostream/clock.hpp"
#include "neurostream/time.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <queue>
#include <vector>

namespace neurostream {

class EventQueue {
public:
    using Handler = std::function<void()>;
    using Handle  = std::shared_ptr<bool>;

    Handle schedule(Time when, Handler handler) {
        auto alive = std::make_shared<bool>(true);
        heap_.push(Entry{when, next_seq_++, std::move(handler), alive});
        return alive;
    }

    static void cancel(const Handle& h) noexcept {
        if (h) *h = false;
    }

    bool empty() const noexcept { return heap_.empty(); }
    std::size_t size() const noexcept { return heap_.size(); }

    // Advance the clock to the next live event's time and run its handler.
    // Tombstoned entries are dropped silently. Returns false if no live event
    // remains. The clock is advanced *before* the handler runs so handlers
    // observe the correct current time.
    bool pop_and_run(Clock& clock) {
        while (!heap_.empty()) {
            Entry e = std::move(const_cast<Entry&>(heap_.top()));
            heap_.pop();
            if (!e.alive || !*e.alive) continue;
            clock.advance_to(e.when);
            e.handler();
            return true;
        }
        return false;
    }

    // Time of the next live event, walking past tombstones at the top.
    // Returns kInvalidTime if the queue has no live events.
    Time peek_time() {
        while (!heap_.empty()) {
            const Entry& e = heap_.top();
            if (e.alive && *e.alive) return e.when;
            heap_.pop();
        }
        return kInvalidTime;
    }

private:
    struct Entry {
        Time when;
        std::uint64_t seq;
        Handler handler;
        Handle alive;

        bool operator>(const Entry& o) const noexcept {
            return when != o.when ? when > o.when : seq > o.seq;
        }
    };

    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> heap_;
    std::uint64_t next_seq_ = 0;
};

}
