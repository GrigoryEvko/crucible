// The Check family of fixy/check/Check.h: pass, CRUCIBLE_TEST_ASSERT and
// expect_dies.  A death test passes for a body that ends by SIGABRT with the
// expected text, and it ends the test for each other end: an exit, a
// different text, and an end by a different signal.  Each such refusal runs
// inside an outer expect_dies, which requires the report of the inner one.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/Process.h>
#include <foundation/effects/Effect.h>

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
    pass(test, "test_check_family: PASSED\n");
    return 0;
}
