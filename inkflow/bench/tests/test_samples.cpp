// ABOUTME: Unit tests for the fixed-capacity sample accumulator used by eink-bench.
// ABOUTME: Wrong statistics would make every measurement worthless, so they are pinned.

#include "vendor/doctest.h"

#include "bench/samples.hpp"

#include <initializer_list>

TEST_CASE("an empty accumulator reports zero for everything") {
    bench::Samples<8> s;
    CHECK(s.count() == 0u);
    CHECK(s.min() == 0u);
    CHECK(s.max() == 0u);
    CHECK(s.mean() == 0u);
    CHECK(s.median() == 0u);
    CHECK(s.p95() == 0u);
}

TEST_CASE("a single sample is its own min, max, mean and median") {
    bench::Samples<8> s;
    REQUIRE(s.add(42u));
    CHECK(s.count() == 1u);
    CHECK(s.min() == 42u);
    CHECK(s.max() == 42u);
    CHECK(s.mean() == 42u);
    CHECK(s.median() == 42u);
    CHECK(s.p95() == 42u);
}

TEST_CASE("min and max are found regardless of insertion order") {
    bench::Samples<8> s;
    for (std::uint32_t v : {50u, 10u, 90u, 30u}) {
        REQUIRE(s.add(v));
    }
    CHECK(s.count() == 4u);
    CHECK(s.min() == 10u);
    CHECK(s.max() == 90u);
}

TEST_CASE("mean uses integer arithmetic and does not overflow on large timings") {
    // Refresh latencies are microseconds; a long run of slow full refreshes must not
    // wrap the accumulator. 200 samples of 2 seconds each is 4e8 us.
    bench::Samples<256> s;
    for (int i = 0; i < 200; ++i) {
        REQUIRE(s.add(2000000u));
    }
    CHECK(s.mean() == 2000000u);
}

TEST_CASE("median of an odd count is the middle value") {
    bench::Samples<8> s;
    for (std::uint32_t v : {5u, 1u, 3u}) {
        REQUIRE(s.add(v));
    }
    CHECK(s.median() == 3u);
}

TEST_CASE("median of an even count averages the two middle values") {
    bench::Samples<8> s;
    for (std::uint32_t v : {10u, 20u, 30u, 40u}) {
        REQUIRE(s.add(v));
    }
    CHECK(s.median() == 25u);
}

TEST_CASE("p95 reports a high percentile, not the maximum") {
    // The tail matters: one slow refresh in twenty is felt by a reader, and a mean
    // hides it. 100 samples where the top 5 are outliers.
    bench::Samples<128> s;
    for (int i = 0; i < 95; ++i) {
        REQUIRE(s.add(100u));
    }
    for (int i = 0; i < 5; ++i) {
        REQUIRE(s.add(900u));
    }
    CHECK(s.max() == 900u);
    CHECK(s.median() == 100u);
    CHECK(s.p95() >= 100u);
    CHECK(s.p95() <= 900u);
}

TEST_CASE("adding beyond capacity is refused without corrupting the data") {
    bench::Samples<4> s;
    for (std::uint32_t v : {1u, 2u, 3u, 4u}) {
        REQUIRE(s.add(v));
    }
    CHECK_FALSE(s.add(5u));
    CHECK(s.count() == 4u);
    CHECK(s.min() == 1u);
    CHECK(s.max() == 4u);
}

TEST_CASE("values are held in sorted order whatever order they arrive in") {
    bench::Samples<8> s;
    for (std::uint32_t v : {7u, 2u, 9u, 1u, 5u}) {
        REQUIRE(s.add(v));
    }
    for (std::size_t i = 1; i < s.count(); ++i) {
        CHECK(s.at(i - 1u) <= s.at(i));
    }
}

TEST_CASE("duplicate values are all retained") {
    bench::Samples<8> s;
    for (int i = 0; i < 5; ++i) {
        REQUIRE(s.add(100u));
    }
    CHECK(s.count() == 5u);
    CHECK(s.min() == 100u);
    CHECK(s.max() == 100u);
    CHECK(s.median() == 100u);
}

TEST_CASE("reset empties the accumulator for the next measurement run") {
    bench::Samples<8> s;
    REQUIRE(s.add(5u));
    s.reset();
    CHECK(s.count() == 0u);
    CHECK(s.max() == 0u);
    REQUIRE(s.add(9u));
    CHECK(s.min() == 9u);
}
