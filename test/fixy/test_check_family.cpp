// The Check family of fixy/check/Check.h: pass, CRUCIBLE_TEST_ASSERT,
// expect_dies and the counting check of a Tally, and run_of of
// fixy/check/Fixture.h.  A death test passes for a body that ends by SIGABRT with the
// expected text, and it ends the test for each other end: an exit, a
// different text, and an end by a different signal.  Each such refusal runs
// inside an outer expect_dies, which requires the report of the inner one.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/check/Fixture.h>
#include <fixy/os/Process.h>
#include <foundation/Linear.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <utility>

namespace {

using ::fixy::check::expect_dies;
using ::fixy::check::pass;

constexpr ::fixy::TextView kRefusal{"expect_dies: the body did not end as the test expects"};

void assert_holds(::fixy::TestRunnerCtx const& test) {
    int const answer = 42;
    CRUCIBLE_TEST_ASSERT(test, answer == 42);
    pass(test, "  assert_holds: PASSED\n");
}

void assert_fails(::fixy::TestRunnerCtx const& test) {
    expect_dies(
        test,
        [&test] noexcept {
            int const answer = 41;
            CRUCIBLE_TEST_ASSERT(test, answer == 42);
        },
        ::fixy::TextView{"assert: answer == 42"});
    pass(test, "  assert_fails: PASSED\n");
}

// The sum of the two template arguments, so a call of it holds a comma.
template <int First, int Second>
[[nodiscard]] constexpr int sum_of() noexcept {
    return First + Second;
}

// A condition can hold the commas of a template argument list, and the
// report writes the full condition.
void assert_with_commas(::fixy::TestRunnerCtx const& test) {
    CRUCIBLE_TEST_ASSERT(test, sum_of<40, 2>() == 42);
    expect_dies(
        test, [&test] noexcept { CRUCIBLE_TEST_ASSERT(test, sum_of<40, 1>() == 42); },
        ::fixy::TextView{"assert: sum_of<40, 1>() == 42"});
    pass(test, "  assert_with_commas: PASSED\n");
}

void death_with_text(::fixy::TestRunnerCtx const& test) {
    expect_dies(test, [] noexcept { ::fixy::fatal("the body ends here"); }, ::fixy::TextView{"the body ends here"});
    pass(test, "  death_with_text: PASSED\n");
}

// The inner body returns, so the inner child exits with the code 0.
void exit_is_refused(::fixy::TestRunnerCtx const& test) {
    expect_dies(test, [&test] noexcept { expect_dies(test, [] noexcept {}, ::fixy::TextView{"no text"}); }, kRefusal);
    expect_dies(
        test, [&test] noexcept { expect_dies(test, [] noexcept {}, ::fixy::TextView{"no text"}); },
        ::fixy::TextView{"the child ended by the exit code 0"});
    pass(test, "  exit_is_refused: PASSED\n");
}

// The inner body aborts with a text that is not the expected text.
void other_text_is_refused(::fixy::TestRunnerCtx const& test) {
    expect_dies(
        test,
        [&test] noexcept {
            expect_dies(test, [] noexcept { ::fixy::fatal("the first text"); }, ::fixy::TextView{"the second text"});
        },
        kRefusal);
    pass(test, "  other_text_is_refused: PASSED\n");
}

// The inner body ends with an exit code through the child door, and writes
// the expected text first.
void exit_with_text_is_refused(::fixy::TestRunnerCtx const& test) {
    expect_dies(
        test,
        [&test] noexcept {
            expect_dies(
                test,
                [&test] noexcept {
                    ::fixy::report(::fixy::Sink::Err, "the expected text\n");
                    ::fixy::process::exit_now(test, 9);
                },
                ::fixy::TextView{"the expected text"});
        },
        ::fixy::TextView{"the child ended by the exit code 9"});
    pass(test, "  exit_with_text_is_refused: PASSED\n");
}

// A Tally whose checks hold closes with no end of the test.
void tally_of_held_checks_closes(::fixy::TestRunnerCtx const& test) {
    ::fixy::check::Tally checks{test, "tally_of_held_checks"};
    int const answer = 42;
    CRUCIBLE_TEST_EXPECT(checks, answer == 42);
    CRUCIBLE_TEST_EXPECT(checks, sum_of<40, 2>() == 42);
    std::move(checks).close();
    pass(test, "  tally_of_held_checks_closes: PASSED\n");
}

// The checks of a Tally in a child: the first fails, the second holds, and
// the third fails with the commas of a template argument list.  The close
// then ends the child.
void run_two_failed_checks(::fixy::TestRunnerCtx const& test) noexcept {
    ::fixy::check::Tally checks{test, "tally_of_failed_checks"};
    int const answer = 41;
    CRUCIBLE_TEST_EXPECT(checks, answer == 42);
    CRUCIBLE_TEST_EXPECT(checks, answer == 41);
    CRUCIBLE_TEST_EXPECT(checks, sum_of<40, 1>() == 42);
    std::move(checks).close();
}

// A failed check writes its condition and its site, the test continues to
// the next check, and the close ends the test with the counts.
void tally_of_failed_checks_ends_at_close(::fixy::TestRunnerCtx const& test) {
    expect_dies(
        test, [&test] noexcept { run_two_failed_checks(test); },
        ::fixy::TextView{"tally_of_failed_checks: 2 of 3 checks failed"});
    expect_dies(
        test, [&test] noexcept { run_two_failed_checks(test); },
        ::fixy::TextView{"tally_of_failed_checks: check failed: answer == 42 at "});
    expect_dies(
        test, [&test] noexcept { run_two_failed_checks(test); },
        ::fixy::TextView{"tally_of_failed_checks: check failed: sum_of<40, 1>() == 42 at "});
    expect_dies(test, [&test] noexcept { run_two_failed_checks(test); }, ::fixy::TextView{"test_check_family.cpp:"});
    expect_dies(
        test, [&test] noexcept { run_two_failed_checks(test); },
        ::fixy::TextView{"a check of the Tally failed, so the test fails at its close"});
    pass(test, "  tally_of_failed_checks_ends_at_close: PASSED\n");
}

// A Tally that ends with no close ends the process with the name of its
// test, also when each of its checks held.  The drop door of
// foundation/Linear.h ends a linear value that the rule must-consume sees
// as consumed, and the destructor of the Tally still refuses it.
void tally_with_no_close_ends_the_test(::fixy::TestRunnerCtx const& test) {
    auto const drop_with_no_close = [&test] noexcept {
        ::fixy::check::Tally checks{test, "tally_with_no_close"};
        CRUCIBLE_TEST_EXPECT(checks, sum_of<40, 2>() == 42);
        ::foundation::linear::drop(std::move(checks));
    };
    expect_dies(test, drop_with_no_close,
                ::fixy::TextView{"tally_with_no_close: the Tally of the test ended with no close"});
    expect_dies(test, drop_with_no_close,
                ::fixy::TextView{
                    "the Tally of the test tally_with_no_close ended with no close, so its failures would be lost"});
    pass(test, "  tally_with_no_close_ends_the_test: PASSED\n");
}

// A move gives the counts to the new Tally, and the old one ends nothing.
void tally_move_carries_the_counts(::fixy::TestRunnerCtx const& test) {
    ::fixy::check::Tally first{test, "tally_move"};
    CRUCIBLE_TEST_EXPECT(first, sum_of<40, 2>() == 42);
    ::fixy::check::Tally second{std::move(first)};
    CRUCIBLE_TEST_EXPECT(second, sum_of<41, 1>() == 42);
    std::move(second).close();
    expect_dies(
        test,
        [&test] noexcept {
            ::fixy::check::Tally failing{test, "tally_move_of_a_failure"};
            CRUCIBLE_TEST_EXPECT(failing, sum_of<40, 1>() == 42);
            ::fixy::check::Tally moved{std::move(failing)};
            std::move(moved).close();
        },
        ::fixy::TextView{"tally_move_of_a_failure: 1 of 1 checks failed"});
    pass(test, "  tally_move_carries_the_counts: PASSED\n");
}

// run_of gives a new array of the arena that holds the values in order.  Each
// value after the first converts to the type of the first.
void run_of_holds_the_values(::fixy::TestRunnerCtx const& test) {
    ::fixy::BumpArena arena{1 << 12};
    ::fixy::View<std::uint32_t> const run =
        ::fixy::check::run_of(test, test.cap().alloc, arena, std::uint32_t{7}, 9, 11);
    CRUCIBLE_TEST_ASSERT(test, run.size() == 3);
    CRUCIBLE_TEST_ASSERT(test, *run.get(0).expect("the run holds three values") == 7);
    CRUCIBLE_TEST_ASSERT(test, *run.get(1).expect("the run holds three values") == 9);
    CRUCIBLE_TEST_ASSERT(test, *run.get(2).expect("the run holds three values") == 11);
    ::fixy::View<std::uint32_t> const single = ::fixy::check::run_of(test, test.cap().alloc, arena, std::uint32_t{5});
    CRUCIBLE_TEST_ASSERT(test, single.size() == 1);
    CRUCIBLE_TEST_ASSERT(test, *single.get(0).expect("the run holds one value") == 5);
    pass(test, "  run_of_holds_the_values: PASSED\n");
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    assert_holds(test);
    assert_fails(test);
    assert_with_commas(test);
    death_with_text(test);
    exit_is_refused(test);
    other_text_is_refused(test);
    exit_with_text_is_refused(test);
    tally_of_held_checks_closes(test);
    tally_of_failed_checks_ends_at_close(test);
    tally_with_no_close_ends_the_test(test);
    tally_move_carries_the_counts(test);
    run_of_holds_the_values(test);
    pass(test, "test_check_family: PASSED\n");
    return 0;
}
