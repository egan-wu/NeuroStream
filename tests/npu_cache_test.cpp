#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "neurostream/npu_cache.hpp"

using namespace neurostream;

namespace {
constexpr std::uint32_t MB = 1024u * 1024u;

NpuCache make_cache(std::uint32_t mb) {
    return NpuCache(mb * MB, std::make_unique<DistanceLruPolicy>());
}
}

TEST_CASE("admit: simple insertion within capacity") {
    auto c = make_cache(200);
    auto r = c.admit({1, 0}, 100 * MB, /*t=*/100, /*dist=*/5.0);
    CHECK(r.accepted);
    CHECK(r.evicted.empty());
    CHECK(c.bytes_used() == 100 * MB);
    CHECK(c.is_resident({1, 0}));
}

TEST_CASE("admit: re-admit same key refreshes without eviction") {
    auto c = make_cache(200);
    c.admit({1, 0}, 100 * MB, 100, 5.0);
    auto r = c.admit({1, 0}, 100 * MB, 200, 6.0);
    CHECK(r.accepted);
    CHECK(r.evicted.empty());
    CHECK(c.size() == 1);
}

TEST_CASE("admit: eviction picks farthest NPC first (distance-LRU)") {
    auto c = make_cache(200);
    c.admit({1, 0},  50 * MB, 100, /*dist=*/  5.0);   // close
    c.admit({2, 0},  50 * MB, 110, /*dist=*/ 50.0);   // mid
    c.admit({3, 0},  50 * MB, 120, /*dist=*/ 90.0);   // farthest
    c.admit({4, 0},  50 * MB, 130, /*dist=*/ 30.0);   // close-mid
    CHECK(c.size() == 4);
    CHECK(c.bytes_used() == 200 * MB);

    // Admit a fifth — capacity is full → must evict. Farthest is npc 3.
    auto r = c.admit({5, 0}, 50 * MB, 140, /*dist=*/ 2.0);
    CHECK(r.accepted);
    REQUIRE(r.evicted.size() == 1);
    CHECK(r.evicted[0].npc_id == 3);
    CHECK_FALSE(c.is_resident({3, 0}));
}

TEST_CASE("admit: tie-break on distance picks oldest access") {
    auto c = make_cache(200);
    c.admit({1, 0}, 100 * MB, /*t=*/100, /*dist=*/10.0);   // tied dist
    c.admit({2, 0}, 100 * MB, /*t=*/200, /*dist=*/10.0);   // newer access

    auto r = c.admit({3, 0}, 100 * MB, 300, 0.0);
    CHECK(r.accepted);
    REQUIRE(r.evicted.size() == 1);
    // Same distance → oldest (npc 1, t=100) goes.
    CHECK(r.evicted[0].npc_id == 1);
}

TEST_CASE("admit: pinned entries cannot be evicted") {
    auto c = make_cache(200);
    c.admit({1, 0}, 100 * MB, 100, /*dist=*/ 90.0);   // farthest, but pinned
    c.pin({1, 0});
    c.admit({2, 0}, 100 * MB, 110, /*dist=*/  5.0);
    CHECK(c.bytes_used() == 200 * MB);

    // Fifth attempt would normally evict npc 1 (farthest), but it's pinned.
    // Should evict npc 2 instead.
    auto r = c.admit({3, 0}, 100 * MB, 120, 5.0);
    CHECK(r.accepted);
    REQUIRE(r.evicted.size() == 1);
    CHECK(r.evicted[0].npc_id == 2);   // pinned npc 1 survived
    CHECK(c.is_resident({1, 0}));
}

TEST_CASE("admit: refuses when nothing is evictable") {
    auto c = make_cache(200);
    c.admit({1, 0}, 100 * MB, 100, 10.0);
    c.admit({2, 0}, 100 * MB, 110, 20.0);
    c.pin({1, 0});
    c.pin({2, 0});

    auto r = c.admit({3, 0}, 100 * MB, 120, 5.0);
    CHECK_FALSE(r.accepted);
    CHECK(r.evicted.empty());
    CHECK_FALSE(c.is_resident({3, 0}));
}

TEST_CASE("admit: refuses entries larger than capacity") {
    auto c = make_cache(50);
    auto r = c.admit({1, 0}, 100 * MB, 100, 5.0);
    CHECK_FALSE(r.accepted);
}

TEST_CASE("pin / unpin: refcount semantics") {
    auto c = make_cache(200);
    c.admit({1, 0}, 100 * MB, 100, 5.0);
    CHECK_FALSE(c.is_pinned({1, 0}));
    c.pin({1, 0});
    CHECK(c.is_pinned({1, 0}));
    c.pin({1, 0});         // refcount = 2
    c.unpin({1, 0});       // refcount = 1
    CHECK(c.is_pinned({1, 0}));
    c.unpin({1, 0});       // refcount = 0
    CHECK_FALSE(c.is_pinned({1, 0}));
}

TEST_CASE("pin: cannot pin non-resident key") {
    auto c = make_cache(200);
    CHECK_FALSE(c.pin({99, 0}));
}

TEST_CASE("touch: refreshes distance for subsequent eviction") {
    auto c = make_cache(200);
    c.admit({1, 0}, 100 * MB, /*t=*/100, /*dist=*/ 90.0);  // initially far
    c.admit({2, 0}, 100 * MB, /*t=*/110, /*dist=*/  5.0);

    // Player approaches NPC 1 → touch with new (small) distance.
    c.touch({1, 0}, 200, 3.0);

    // Now NPC 2 is the farther one → it should be evicted.
    auto r = c.admit({3, 0}, 100 * MB, 300, 2.0);
    REQUIRE(r.evicted.size() == 1);
    CHECK(r.evicted[0].npc_id == 2);
}
