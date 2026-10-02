// The detector finds a period that starts at any op of the stream, keeps a
// bounded history, and states the oldest op that a future iteration can hold.

#include <crucible/IterationDetector.h>

#include "test_assert.h"
#include <cstdint>

using crucible::IterationDetector;
using crucible::SchemaHash;
using crucible::ShapeHash;

namespace {

constexpr uint64_t K = IterationDetector::K;

// The schema of op k of the periodic body that the tests record.  The body
// has eight ops, and each op occurs once in it.
SchemaHash periodic_op(uint64_t k) { return SchemaHash{0x100 + k % 8}; }

// Feeds prefix_ops ops that never occur again, then the eight-op body for
// the given number of iterations.  Returns the boundaries that the body gives,
// and checks that each one after the first three reports a length of eight,
// eight ops after the boundary before it.
uint32_t feed_prefix_then_body(IterationDetector& detector, uint64_t prefix_ops, uint32_t iterations) {
    for (uint64_t i = 0; i < prefix_ops; ++i)
        (void)detector.check(SchemaHash{0x1000000 + i}, ShapeHash{0x2000000 + i});
    uint32_t boundaries = 0;
    uint64_t last_boundary = 0;
    for (uint64_t n = 0; n < uint64_t{iterations} * 8; ++n) {
        if (!detector.check(periodic_op(n))) continue;
        ++boundaries;
        if (boundaries > 3) {
            assert(detector.last_completed_len == 8);
            assert(n - last_boundary == 8);
        }
        last_boundary = n;
    }
    return boundaries;
}

void expect_period_after_prefix(uint64_t prefix_ops) {
    IterationDetector d;
    const uint32_t boundaries = feed_prefix_then_body(d, prefix_ops, 400);
    assert(boundaries >= 395);
    assert(d.confirmed);
}

void test_prefix_that_never_recurs() {
    // The ops before the training loop occur one time.  The detector finds
    // the period of the loop all the same, two iterations after the loop
    // starts, at each length of the prefix.
    expect_period_after_prefix(5);
    expect_period_after_prefix(1);
    expect_period_after_prefix(4093);
    expect_period_after_prefix(100000);
}

void test_history_is_bounded() {
    // Ops that never repeat give no period.  The detector forgets its oldest
    // op for each new op once the history fills the largest ring.
    constexpr uint64_t kHistoryBound = uint64_t{1} << 19;
    IterationDetector d;
    for (uint64_t i = 0; i < 3 * kHistoryBound; ++i) {
        assert(!d.check(SchemaHash{0x7000000 + i}));
        assert(d.pos_ - d.base_ <= kHistoryBound);
    }
    assert(kHistoryBound == IterationDetector::HISTORY_MAX_CAPACITY);
    assert(d.history_capacity() == IterationDetector::HISTORY_MAX_CAPACITY);
    // The detector still finds a period after the bound.
    assert(feed_prefix_then_body(d, 0, 400) >= 395);
}

uint32_t boundaries_of_long_body(uint32_t length) {
    IterationDetector d;
    uint32_t boundaries = 0;
    for (uint64_t n = 0; n < uint64_t{length} * 3; ++n)
        boundaries += d.check(SchemaHash{0x9000000 + n % length}) ? 1u : 0u;
    assert(d.pos_ - d.base_ <= IterationDetector::HISTORY_MAX_CAPACITY);
    return boundaries;
}

void test_period_past_the_bound_gives_no_boundary() {
    // A body of MAX_PERIOD ops has its square in the largest ring.  A body
    // one op longer does not, and gives no boundary.
    assert(boundaries_of_long_body(IterationDetector::MAX_PERIOD) > 0);
    assert(boundaries_of_long_body(IterationDetector::MAX_PERIOD + 1) == 0);
}

void test_oldest_claimable_holds_each_iteration() {
    // Each iteration that a boundary reports starts at or after each value
    // that oldest_claimable gave before the boundary, and the value never
    // decreases between two restarts.  The stream has a prefix that never
    // recurs, a period, a gap that breaks the period one time, and an
    // eleven-op body whose window of five ops occurs three times in it.
    IterationDetector d;
    uint64_t floor_before = 0;
    uint32_t boundaries = 0;
    auto feed_op = [&](SchemaHash schema) {
        const uint64_t floor_now = d.oldest_claimable();
        assert(floor_now >= floor_before);
        floor_before = floor_now;
        if (!d.check(schema)) return;
        ++boundaries;
        const uint64_t iteration_start = d.pos_ - K - d.last_completed_len;
        assert(iteration_start >= floor_now);
    };
    for (uint64_t i = 0; i < 37; ++i)
        feed_op(SchemaHash{0x5000000 + i});
    for (uint64_t n = 0; n < 8 * 40; ++n)
        feed_op(periodic_op(n));
    for (uint64_t n = 3; n < 8 * 40; ++n)
        feed_op(periodic_op(n));
    for (uint64_t n = 0; n < 11 * 40; ++n) {
        const uint64_t phase = n % 11;
        feed_op(SchemaHash{phase < 9 ? 1 + phase % 3 : phase - 8});
    }
    assert(boundaries > 60);
}

}  // namespace

int main() {
    test_prefix_that_never_recurs();
    test_history_is_bounded();
    test_period_past_the_bound_gives_no_boundary();
    test_oldest_claimable_holds_each_iteration();
    return 0;
}
