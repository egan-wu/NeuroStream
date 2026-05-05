#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

TEST_CASE("smoke: arithmetic still works") {
    CHECK(1 + 1 == 2);
}
