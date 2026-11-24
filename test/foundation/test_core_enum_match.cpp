// Tests of foundation/core/EnumMatch.h at run time: match_enum runs the arm
// of each enumerator, a stated set gives each of its enumerators to its arm,
// the arm otherwise of an open enumeration takes each value that no other
// arm names, and a forged value of a closed enumeration ends the process.
// The values come through a call that the optimizer cannot read, so each
// match runs at run time.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/core/EnumMatch.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/Errno.h>

#include <cstdint>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

using ::foundation::core::arm;
using ::foundation::core::enum_from;
using ::foundation::core::enumerators;
using ::foundation::core::match_enum;
using ::foundation::core::otherwise;
using ::foundation::os::Errno;

template <class T>
[[gnu::noipa, nodiscard]] T opaque(T value) noexcept {
    return value;
}

enum class Kind : std::uint8_t {
    add,
    multiply,
    divide,
    negate
};

// The kinds of two operands, as a stated set beside the enumeration.
inline constexpr auto kBinaryKinds = enumerators<Kind::add, Kind::multiply, Kind::divide>;

[[nodiscard]] int apply(Kind kind, int left, int right) noexcept {
    return match_enum(kind, arm<Kind::add>([&] noexcept { return left + right; }),
                      arm<Kind::multiply>([&] noexcept { return left * right; }),
                      arm<Kind::divide>([&] noexcept { return left / right; }),
                      arm<Kind::negate>([&] noexcept { return -left; }));
}

[[nodiscard]] int operands_of(Kind kind) noexcept {
    return match_enum(kind, arm<kBinaryKinds>([] noexcept { return 2; }), arm<Kind::negate>([] noexcept { return 1; }));
}

// Each enumerator runs the body of its arm, and no other body.
void test_each_enumerator_runs_its_arm() {
    CRUCIBLE_TEST_ASSERT(kTest, apply(opaque(Kind::add), 12, 3) == 15);
    CRUCIBLE_TEST_ASSERT(kTest, apply(opaque(Kind::multiply), 12, 3) == 36);
    CRUCIBLE_TEST_ASSERT(kTest, apply(opaque(Kind::divide), 12, 3) == 4);
    CRUCIBLE_TEST_ASSERT(kTest, apply(opaque(Kind::negate), 12, 3) == -12);
}

// A stated set gives each of its enumerators to its arm.
void test_a_set_gives_each_enumerator_to_its_arm() {
    CRUCIBLE_TEST_ASSERT(kTest, operands_of(opaque(Kind::add)) == 2);
    CRUCIBLE_TEST_ASSERT(kTest, operands_of(opaque(Kind::multiply)) == 2);
    CRUCIBLE_TEST_ASSERT(kTest, operands_of(opaque(Kind::divide)) == 2);
    CRUCIBLE_TEST_ASSERT(kTest, operands_of(opaque(Kind::negate)) == 1);
}

// A match of arms with no result runs one body.
void test_a_match_with_no_result_runs_one_body() {
    int runs = 0;
    int last = 0;
    auto const run_one = [&runs, &last](Kind kind) noexcept {
        match_enum(kind, arm<kBinaryKinds>([&] noexcept {
                       ++runs;
                       last = 2;
                   }),
                   arm<Kind::negate>([&] noexcept {
                       ++runs;
                       last = 1;
                   }));
    };
    run_one(opaque(Kind::multiply));
    CRUCIBLE_TEST_ASSERT(kTest, runs == 1 && last == 2);
    run_one(opaque(Kind::negate));
    CRUCIBLE_TEST_ASSERT(kTest, runs == 2 && last == 1);
}

// The arm otherwise of the open Errno gets each number that no other arm
// names, the numbers that no name gives too.
void test_otherwise_takes_each_value_that_no_arm_names() {
    for (unsigned raw = 0; raw <= 65535; ++raw) {
        Errno const error = enum_from<Errno>(opaque(static_cast<std::uint16_t>(raw)));
        int const retry =
            match_enum(error, arm<Errno::eintr, Errno::eagain>([] noexcept { return 1; }),
                       otherwise([raw](Errno other) noexcept { return std::to_underlying(other) == raw ? 0 : -1; }));
        bool const is_retry = raw == std::to_underlying(Errno::eintr) || raw == std::to_underlying(Errno::eagain);
        CRUCIBLE_TEST_ASSERT(kTest, retry == (is_retry ? 1 : 0));
    }
}

// A value of a closed enumeration that names no enumerator comes only from a
// forged cast, and the match ends the process at it.
void test_a_forged_value_ends_the_process() {
    ::fixy::check::expect_dies(
        kTest,
        [] noexcept {
            Kind const forged = static_cast<Kind>(opaque(std::uint8_t{9}));
            static_cast<void>(apply(forged, 1, 1));
        },
        ::foundation::core::TextView{"control reached a point that the code marks as unreachable"});
}

}  // namespace

int main() {
    test_each_enumerator_runs_its_arm();
    test_a_set_gives_each_enumerator_to_its_arm();
    test_a_match_with_no_result_runs_one_body();
    test_otherwise_takes_each_value_that_no_arm_names();
    test_a_forged_value_ends_the_process();
    return 0;
}
