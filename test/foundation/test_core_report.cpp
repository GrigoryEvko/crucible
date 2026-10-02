// Tests of foundation/core/Report.h: fatal and unreachable end the process
// in each build, and a switch that handles each enumerator reaches
// unreachable only for a value outside the enumerators.

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
        ::foundation::test::aborts([] { ::foundation::core::fatal("the test stops here on purpose"); });
    CRUCIBLE_FATAL_INVARIANT(fatal_aborts);
}

void test_unreachable_ends_the_process() {
    bool const unreachable_aborts = ::foundation::test::aborts([] { ::foundation::core::unreachable(); });
    CRUCIBLE_FATAL_INVARIANT(unreachable_aborts);
}

// Each enumerator takes its arm.  A value of the underlying type outside
// the enumerators reaches the default arm, and the process ends.
void test_switch_reaches_unreachable_only_outside_the_enumerators() {
    CRUCIBLE_FATAL_INVARIANT(weight_of(Phase::idle) == 1);
    CRUCIBLE_FATAL_INVARIANT(weight_of(Phase::busy) == 2);
    CRUCIBLE_FATAL_INVARIANT(weight_of(Phase::done) == 3);
    for (std::uint8_t raw = 3; raw < 6; ++raw) {
        Phase const outside = static_cast<Phase>(raw);
        bool const outside_aborts = ::foundation::test::aborts([outside] { static_cast<void>(weight_of(outside)); });
        CRUCIBLE_FATAL_INVARIANT(outside_aborts);
    }
}

}  // namespace

int main() {
    test_fatal_ends_the_process();
    test_unreachable_ends_the_process();
    test_switch_reaches_unreachable_only_outside_the_enumerators();
    return 0;
}
