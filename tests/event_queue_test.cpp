#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "neurostream/clock.hpp"
#include "neurostream/event_queue.hpp"
#include <vector>

using namespace neurostream;

TEST_CASE("events fire in time order") {
    EventQueue q;
    std::vector<Time> seen;
    q.schedule(300, [&] { seen.push_back(300); });
    q.schedule(100, [&] { seen.push_back(100); });
    q.schedule(200, [&] { seen.push_back(200); });

    Clock clk;
    while (q.pop_and_run(clk)) {}

    CHECK(seen == std::vector<Time>{100, 200, 300});
}

TEST_CASE("equal-time events fire in schedule order (stable)") {
    EventQueue q;
    std::vector<int> seen;
    q.schedule(100, [&] { seen.push_back(1); });
    q.schedule(100, [&] { seen.push_back(2); });
    q.schedule(100, [&] { seen.push_back(3); });

    Clock clk;
    while (q.pop_and_run(clk)) {}
    CHECK(seen == std::vector<int>{1, 2, 3});
}

TEST_CASE("cancelled events do not fire") {
    EventQueue q;
    int hits = 0;
    auto h1 = q.schedule(100, [&] { hits++; });
    auto h2 = q.schedule(200, [&] { hits++; });
    auto h3 = q.schedule(300, [&] { hits++; });
    EventQueue::cancel(h2);

    Clock clk;
    while (q.pop_and_run(clk)) {}
    CHECK(hits == 2);
    (void)h1; (void)h3;
}

TEST_CASE("peek_time skips tombstones at the head") {
    EventQueue q;
    auto h = q.schedule(50, [] {});
    q.schedule(100, [] {});
    EventQueue::cancel(h);
    CHECK(q.peek_time() == 100);
}

TEST_CASE("clock advances with each fired event and rejects rewind") {
    EventQueue q;
    Clock clk;
    q.schedule(100, [] {});
    q.schedule(250, [] {});

    while (q.pop_and_run(clk)) {}
    CHECK(clk.now() == 250);
    CHECK_THROWS_AS(clk.advance_to(100), std::logic_error);
}

TEST_CASE("handler observes correct clock time when it fires") {
    EventQueue q;
    Clock clk;
    std::vector<Time> observed;
    q.schedule(100, [&] { observed.push_back(clk.now()); });
    q.schedule(250, [&] { observed.push_back(clk.now()); });
    q.schedule(50,  [&] { observed.push_back(clk.now()); });

    while (q.pop_and_run(clk)) {}
    CHECK(observed == std::vector<Time>{50, 100, 250});
}

TEST_CASE("scheduling from inside a handler works") {
    EventQueue q;
    int hits = 0;
    q.schedule(100, [&] {
        hits++;
        q.schedule(200, [&] { hits++; });
    });

    Clock clk;
    while (q.pop_and_run(clk)) {}
    CHECK(hits == 2);
}
