// Tests of the trap checks of a Release build (cmake/CodeFlags.cmake, 196b).
// Each body reads its operands from volatile objects, so no operation folds
// at compile time, and writes its result to a volatile object, so no
// operation is dead.  Each body runs in a child.  An integer division and a
// remainder by zero, the quotient of the minimum by -1, a shift by the width
// of the type, and a cast of a float past the range of an integer type each
// end the child with a trap: the instruction ud2 and SIGILL on x86_64, and
// brk and SIGTRAP on aarch64.  With no check, x86_64 gave SIGFPE or the value
// 0x80...0, and aarch64 gave 0, the minimum or a saturated value.  The test
// builds only in a build other than Debug, because a Debug build of the tests
// reports the same checks through UBSan.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/Signal.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

namespace {

using ::fixy::check::expect_dies_by;
using ::fixy::check::pass;
using ::fixy::signal::FatalSignal;

#if defined(__aarch64__)
constexpr FatalSignal kTrapSignal = FatalSignal::breakpoint_trap;
#else
constexpr FatalSignal kTrapSignal = FatalSignal::illegal_instruction;
#endif

void divide_by_zero() noexcept {
    volatile std::int64_t seven = 7;
    volatile std::int64_t zero = 0;
    volatile std::int64_t const result = seven / zero;
    static_cast<void>(result);
}

void take_remainder_by_zero() noexcept {
    volatile std::int64_t seven = 7;
    volatile std::int64_t zero = 0;
    volatile std::int64_t const result = seven % zero;
    static_cast<void>(result);
}

void divide_minimum_by_minus_one() noexcept {
    volatile std::int64_t minimum = ::fixy::min_of<std::int64_t>;
    volatile std::int64_t minus_one = -1;
    volatile std::int64_t const result = minimum / minus_one;
    static_cast<void>(result);
}

void shift_by_width() noexcept {
    volatile std::int64_t seven = 7;
    volatile int width = 64;
    volatile std::int64_t const result = seven << width;
    static_cast<void>(result);
}

void cast_huge_double() noexcept {
    volatile double huge = 1e30;
    volatile std::int64_t const result = static_cast<std::int64_t>(huge);
    static_cast<void>(result);
}

void cast_not_a_number() noexcept {
    volatile double not_a_number = __builtin_nan("");
    volatile std::int64_t const result = static_cast<std::int64_t>(not_a_number);
    static_cast<void>(result);
}

// Each operation with no defined result ends the child with the trap.
void each_check_traps(::fixy::TestRunnerCtx const& test) noexcept {
    expect_dies_by(test, divide_by_zero, kTrapSignal);
    expect_dies_by(test, take_remainder_by_zero, kTrapSignal);
    expect_dies_by(test, divide_minimum_by_minus_one, kTrapSignal);
    expect_dies_by(test, shift_by_width, kTrapSignal);
    expect_dies_by(test, cast_huge_double, kTrapSignal);
    expect_dies_by(test, cast_not_a_number, kTrapSignal);
    pass(test, "  each_check_traps: PASSED\n");
}

// The operands of a defined result pass each check in the test process.
void defined_operations_pass(::fixy::TestRunnerCtx const& test) noexcept {
    volatile std::int64_t seven = 7;
    volatile std::int64_t minimum = ::fixy::min_of<std::int64_t>;
    volatile std::int64_t minus_one = -1;
    volatile int count = 60;
    volatile double huge = 1e30;
    std::int64_t const quotient = seven / minus_one;
    std::int64_t const remainder = minimum % seven;
    std::int64_t const shifted = seven << count;
    std::int64_t const truncated = static_cast<std::int64_t>(huge / 1e20);
    CRUCIBLE_TEST_ASSERT(test, quotient == -7);
    CRUCIBLE_TEST_ASSERT(test, remainder == -1);
    CRUCIBLE_TEST_ASSERT(test, shifted == std::int64_t{7} << 60);
    CRUCIBLE_TEST_ASSERT(test, truncated == 10000000000);
    pass(test, "  defined_operations_pass: PASSED\n");
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    defined_operations_pass(test);
    each_check_traps(test);
    pass(test, "test_release_traps: all tests passed\n");
    return 0;
}
