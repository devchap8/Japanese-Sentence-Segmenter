#include <catch2/catch_test_macros.hpp>

#include "jss/version.hpp"

TEST_CASE("library links and reports a version", "[smoke]") {
    REQUIRE_FALSE(jss::version().empty());
}
