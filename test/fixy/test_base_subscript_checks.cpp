// Each subscript of the base checks its index in each build.  The bounds
// check of the standard library is on only in Debug, so a subscript that
// relied on it read past its run in a Release build.  Each death runs in a
// child process through expect_dies of the Check family, in each preset that
// runs the tests, the release preset too.  The child must end by SIGABRT with
// the text of the check.  The controls then read each run inside its bounds.
//
// The subscripts: AppendOnly (operator[], front and back), OrderedAppendOnly,
// which reads through AppendOnly, static_run, and the lane of a mask and of a
// vector of the Simd facade.  test_session_event_log_writers.cpp holds the
// deaths of the subscripts of a SessionEventLog.  A SharedRead has no
// subscript: its element door compares the position with the count and runs
// on_none past the end, and test_shared_region.cpp holds that control.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Mutation.h>
#include <fixy/check/Check.h>
#include <fixy/session/Registry.h>
#include <foundation/Simd.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

namespace {

namespace eff = ::foundation::effects;
using ::fixy::check::expect_dies;
using ::fixy::check::pass;

// The value comes back through a call that no optimization reads, so each
// index below is a value of the run time.
template <typename T>
[[nodiscard, gnu::noipa]] T opaque(T value) noexcept {
    return value;
}

// A log of count values from 1.  The count is opaque, so the optimizer
// cannot see an empty log and refuse the read of the old code at compile
// time.
[[nodiscard]] ::fixy::AppendOnly<int> log_of(int count) {
    ::fixy::AppendOnly<int> log = ::fixy::mint_append_only<int>();
    for (int value = 1; value <= opaque(count); ++value) {
        log.append(eff::testing::test().alloc, value);
    }
    return log;
}

void read_append_only_past_end() {
    ::fixy::AppendOnly<int> const log = log_of(3);
    (void)opaque(log[opaque(std::size_t{3})]);
}
void read_front_of_empty_append_only() {
    ::fixy::AppendOnly<int> const log = log_of(0);
    (void)opaque(log.front());
}
void read_back_of_empty_append_only() {
    ::fixy::AppendOnly<int> const log = log_of(0);
    (void)opaque(log.back());
}

void read_ordered_past_end() {
    ::fixy::OrderedAppendOnly<int> ordered = ::fixy::mint_ordered_append_only<int>();
    ordered.append(eff::testing::test().alloc, 1);
    (void)opaque(ordered[opaque(std::size_t{1})]);
}

// A run of three integers in static storage.
namespace transition = ::fixy::session::transition;

constexpr ::fixy::FixedArray<int, 3> run_values{4, 5, 6};

consteval transition::static_run<int> run_of_three() {
    return transition::static_run<int>{&*run_values.get(0).expect("three values"), 3};
}

void read_static_run_past_end() noexcept {
    constexpr transition::static_run<int> run = run_of_three();
    (void)opaque(run[opaque(std::size_t{3})]);
}

using Lanes4 = ::foundation::simd::vec<std::int32_t, 4>;

void read_vector_lane_past_end() noexcept {
    Lanes4 const vector{std::int32_t{7}};
    (void)opaque(vector[opaque(4)]);
}

void read_mask_lane_below_zero() noexcept {
    Lanes4 const vector{std::int32_t{7}};
    auto const equal = vector == vector;
    (void)opaque(equal[opaque(-1)]);
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{eff::testing::test()};
    expect_dies(test, read_append_only_past_end, ::fixy::TextView{"fatal invariant failed: i < size()"});
    expect_dies(test, read_front_of_empty_append_only, ::fixy::TextView{"fatal invariant failed: !empty()"});
    expect_dies(test, read_back_of_empty_append_only, ::fixy::TextView{"fatal invariant failed: !empty()"});
    expect_dies(test, read_ordered_past_end, ::fixy::TextView{"fatal invariant failed: i < size()"});
    expect_dies(test, read_static_run_past_end, ::fixy::TextView{"fatal invariant failed: index < length"});
    expect_dies(test, read_vector_lane_past_end, ::fixy::TextView{"a read of lane 4 of a vector of 4 lanes"});
    expect_dies(test, read_mask_lane_below_zero, ::fixy::TextView{"a read of lane -1 of a mask of 4 lanes"});

    // The controls: each read inside its bounds.
    ::fixy::AppendOnly<int> const log = log_of(3);
    CRUCIBLE_TEST_ASSERT(test, log[opaque(std::size_t{2})] == 3 && log.front() == 1 && log.back() == 3);
    constexpr transition::static_run<int> run = run_of_three();
    CRUCIBLE_TEST_ASSERT(test, run[opaque(std::size_t{2})] == 6);
    Lanes4 const vector{std::int32_t{7}};
    CRUCIBLE_TEST_ASSERT(test, vector[opaque(3)] == 7 && (vector == vector)[opaque(0)]);

    pass(test, "test_base_subscript_checks: PASSED\n");
    return 0;
}
