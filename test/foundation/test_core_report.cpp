// Tests of foundation/core/Report.h: fatal and unreachable end the process
// in each build, a report returns, and a switch that handles each
// enumerator reaches unreachable only for a value outside the enumerators.

#include <foundation/core/Report.h>

#include <foundation/Platform.h>

#include "abort_probe.h"

#include <cstdint>

namespace {

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

void test_fatal_ends_the_process() {
    bool const fatal_aborts =
        ::crucible::test::aborts([] { ::foundation::core::fatal("the test stops here on purpose"); });
    CRUCIBLE_FATAL_INVARIANT(fatal_aborts);
}

void test_unreachable_ends_the_process() {
    bool const unreachable_aborts = ::crucible::test::aborts([] { ::foundation::core::unreachable(); });
    CRUCIBLE_FATAL_INVARIANT(unreachable_aborts);
}

// A fatal exit with arguments ends the process too.  test/harness/
// check_output.py reads the text of a fatal exit.
void test_fatal_with_arguments_ends_the_process() {
    bool const fatal_aborts = ::crucible::test::aborts([] {
        ::foundation::core::fatal("the test stops at {} of {} on purpose, {}", 3, -4,
                                  ::foundation::core::TextView{"with a text"});
    });
    CRUCIBLE_FATAL_INVARIANT(fatal_aborts);
}

// A report returns, and a report with no argument writes its escapes as
// one brace each.
void test_report_returns() {
    ::foundation::core::report(::foundation::core::Sink::Out, "test_core_report: a report of {} and {}\n", 7, false);
    ::foundation::core::report(::foundation::core::Sink::Out, "test_core_report: {{a brace pair}}\n");
}

// Each enumerator takes its arm.  A value of the underlying type outside
// the enumerators reaches the default arm, and the process ends.
void test_switch_reaches_unreachable_only_outside_the_enumerators() {
    CRUCIBLE_FATAL_INVARIANT(weight_of(Phase::idle) == 1);
    CRUCIBLE_FATAL_INVARIANT(weight_of(Phase::busy) == 2);
    CRUCIBLE_FATAL_INVARIANT(weight_of(Phase::done) == 3);
    for (std::uint8_t raw = 3; raw < 6; ++raw) {
        Phase const outside = static_cast<Phase>(raw);
        bool const outside_aborts = ::crucible::test::aborts([outside] { static_cast<void>(weight_of(outside)); });
        CRUCIBLE_FATAL_INVARIANT(outside_aborts);
    }
}

// The abort probe has one jump buffer, so one arm at a time is open.  A
// second arm while the first arm is open is a fatal exit, and the first arm
// catches it.  After that, the probe arms again.
void test_abort_probe_refuses_a_second_open_arm() {
    bool const second_arm_aborts = ::crucible::test::aborts([] { static_cast<void>(::crucible::test::aborts([] {})); });
    CRUCIBLE_FATAL_INVARIANT(second_arm_aborts);
    bool const later_arm_aborts = ::crucible::test::aborts([] { ::foundation::core::unreachable(); });
    CRUCIBLE_FATAL_INVARIANT(later_arm_aborts);
}

}  // namespace

int main() {
    test_fatal_ends_the_process();
    test_unreachable_ends_the_process();
    test_fatal_with_arguments_ends_the_process();
    test_report_returns();
    test_switch_reaches_unreachable_only_outside_the_enumerators();
    test_abort_probe_refuses_a_second_open_arm();
    return 0;
}
