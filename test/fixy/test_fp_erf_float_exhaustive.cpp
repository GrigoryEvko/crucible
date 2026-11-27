// erf of float of fixy/fp on each of the 2^32 floats.
//
// The float form has no run-time rounding test: its fast paths round their
// double result to a float, and this test proves that each such round is the
// correctly rounded result.  A change of the table or of the polynomials of
// the float form cannot land without this test, because only a Release build
// of the gate holds it (test/fixy/CMakeLists.txt).
//
// The oracle is the round of erf(double) of fixy/fp.  Its error is below one
// unit in the last place of a double, so its round is the correctly rounded
// float unless the double result is a float midpoint (fixy/fp/RoundToFloat.h).
// For such an input the oracle cannot decide, and kMidpointInputs gives the
// correctly rounded result.  The test fails for a midpoint input that the list
// does not hold.  The sweep of bench_fp_sweep against CORE-MATH agrees with
// the oracle and the list on each float input.  A NaN result must have the
// bits of the canonical quiet NaN.  Complexity: 2^32 calls of each form.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/fp/Erf.h>
#include <fixy/fp/RoundToFloat.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

namespace {

constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

// The count of the mismatches that the test writes.
constexpr std::uint64_t kShownMismatches = 16;

// One input and its correctly rounded result, as bits.
struct FloatCase {
    std::uint32_t input = 0;
    std::uint32_t expected = 0;
};

// Each input whose erf(double) is a float midpoint, with the correctly
// rounded result of CORE-MATH.  The true value of erf(0x1.81d5acp-13) is just
// above the midpoint, so the result rounds up.
constexpr ::fixy::FixedArray<FloatCase, 2> kMidpointInputs{
    FloatCase{0x3940EAD6u, 0x3959AF14u},
    FloatCase{0xB940EAD6u, 0xB959AF14u},
};

// The expected bits of erf of one float input: the canonical NaN for a NaN,
// the result of kMidpointInputs for a midpoint input, and the round of
// erf(double) for each other input.  None for a midpoint input that the list
// does not hold.
[[nodiscard]] ::fixy::Option<std::uint32_t> expected_of(std::uint32_t input_bits) noexcept {
    double const reference = ::fixy::fp::erf(static_cast<double>(::fixy::bit_cast<float>(input_bits)));
    if (::fixy::fp::is_nan(reference))
        return ::fixy::some(::fixy::bit_cast<std::uint32_t>(::fixy::quiet_nan_of<float>));
    if (!::fixy::fp::detail::is_float_midpoint_(reference))
        return ::fixy::some(::fixy::bit_cast<std::uint32_t>(static_cast<float>(reference)));
    for (FloatCase const& entry : kMidpointInputs) {
        if (entry.input == input_bits) return ::fixy::some(entry.expected);
    }
    return ::fixy::none;
}

void test_each_float_is_correctly_rounded() {
    std::uint64_t mismatches = 0;
    std::uint64_t midpoints = 0;
    for (std::uint64_t word = 0; word < (std::uint64_t{1} << 32); ++word) {
        auto const input_bits = static_cast<std::uint32_t>(word);
        std::uint32_t const got = ::fixy::bit_cast<std::uint32_t>(::fixy::fp::erf(::fixy::bit_cast<float>(input_bits)));
        ::fixy::Option<std::uint32_t> const expected = expected_of(input_bits);
        if (expected.is_none()) {
            ::fixy::report(::fixy::Sink::Err,
                           "test_fp_erf_float_exhaustive: erf(double) of 0x{} is a float midpoint that "
                           "kMidpointInputs does not hold, so the oracle cannot decide.  Add the result of "
                           "CORE-MATH for this input to the list.\n",
                           ::fixy::Hex<8>{input_bits});
            ++midpoints;
            continue;
        }
        std::uint32_t const expected_bits = ::fixy::Option<std::uint32_t>{expected}.expect("the oracle decides");
        if (got == expected_bits) continue;
        if (mismatches < kShownMismatches) {
            ::fixy::report(::fixy::Sink::Err,
                           "test_fp_erf_float_exhaustive: erf of 0x{} gives 0x{}, and the correct round is 0x{}\n",
                           ::fixy::Hex<8>{input_bits}, ::fixy::Hex<8>{got}, ::fixy::Hex<8>{expected_bits});
        }
        ++mismatches;
    }
    CRUCIBLE_TEST_ASSERT(kTest, midpoints == 0);
    CRUCIBLE_TEST_ASSERT(kTest, mismatches == 0);
    ::fixy::check::pass(kTest, "test_fp_erf_float_exhaustive: each of the 2^32 floats is correctly rounded\n");
}

}  // namespace

int main() {
    test_each_float_is_correctly_rounded();
    return 0;
}
