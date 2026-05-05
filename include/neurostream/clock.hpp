#pragma once
#include "neurostream/time.hpp"
#include <stdexcept>

namespace neurostream {

class Clock {
public:
    Time now() const noexcept { return now_; }

    void advance_to(Time t) {
        if (t < now_) throw std::logic_error("Clock cannot move backward");
        now_ = t;
    }

private:
    Time now_ = 0;
};

}
