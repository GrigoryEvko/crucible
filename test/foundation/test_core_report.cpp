// Tests of foundation/core/Report.h: fatal and unreachable end the process
// in each build, a report returns, and a switch that handles each
// enumerator reaches unreachable only for a value outside the enumerators.

#include <foundation/core/Report.h>

#include <foundation/Platform.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

// Each end of the process below runs in a child through expect_dies of the
// Check family.

namespace {

using ::fixy::TestRunnerCtx;
using ::fixy::check::expect_dies;

enum class Phase : std::uint8_t {
    idle,
    busy,
    done
};

// A switch that handles each enumerator.  The default arm is the point that
// control never reaches for an enumerator.
[[nodiscard]] int weight_of(Phase phase) noexcept {
    switch (phase) {
        case Phase::idle:
            return 1;
        case Phase::busy:
            return 2;
        case Phase::done:
            return 3;
        default:
            ::foundation::core::unreachable();
    }
}

void test_fatal_ends_the_process(TestRunnerCtx const& test) {
    expect_dies(
        test, [] noexcept { ::foundation::core::fatal("the test stops here on purpose"); },
        ::foundation::core::TextView{"foundation: fatal: the test stops here on purpose"});
}

void test_unreachable_ends_the_process(TestRunnerCtx const& test) {
    expect_dies(
        test, [] noexcept { ::foundation::core::unreachable(); },
        ::foundation::core::TextView{"foundation: fatal: control reached a point that the code marks as unreachable"});
}

// A fatal exit with arguments ends the process too, and its text holds the
// formatted arguments.
void test_fatal_with_arguments_ends_the_process(TestRunnerCtx const& test) {
    expect_dies(
        test,
        [] noexcept {
            ::foundation::core::fatal("the test stops at {} of {} on purpose, {}", 3, -4,
                                      ::foundation::core::TextView{"with a text"});
        },
        ::foundation::core::TextView{"foundation: fatal: the test stops at 3 of -4 on purpose, with a text"});
}

// A report returns, and a report with no argument writes its escapes as
// one brace each.
void test_report_returns() {
    ::foundation::core::report(::foundation::core::Sink::Out, "test_core_report: a report of {} and {}\n", 7, false);
    ::foundation::core::report(::foundation::core::Sink::Out, "test_core_report: {{a brace pair}}\n");
}

// Each enumerator takes its arm.  A value of the underlying type outside
// the enumerators reaches the default arm, and the process ends.
void test_switch_reaches_unreachable_only_outside_the_enumerators(TestRunnerCtx const& test) {
    CRUCIBLE_FATAL_INVARIANT(weight_of(Phase::idle) == 1);
    CRUCIBLE_FATAL_INVARIANT(weight_of(Phase::busy) == 2);
    CRUCIBLE_FATAL_INVARIANT(weight_of(Phase::done) == 3);
    for (std::uint8_t raw = 3; raw < 6; ++raw) {
        Phase const outside = static_cast<Phase>(raw);
        expect_dies(
            test, [outside] noexcept { static_cast<void>(weight_of(outside)); },
            ::foundation::core::TextView{
                "foundation: fatal: control reached a point that the code marks as unreachable"});
    }
}

}  // namespace

int main() {
    TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_fatal_ends_the_process(test);
    test_unreachable_ends_the_process(test);
    test_fatal_with_arguments_ends_the_process(test);
    test_report_returns();
    test_switch_reaches_unreachable_only_outside_the_enumerators(test);
    return 0;
}
