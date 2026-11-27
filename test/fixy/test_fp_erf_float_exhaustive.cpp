// erf of float of fixy/fp on each of the 2^32 floats, in kParts parts.  The
// command is
//
//   test_fp_erf_float_exhaustive PART PARTS
//
// and part PART takes each input whose value modulo PARTS is PART.  PARTS must
// be kParts, because each part has its own pinned digest.  CTest runs each
// part as one test (test/fixy/CMakeLists.txt).  The stride gives each part the
// same share of each path of the form, so each part has the same cost.
//
// The float form has no run-time rounding test: its fast paths round their
// double result to a float, and this test proves that each such round is the
// correctly rounded result.  A change of the table or of the polynomials of
// the float form cannot land without this test, because only a Release build
// of the gate holds it.
//
// Two checks hold for each part:
//   - The digest of the results of the part must be its pinned digest in
//     kPartDigests.  The digests are those of the results of CORE-MATH, with
//     the canonical quiet NaN for each NaN, so a change of fixy::fp::erf that
//     moves the code and the oracle below together still fails here.
//   - Each result must be the round of erf(double) of fixy/fp.  Its error is
//     below one unit in the last place of a double, so its round is the
//     correctly rounded float unless the double result is a float midpoint
//     (fixy/fp/RoundToFloat.h).  For such an input kMidpointInputs gives the
//     correctly rounded result, and the test fails for a midpoint input that
//     the list does not hold.  This check names each input that is wrong.
// Complexity: 2^32 / kParts calls of each form in each part.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/fp/Erf.h>
#include <fixy/fp/RoundToFloat.h>
#include <fixy/os/Args.h>
#include <foundation/Quarantine.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

namespace {

constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

// The count of the mismatches that the test writes.
constexpr std::uint64_t kShownMismatches = 16;

constexpr std::uint32_t kParts = 4;

// The FNV-1a digest of the bits of each result of a part, in the order of its
// inputs, four bytes of each result.  Each digest is the digest of the results
// of cr_erff of the pinned CORE-MATH on the inputs of its part.
constexpr ::fixy::FixedArray<std::uint64_t, kParts> kPartDigests{
    0xF6471B179EBDA1E5ULL,
    0x180D1189EED32F61ULL,
    0xC3487BD0BDE58AA5ULL,
    0xDAFA71EBC96FE3F1ULL,
};

constexpr std::uint64_t kFnvOffset = 0xCBF29CE484222325ULL;
constexpr std::uint64_t kFnvPrime = 0x100000001B3ULL;

[[nodiscard]] constexpr std::uint64_t fold_result(std::uint64_t digest, std::uint32_t result) noexcept {
    for (std::uint32_t byte = 0; byte < 4; ++byte) {
        digest = (digest ^ ((result >> (8 * byte)) & 0xFFu)) * kFnvPrime;
    }
    return digest;
}

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

[[nodiscard]] std::uint32_t argument_at(::fixy::os::Args const& arguments, std::size_t position) noexcept {
    return ::fixy::parse<std::uint32_t>(arguments.get(position).expect("the command is: PART PARTS").view())
        .expect("PART and PARTS are decimal integers");
}

// Checks each input of part PART of kParts.
int run_part(::fixy::os::Args const& arguments) {
    std::uint32_t const part = argument_at(arguments, 1);
    std::uint32_t const parts = argument_at(arguments, 2);
    CRUCIBLE_TEST_ASSERT(kTest, parts == kParts && part < parts);
    std::uint64_t digest = kFnvOffset;
    std::uint64_t mismatches = 0;
    std::uint64_t midpoints = 0;
    for (std::uint64_t word = part; word < (std::uint64_t{1} << 32); word += parts) {
        auto const input_bits = static_cast<std::uint32_t>(word);
        std::uint32_t const got = ::fixy::bit_cast<std::uint32_t>(::fixy::fp::erf(::fixy::bit_cast<float>(input_bits)));
        digest = fold_result(digest, got);
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
    std::uint64_t const pinned = *kPartDigests.get(part).expect("the part is below kParts");
    if (digest != pinned) {
        ::fixy::report(::fixy::Sink::Err,
                       "test_fp_erf_float_exhaustive: the results of part {} have the digest 0x{}, and the pinned "
                       "digest of CORE-MATH is 0x{}\n",
                       std::uint64_t{part}, ::fixy::Hex<16>{digest}, ::fixy::Hex<16>{pinned});
    }
    CRUCIBLE_TEST_ASSERT(kTest, midpoints == 0);
    CRUCIBLE_TEST_ASSERT(kTest, mismatches == 0);
    CRUCIBLE_TEST_ASSERT(kTest, digest == pinned);
    ::fixy::check::pass(kTest, "test_fp_erf_float_exhaustive: each float of part {} of {} is correctly rounded\n",
                        std::uint64_t{part}, std::uint64_t{parts});
    return 0;
}

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: main takes the arguments of the C runtime as a count and an array of texts")
int main(int argc, char** argv) { return run_part(::fixy::os::args(argc, argv)); }
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
