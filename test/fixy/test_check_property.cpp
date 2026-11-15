// The property tests of fixy/check/Property.h.  A property that holds passes
// each run.  A property that fails ends the test with the shrunk input, and
// the shrunk input is the least value that fails: 1000 for x < 1000, and
// -101 for -100 <= x on a negative range.  An oracle compares popcount of the
// Scalar family with a model that counts the bits one at a time.  Each
// failure runs inside expect_dies, which requires its report.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/check/Property.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

namespace {

using ::fixy::check::Draw;
using ::fixy::check::expect_dies;
using ::fixy::check::oracle;
using ::fixy::check::pass;
using ::fixy::check::property;

constexpr std::uint64_t kSeed = 0x5EED00000000C4EC;

void holding_property(::fixy::TestRunnerCtx const& test) {
    property(test, kSeed, 500, [](Draw& draw) noexcept {
        std::int32_t const value = draw.integer<std::int32_t>(-1000, 1000);
        return value * 2 % 2 == 0;
    });
    pass(test, "  holding_property: PASSED\n");
}

// The draws of one seed and run are the same in each replay.
void draws_are_deterministic(::fixy::TestRunnerCtx const& test) {
    std::uint64_t first_sum = 0;
    std::uint64_t second_sum = 0;
    property(test, kSeed, 50, [&first_sum](Draw& draw) noexcept {
        first_sum += draw.word();
        return true;
    });
    property(test, kSeed, 50, [&second_sum](Draw& draw) noexcept {
        second_sum += draw.word();
        return true;
    });
    CRUCIBLE_TEST_ASSERT(test, first_sum == second_sum && first_sum != 0);
    pass(test, "  draws_are_deterministic: PASSED\n");
}

// Each value of a draw is in its range, also at the limits of the type.
void draws_stay_in_range(::fixy::TestRunnerCtx const& test) {
    std::uint32_t negative_count = 0;
    std::uint32_t true_count = 0;
    property(test, kSeed, 2000, [&negative_count, &true_count](Draw& draw) noexcept {
        std::int64_t const wide =
            draw.integer<std::int64_t>(::fixy::min_of<std::int64_t>, ::fixy::max_of<std::int64_t>);
        std::uint8_t const byte = draw.integer<std::uint8_t>(3, 7);
        std::int16_t const negative = draw.integer<std::int16_t>(-300, -200);
        std::uint64_t const top =
            draw.integer<std::uint64_t>(::fixy::max_of<std::uint64_t> - 2, ::fixy::max_of<std::uint64_t>);
        negative_count += wide < 0 ? 1U : 0U;
        true_count += draw.boolean() ? 1U : 0U;
        return byte >= 3 && byte <= 7 && negative >= -300 && negative <= -200
            && top >= ::fixy::max_of<std::uint64_t> - 2;
    });
    // Each side of a range and each bool comes up in about half of the runs.
    CRUCIBLE_TEST_ASSERT(test, negative_count > 800 && negative_count < 1200);
    CRUCIBLE_TEST_ASSERT(test, true_count > 800 && true_count < 1200);
    pass(test, "  draws_stay_in_range: PASSED\n");
}

void failing_property_shrinks(::fixy::TestRunnerCtx const& test) {
    expect_dies(
        test,
        [&test] noexcept {
            property(test, kSeed, 500,
                     [](Draw& draw) noexcept { return draw.integer<std::int64_t>(-1000000, 1000000) < 1000; });
        },
        ::fixy::TextView{"property: the values of the shrunk input: 1000\n"});
    expect_dies(
        test,
        [&test] noexcept {
            property(test, kSeed, 500,
                     [](Draw& draw) noexcept { return draw.integer<std::int32_t>(-100000, -10) >= -100; });
        },
        ::fixy::TextView{"property: the values of the shrunk input: -101\n"});
    pass(test, "  failing_property_shrinks: PASSED\n");
}

// The model counts the bits of a word one at a time.
[[nodiscard]] int count_bits_one_at_a_time(std::uint64_t word) noexcept {
    int count = 0;
    for (int bit = 0; bit < 64; ++bit) {
        count += static_cast<int>((word >> bit) & 1U);
    }
    return count;
}

void oracle_agrees(::fixy::TestRunnerCtx const& test) {
    oracle(
        test, kSeed, 2000, [](Draw& draw) noexcept { return draw.word(); },
        [](std::uint64_t const& word) noexcept { return ::fixy::popcount(word); },
        [](std::uint64_t const& word) noexcept { return count_bits_one_at_a_time(word); });
    pass(test, "  oracle_agrees: PASSED\n");
}

// A model with a defect at the value 7 makes the oracle fail, and the shrink
// finds 7.
void oracle_finds_the_difference(::fixy::TestRunnerCtx const& test) {
    expect_dies(
        test,
        [&test] noexcept {
            oracle(
                test, kSeed, 5000, [](Draw& draw) noexcept { return draw.integer<std::uint32_t>(0, 50); },
                [](std::uint32_t const& value) noexcept { return value; },
                [](std::uint32_t const& value) noexcept { return value == 7 ? 8U : value; });
        },
        ::fixy::TextView{"property: the values of the shrunk input: 7\n"});
    pass(test, "  oracle_finds_the_difference: PASSED\n");
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    holding_property(test);
    draws_are_deterministic(test);
    draws_stay_in_range(test);
    failing_property_shrinks(test);
    oracle_agrees(test);
    oracle_finds_the_difference(test);
    pass(test, "test_check_property: PASSED\n");
    return 0;
}
