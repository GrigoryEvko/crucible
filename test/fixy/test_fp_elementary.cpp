// exp, log and erf of fixy/fp: the special values, the edges, the inputs of
// the slow paths, and the same bits on each level of x86-64.
//
// Each expected value is a bit pattern.  The value of each finite result is
// the correctly rounded result of CORE-MATH, which the sweep of the target
// bench_fp_sweep also reads.  For the double functions, these inputs are
// inputs where the bounded result is also the correctly rounded result.  Each
// NaN result is the canonical quiet NaN of the base.
//
// The digests: the unit of the test and each unit of a level compute the
// digests of fp_elementary_forms.h with their own bodies, and each digest must
// be the literal of kDigests.  So each level of x86-64 gives the same bits,
// the default build (Debug) gives the bits of the release builds, and a build
// on aarch64 gives them too.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/X86Level.h>

#include "fp_elementary_forms.h"

#include <cstddef>
#include <cstdint>

namespace {

namespace fe = ::test_fp_elementary;

constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

// The digests of each function on its sample, on each level and each target.
constexpr fe::FpDigests kDigests{
    0x10BAF092FEFD09FDULL, 0x48B3390E0E753230ULL, 0x81567B07633715BFULL,
    0x578109E55FF5FA19ULL, 0x13CAEAE827152888ULL, 0x466D780B813F634AULL,
};

// One input and its expected result, as bits.
struct DoubleCase {
    std::uint64_t input = 0;
    std::uint64_t expected = 0;
};

struct FloatCase {
    std::uint32_t input = 0;
    std::uint32_t expected = 0;
};

constexpr std::uint64_t kNan64 = 0x7FF8000000000000ULL;
constexpr std::uint32_t kNan32 = 0x7FC00000u;

// Reading through a volatile keeps the optimizer from folding a call into a
// constant, so each call runs the code of the build.
[[nodiscard]] double opaque_double(std::uint64_t bits) noexcept {
    volatile std::uint64_t sink = bits;
    return ::fixy::bit_cast<double>(std::uint64_t{sink});
}

[[nodiscard]] float opaque_float(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink = bits;
    return ::fixy::bit_cast<float>(std::uint32_t{sink});
}

template <std::size_t Count, class Function>
void check_double_cases(::fixy::TextView name, ::fixy::FixedArray<DoubleCase, Count> const& cases,
                        Function const& function) {
    for (DoubleCase const& item : cases) {
        auto const got = ::fixy::bit_cast<std::uint64_t>(function(opaque_double(item.input)));
        if (got != item.expected) {
            ::fixy::report(::fixy::Sink::Err, "test_fp_elementary: {} of 0x{} gives 0x{}, the expected bits are 0x{}\n",
                           name, ::fixy::Hex<16>{item.input}, ::fixy::Hex<16>{got}, ::fixy::Hex<16>{item.expected});
        }
        CRUCIBLE_TEST_ASSERT(kTest, got == item.expected);
    }
}

template <std::size_t Count, class Function>
void check_float_cases(::fixy::TextView name, ::fixy::FixedArray<FloatCase, Count> const& cases,
                       Function const& function) {
    for (FloatCase const& item : cases) {
        auto const got = ::fixy::bit_cast<std::uint32_t>(function(opaque_float(item.input)));
        if (got != item.expected) {
            ::fixy::report(::fixy::Sink::Err, "test_fp_elementary: {} of 0x{} gives 0x{}, the expected bits are 0x{}\n",
                           name, ::fixy::Hex<8>{item.input}, ::fixy::Hex<8>{got}, ::fixy::Hex<8>{item.expected});
        }
        CRUCIBLE_TEST_ASSERT(kTest, got == item.expected);
    }
}

// exp of double: the zeros, the infinities, the NaNs (quiet and signaling),
// exp(1), the overflow edge, the underflow edge to the smallest subnormal and
// to +0, the subnormal edge, a subnormal result, the tiny inputs that round to
// 1, and the edges of the path past the normal range.
constexpr ::fixy::FixedArray<DoubleCase, 19> kExpDoubleCases{
    DoubleCase{0x0000000000000000ULL, 0x3FF0000000000000ULL},
    DoubleCase{0x8000000000000000ULL, 0x3FF0000000000000ULL},
    DoubleCase{0x7FF0000000000000ULL, 0x7FF0000000000000ULL},
    DoubleCase{0xFFF0000000000000ULL, 0x0000000000000000ULL},
    DoubleCase{0x7FF8000000000001ULL, kNan64},
    DoubleCase{0x7FF0000000000001ULL, kNan64},
    DoubleCase{0xFFF8000000000000ULL, kNan64},
    DoubleCase{0x3FF0000000000000ULL, 0x4005BF0A8B145769ULL},
    DoubleCase{0x40862E42FEFA39EFULL, 0x7FEFFFFFFFFFFF2AULL},
    DoubleCase{0x40862E42FEFA39F0ULL, 0x7FF0000000000000ULL},
    DoubleCase{0xC0874910D52D3051ULL, 0x0000000000000001ULL},
    DoubleCase{0xC0874910D52D3052ULL, 0x0000000000000000ULL},
    DoubleCase{0xC086232BDD7ABCD2ULL, 0x001000000000007CULL},
    DoubleCase{0xC087200000000000ULL, 0x0000000000000055ULL},
    DoubleCase{0x0000000000000001ULL, 0x3FF0000000000000ULL},
    DoubleCase{0x3C90000000000000ULL, 0x3FF0000000000000ULL},
    DoubleCase{0xBC90000000000000ULL, 0x3FF0000000000000ULL},
    DoubleCase{0x4080000000000000ULL, 0x6E19476504BA852EULL},
    DoubleCase{0xC080000000000000ULL, 0x11C44109EDB20931ULL},
};

// log of double: the zeros, the infinities, the negative values (a normal, a
// subnormal and -inf), a NaN, 1 and its neighbors, the smallest subnormal,
// the largest subnormal, the smallest normal, the largest finite value, the
// edges of the path near 1, and 2.
constexpr ::fixy::FixedArray<DoubleCase, 17> kLogDoubleCases{
    DoubleCase{0x0000000000000000ULL, 0xFFF0000000000000ULL},
    DoubleCase{0x8000000000000000ULL, 0xFFF0000000000000ULL},
    DoubleCase{0x7FF0000000000000ULL, 0x7FF0000000000000ULL},
    DoubleCase{0xFFF0000000000000ULL, kNan64},
    DoubleCase{0xBFF0000000000000ULL, kNan64},
    DoubleCase{0x8000000000000001ULL, kNan64},
    DoubleCase{0x7FF8000000000001ULL, kNan64},
    DoubleCase{0x3FF0000000000000ULL, 0x0000000000000000ULL},
    DoubleCase{0x3FEFFFFFFFFFFFFFULL, 0xBCA0000000000000ULL},
    DoubleCase{0x3FF0000000000001ULL, 0x3CAFFFFFFFFFFFFFULL},
    DoubleCase{0x0000000000000001ULL, 0xC0874385446D71C3ULL},
    DoubleCase{0x000FFFFFFFFFFFFFULL, 0xC086232BDD7ABCD2ULL},
    DoubleCase{0x0010000000000000ULL, 0xC086232BDD7ABCD2ULL},
    DoubleCase{0x7FEFFFFFFFFFFFFFULL, 0x40862E42FEFA39EFULL},
    DoubleCase{0x3FEE000000000000ULL, 0xBFB08598B59E3A07ULL},
    DoubleCase{0x3FF1090000000000ULL, 0x3FB00C7C14184EC5ULL},
    DoubleCase{0x4000000000000000ULL, 0x3FE62E42FEFA39EFULL},
};

// erf of double: the zeros with their signs, the infinities, a NaN, the
// smallest subnormal of each sign, the largest subnormal, 2^-28 (the first
// input of the odd polynomial), 1/2 (the first input of the intervals), 1,
// the edge to ±1 and the value below it, and 6.
constexpr ::fixy::FixedArray<DoubleCase, 15> kErfDoubleCases{
    DoubleCase{0x0000000000000000ULL, 0x0000000000000000ULL},
    DoubleCase{0x8000000000000000ULL, 0x8000000000000000ULL},
    DoubleCase{0x7FF0000000000000ULL, 0x3FF0000000000000ULL},
    DoubleCase{0xFFF0000000000000ULL, 0xBFF0000000000000ULL},
    DoubleCase{0x7FF8000000000001ULL, kNan64},
    DoubleCase{0x0000000000000001ULL, 0x0000000000000001ULL},
    DoubleCase{0x8000000000000001ULL, 0x8000000000000001ULL},
    DoubleCase{0x000FFFFFFFFFFFFFULL, 0x00120DD750429B6CULL},
    DoubleCase{0x3E30000000000000ULL, 0x3E320DD750429B6DULL},
    DoubleCase{0x3FE0000000000000ULL, 0x3FE0A7EF5C18EDD2ULL},
    DoubleCase{0x3FF0000000000000ULL, 0x3FEAF767A741088BULL},
    DoubleCase{0x4017AFB48DC96627ULL, 0x3FF0000000000000ULL},
    DoubleCase{0x4017AFB48DC96626ULL, 0x3FEFFFFFFFFFFFFFULL},
    DoubleCase{0xC017AFB48DC96627ULL, 0xBFF0000000000000ULL},
    DoubleCase{0x4018000000000000ULL, 0x3FF0000000000000ULL},
};

// exp of float: as for double, with the edges of the float form, and each
// side of the positive edge of the fast path at 0x1.5d589ep+6.
constexpr ::fixy::FixedArray<FloatCase, 19> kExpFloatCases{
    FloatCase{0x00000000u, 0x3F800000u}, FloatCase{0x80000000u, 0x3F800000u}, FloatCase{0x7F800000u, 0x7F800000u},
    FloatCase{0xFF800000u, 0x00000000u}, FloatCase{0x7FC00001u, kNan32},      FloatCase{0x7F800001u, kNan32},
    FloatCase{0x3F800000u, 0x402DF854u}, FloatCase{0x42B17217u, 0x7F7FFF84u}, FloatCase{0x42B17218u, 0x7F800000u},
    FloatCase{0xC2CFF1B4u, 0x00000001u}, FloatCase{0xC2CFF1B5u, 0x00000000u}, FloatCase{0xC2AEAC4Fu, 0x00800026u},
    FloatCase{0xC2AEAC50u, 0x007FFFE6u}, FloatCase{0xC2C80000u, 0x0000001Bu}, FloatCase{0x00000001u, 0x3F800000u},
    FloatCase{0x33000000u, 0x3F800000u}, FloatCase{0xB3000000u, 0x3F800000u}, FloatCase{0x42AEAC4Fu, 0x7E7FFFB4u},
    FloatCase{0x42AEAC50u, 0x7E80001Au},
};

// log of float: as for double, the five inputs whose log(double) is a float
// midpoint, which the list of Log.h answers, and each side of four edges of the
// tables of Log.h: the start and the end of the range of z, and the two ends
// of the interval that holds 1.
constexpr ::fixy::FixedArray<FloatCase, 25> kLogFloatCases{
    FloatCase{0x00000000u, 0xFF800000u}, FloatCase{0x80000000u, 0xFF800000u}, FloatCase{0x7F800000u, 0x7F800000u},
    FloatCase{0xFF800000u, kNan32},      FloatCase{0xBF800000u, kNan32},      FloatCase{0x80000001u, kNan32},
    FloatCase{0x7FC00001u, kNan32},      FloatCase{0x3F800000u, 0x00000000u}, FloatCase{0x00000001u, 0xC2CE8ED0u},
    FloatCase{0x007FFFFFu, 0xC2AEAC50u}, FloatCase{0x00800000u, 0xC2AEAC50u}, FloatCase{0x7F7FFFFFu, 0x42B17218u},
    FloatCase{0x41178FEBu, 0x400FE5E7u}, FloatCase{0x4C5D65A5u, 0x418F034Bu}, FloatCase{0x3C413D3Au, 0xC08E158Fu},
    FloatCase{0x65D890D3u, 0x4254D1F9u}, FloatCase{0x6F31A8ECu, 0x42845A89u}, FloatCase{0x3F337FFFu, 0xBEB5C2DDu},
    FloatCase{0x3F338000u, 0xBEB5C2DAu}, FloatCase{0x3FB37FFFu, 0x3EAD2153u}, FloatCase{0x3FB38000u, 0x3EAD2156u},
    FloatCase{0x3F7F7FFFu, 0xBB00210Bu}, FloatCase{0x3F7F8000u, 0xBB00200Bu}, FloatCase{0x3F807FFFu, 0x3B7F7E57u},
    FloatCase{0x3F808000u, 0x3B7F8055u},
};

// erf of float: as for double, with subnormal results, the results near 2^-123,
// the edge to ±1 of the float form, and three small inputs whose round needs
// Q0 at the double nearest 2/sqrt(pi).  erf(double) of 0x3940EAD6 is a float
// midpoint.
constexpr ::fixy::FixedArray<FloatCase, 18> kErfFloatCases{
    FloatCase{0x00000000u, 0x00000000u}, FloatCase{0x80000000u, 0x80000000u}, FloatCase{0x7F800000u, 0x3F800000u},
    FloatCase{0xFF800000u, 0xBF800000u}, FloatCase{0x7FC00001u, kNan32},      FloatCase{0x00000001u, 0x00000001u},
    FloatCase{0x80000001u, 0x80000001u}, FloatCase{0x007FFFFFu, 0x00906EB9u}, FloatCase{0x01FFFFFFu, 0x02106EBAu},
    FloatCase{0x02000000u, 0x02106EBBu}, FloatCase{0x3F000000u, 0x3F053F7Bu}, FloatCase{0x3F800000u, 0x3F57BB3Du},
    FloatCase{0x407AD445u, 0x3F800000u}, FloatCase{0x407AD444u, 0x3F7FFFFFu}, FloatCase{0xC07AD445u, 0xBF800000u},
    FloatCase{0x36AEA466u, 0x36C51007u}, FloatCase{0x3940EAD6u, 0x3959AF14u}, FloatCase{0x39C0057Du, 0x39D8AC49u},
};

void test_the_special_values() {
    check_double_cases("exp", kExpDoubleCases, [](double value) noexcept { return ::fixy::fp::exp(value); });
    check_double_cases("log", kLogDoubleCases, [](double value) noexcept { return ::fixy::fp::log(value); });
    check_double_cases("erf", kErfDoubleCases, [](double value) noexcept { return ::fixy::fp::erf(value); });
    check_float_cases("exp", kExpFloatCases, [](float value) noexcept { return ::fixy::fp::exp(value); });
    check_float_cases("log", kLogFloatCases, [](float value) noexcept { return ::fixy::fp::log(value); });
    check_float_cases("erf", kErfFloatCases, [](float value) noexcept { return ::fixy::fp::erf(value); });
    ::fixy::check::pass(kTest, "test_fp_elementary: the special values and the edges hold\n");
}

// erf is odd for each input of the sample: erf(-x) has the bits of erf(x)
// with the sign bit flipped.
void test_erf_is_odd() {
    for (std::uint32_t index = 0; index < fe::kSampleCount; ++index) {
        std::uint64_t const pattern = ((std::uint64_t{index} + 1) * 0x9E3779B97F4A7C15ULL) & ~(std::uint64_t{1} << 63);
        double const value = opaque_double(pattern);
        if (::fixy::fp::is_nan(value)) continue;
        auto const positive = ::fixy::bit_cast<std::uint64_t>(::fixy::fp::erf(value));
        auto const negative = ::fixy::bit_cast<std::uint64_t>(::fixy::fp::erf(-value));
        CRUCIBLE_TEST_ASSERT(kTest, (positive ^ negative) == (std::uint64_t{1} << 63));
        float const narrow = static_cast<float>(value);
        auto const positive_float = ::fixy::bit_cast<std::uint32_t>(::fixy::fp::erf(narrow));
        auto const negative_float = ::fixy::bit_cast<std::uint32_t>(::fixy::fp::erf(-narrow));
        CRUCIBLE_TEST_ASSERT(kTest, (positive_float ^ negative_float) == 0x80000000u);
    }
    ::fixy::check::pass(kTest, "test_fp_elementary: erf is odd on the sample\n");
}

[[nodiscard]] bool agree_with_the_literal(::fixy::TextView unit, fe::FpDigests digests) noexcept {
    bool const is_same = digests.exp_double == kDigests.exp_double && digests.exp_float == kDigests.exp_float
                      && digests.log_double == kDigests.log_double && digests.log_float == kDigests.log_float
                      && digests.erf_double == kDigests.erf_double && digests.erf_float == kDigests.erf_float;
    if (!is_same) {
        ::fixy::report(::fixy::Sink::Err,
                       "test_fp_elementary: the unit {} gives the digests 0x{}, 0x{}, 0x{}, 0x{}, 0x{} and 0x{}\n",
                       unit, ::fixy::Hex<16>{digests.exp_double}, ::fixy::Hex<16>{digests.exp_float},
                       ::fixy::Hex<16>{digests.log_double}, ::fixy::Hex<16>{digests.log_float},
                       ::fixy::Hex<16>{digests.erf_double}, ::fixy::Hex<16>{digests.erf_float});
    }
    return is_same;
}

void test_each_level_gives_the_same_bits() {
    CRUCIBLE_TEST_ASSERT(kTest,
                         agree_with_the_literal("of the build", fe::fp_digests_of_this_unit<fe::FpUnit::Build>()));
    std::size_t units = 1;
#if defined(__x86_64__)
    namespace os = ::foundation::os;
    // The build compiles the unit of each level other than the level of the
    // build (test/fixy/CMakeLists.txt).
    os::X86Level const host = os::x86_level_of(os::read_x86_feature_words());
    if constexpr (CRUCIBLE_TEST_HAS_V1_UNIT != 0) {
        CRUCIBLE_TEST_ASSERT(kTest, agree_with_the_literal("v1", fe::fp_digests_of_v1_unit()));
        ++units;
    }
    if constexpr (CRUCIBLE_TEST_HAS_V2_UNIT != 0) {
        if (host >= os::X86Level::v2) {
            CRUCIBLE_TEST_ASSERT(kTest, agree_with_the_literal("v2", fe::fp_digests_of_v2_unit()));
            ++units;
        }
    }
    if constexpr (CRUCIBLE_TEST_HAS_V3_UNIT != 0) {
        if (host >= os::X86Level::v3) {
            CRUCIBLE_TEST_ASSERT(kTest, agree_with_the_literal("v3", fe::fp_digests_of_v3_unit()));
            ++units;
        }
    }
    if constexpr (CRUCIBLE_TEST_HAS_V4_UNIT != 0) {
        if (host >= os::X86Level::v4) {
            CRUCIBLE_TEST_ASSERT(kTest, agree_with_the_literal("v4", fe::fp_digests_of_v4_unit()));
            ++units;
        }
    }
#endif
    ::fixy::check::pass(kTest, "test_fp_elementary: the digests hold in {} units\n", units);
}

}  // namespace

int main() {
    test_the_special_values();
    test_erf_is_odd();
    test_each_level_gives_the_same_bits();
    return 0;
}
