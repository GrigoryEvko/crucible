// The x86-64 level door of foundation/os/X86Level.h.
//
// The oracle of the bits is GCC: the bit constants of <cpuid.h> for the
// words of CPUID, and __builtin_cpu_supports for the features and the levels
// of this host.  The bits of XCR0 are the state components of the Intel SDM,
// volume 1, section 13.1, because <cpuid.h> names none.  The levels of each
// feature are the levels of the psABI of x86-64.
//
// The checks:
//   - The words of one feature hold that feature and no other.
//   - Words with each feature but one give the level below the level of
//     that feature, and the feature is the first missing feature.
//   - The level of this host and each feature of it agree with GCC.
//   - The parse takes the four texts v1 to v4 and no other text.
//   - The pick gives the level of the host, or a requested level at or below
//     it, or the first missing feature of a requested level above it.
//   - A request above the host, and a text that is no level, end the
//     process with a text that names the feature or the text.
//   - The level of the flags of this unit is at or below the level of the
//     host, because the host runs this unit.

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/Platform.h>
#include <foundation/Quarantine.h>
#include <foundation/core/Choice.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/X86Level.h>

#include <cstdint>

#if defined(__x86_64__)

namespace {

namespace os = ::foundation::os;
using os::X86Feature;
using os::X86FeatureWords;
using os::X86Level;

constexpr std::uint8_t kFirstFeature = static_cast<std::uint8_t>(X86Feature::cx16);
constexpr std::uint8_t kLastFeature = static_cast<std::uint8_t>(X86Feature::hi16_zmm_state);

// The bits of XCR0 (Intel SDM, volume 1, section 13.1).
constexpr std::uint64_t kXcr0Sse = std::uint64_t{1} << 1;
constexpr std::uint64_t kXcr0Avx = std::uint64_t{1} << 2;
constexpr std::uint64_t kXcr0Opmask = std::uint64_t{1} << 5;
constexpr std::uint64_t kXcr0ZmmHi256 = std::uint64_t{1} << 6;
constexpr std::uint64_t kXcr0Hi16Zmm = std::uint64_t{1} << 7;

// The level of each feature in the psABI of x86-64.
[[nodiscard]] constexpr X86Level level_of_feature(X86Feature feature) noexcept {
    switch (feature) {
        case X86Feature::cx16:
        case X86Feature::lahf_sahf:
        case X86Feature::popcnt:
        case X86Feature::sse3:
        case X86Feature::sse4_1:
        case X86Feature::sse4_2:
        case X86Feature::ssse3:
            return X86Level::v2;
        case X86Feature::avx512f:
        case X86Feature::avx512bw:
        case X86Feature::avx512cd:
        case X86Feature::avx512dq:
        case X86Feature::avx512vl:
        case X86Feature::opmask_state:
        case X86Feature::zmm_hi256_state:
        case X86Feature::hi16_zmm_state:
            return X86Level::v4;
        default:
            return X86Level::v3;
    }
}

[[nodiscard]] constexpr X86Feature feature_at(std::uint8_t number) noexcept { return static_cast<X86Feature>(number); }

// Each bit of each word, apart from the bits that were removed.
[[nodiscard]] constexpr X86FeatureWords words_without(X86FeatureWords const& removed) noexcept {
    return X86FeatureWords{.xcr0 = ~removed.xcr0,
                           .leaf1_ecx = ~removed.leaf1_ecx,
                           .leaf7_ebx = ~removed.leaf7_ebx,
                           .extended1_ecx = ~removed.extended1_ecx};
}

[[nodiscard]] constexpr X86FeatureWords words_or(X86FeatureWords const& left, X86FeatureWords const& right) noexcept {
    return X86FeatureWords{.xcr0 = left.xcr0 | right.xcr0,
                           .leaf1_ecx = left.leaf1_ecx | right.leaf1_ecx,
                           .leaf7_ebx = left.leaf7_ebx | right.leaf7_ebx,
                           .extended1_ecx = left.extended1_ecx | right.extended1_ecx};
}

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ORACLE: GCC <cpuid.h> and __builtin_cpu_supports give the x86-64 features")
#include <cpuid.h>

namespace {

// The words that hold only the bit of FEATURE, from the constants of GCC.
[[nodiscard]] constexpr X86FeatureWords oracle_words_of(X86Feature feature) noexcept {
    switch (feature) {
        case X86Feature::cx16:
            return X86FeatureWords{.leaf1_ecx = bit_CMPXCHG16B};
        case X86Feature::lahf_sahf:
            return X86FeatureWords{.extended1_ecx = bit_LAHF_LM};
        case X86Feature::popcnt:
            return X86FeatureWords{.leaf1_ecx = bit_POPCNT};
        case X86Feature::sse3:
            return X86FeatureWords{.leaf1_ecx = bit_SSE3};
        case X86Feature::sse4_1:
            return X86FeatureWords{.leaf1_ecx = bit_SSE4_1};
        case X86Feature::sse4_2:
            return X86FeatureWords{.leaf1_ecx = bit_SSE4_2};
        case X86Feature::ssse3:
            return X86FeatureWords{.leaf1_ecx = bit_SSSE3};
        case X86Feature::avx:
            return X86FeatureWords{.leaf1_ecx = bit_AVX};
        case X86Feature::avx2:
            return X86FeatureWords{.leaf7_ebx = bit_AVX2};
        case X86Feature::bmi1:
            return X86FeatureWords{.leaf7_ebx = bit_BMI};
        case X86Feature::bmi2:
            return X86FeatureWords{.leaf7_ebx = bit_BMI2};
        case X86Feature::f16c:
            return X86FeatureWords{.leaf1_ecx = bit_F16C};
        case X86Feature::fma:
            return X86FeatureWords{.leaf1_ecx = bit_FMA};
        case X86Feature::lzcnt:
            // ABM of the extended leaf holds LZCNT.
            return X86FeatureWords{.extended1_ecx = bit_ABM};
        case X86Feature::movbe:
            return X86FeatureWords{.leaf1_ecx = bit_MOVBE};
        case X86Feature::xsave:
            return X86FeatureWords{.leaf1_ecx = bit_XSAVE};
        case X86Feature::osxsave:
            return X86FeatureWords{.leaf1_ecx = bit_OSXSAVE};
        case X86Feature::xmm_state:
            return X86FeatureWords{.xcr0 = kXcr0Sse};
        case X86Feature::ymm_state:
            return X86FeatureWords{.xcr0 = kXcr0Avx};
        case X86Feature::avx512f:
            return X86FeatureWords{.leaf7_ebx = bit_AVX512F};
        case X86Feature::avx512bw:
            return X86FeatureWords{.leaf7_ebx = bit_AVX512BW};
        case X86Feature::avx512cd:
            return X86FeatureWords{.leaf7_ebx = bit_AVX512CD};
        case X86Feature::avx512dq:
            return X86FeatureWords{.leaf7_ebx = bit_AVX512DQ};
        case X86Feature::avx512vl:
            return X86FeatureWords{.leaf7_ebx = bit_AVX512VL};
        case X86Feature::opmask_state:
            return X86FeatureWords{.xcr0 = kXcr0Opmask};
        case X86Feature::zmm_hi256_state:
            return X86FeatureWords{.xcr0 = kXcr0ZmmHi256};
        case X86Feature::hi16_zmm_state:
            return X86FeatureWords{.xcr0 = kXcr0Hi16Zmm};
        default:
            return X86FeatureWords{};
    }
}

// True when GCC finds FEATURE on this host.  GCC names no state of XCR0, so
// a state gives the answer of the level that needs it.  GCC checks XCR0
// before it sets avx and the features of v4.
[[nodiscard]] bool oracle_host_has(X86Feature feature) noexcept {
    switch (feature) {
        case X86Feature::cx16:
            return __builtin_cpu_supports("cmpxchg16b");
        case X86Feature::lahf_sahf:
            return __builtin_cpu_supports("lahf_lm");
        case X86Feature::popcnt:
            return __builtin_cpu_supports("popcnt");
        case X86Feature::sse3:
            return __builtin_cpu_supports("sse3");
        case X86Feature::sse4_1:
            return __builtin_cpu_supports("sse4.1");
        case X86Feature::sse4_2:
            return __builtin_cpu_supports("sse4.2");
        case X86Feature::ssse3:
            return __builtin_cpu_supports("ssse3");
        case X86Feature::avx:
            return __builtin_cpu_supports("avx");
        case X86Feature::avx2:
            return __builtin_cpu_supports("avx2");
        case X86Feature::bmi1:
            return __builtin_cpu_supports("bmi");
        case X86Feature::bmi2:
            return __builtin_cpu_supports("bmi2");
        case X86Feature::f16c:
            return __builtin_cpu_supports("f16c");
        case X86Feature::fma:
            return __builtin_cpu_supports("fma");
        case X86Feature::lzcnt:
            return __builtin_cpu_supports("lzcnt");
        case X86Feature::movbe:
            return __builtin_cpu_supports("movbe");
        case X86Feature::xsave:
            return __builtin_cpu_supports("xsave");
        case X86Feature::osxsave:
            return __builtin_cpu_supports("osxsave");
        case X86Feature::xmm_state:
        case X86Feature::ymm_state:
            return __builtin_cpu_supports("x86-64-v3");
        case X86Feature::avx512f:
            return __builtin_cpu_supports("avx512f");
        case X86Feature::avx512bw:
            return __builtin_cpu_supports("avx512bw");
        case X86Feature::avx512cd:
            return __builtin_cpu_supports("avx512cd");
        case X86Feature::avx512dq:
            return __builtin_cpu_supports("avx512dq");
        case X86Feature::avx512vl:
            return __builtin_cpu_supports("avx512vl");
        case X86Feature::opmask_state:
        case X86Feature::zmm_hi256_state:
        case X86Feature::hi16_zmm_state:
            return __builtin_cpu_supports("x86-64-v4");
        default:
            return false;
    }
}

// The level of this host in the probes of GCC.
[[nodiscard]] X86Level oracle_host_level() noexcept {
    if (__builtin_cpu_supports("x86-64-v4")) return X86Level::v4;
    if (__builtin_cpu_supports("x86-64-v3")) return X86Level::v3;
    if (__builtin_cpu_supports("x86-64-v2")) return X86Level::v2;
    return X86Level::v1;
}

}  // namespace

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

// The words of each feature of the levels up to LEVEL.
[[nodiscard]] constexpr X86FeatureWords oracle_words_of_level(X86Level level) noexcept {
    X86FeatureWords words{};
    for (std::uint8_t number = kFirstFeature; number <= kLastFeature; ++number) {
        if (level_of_feature(feature_at(number)) <= level) words = words_or(words, oracle_words_of(feature_at(number)));
    }
    return words;
}

// The words of one feature hold that feature and no other.  A row of the door
// with a wrong bit fails here.
template <typename Ctx>
void test_each_feature_has_its_bit(Ctx const& test) {
    for (std::uint8_t number = kFirstFeature; number <= kLastFeature; ++number) {
        X86FeatureWords const words = oracle_words_of(feature_at(number));
        for (std::uint8_t other = kFirstFeature; other <= kLastFeature; ++other) {
            CRUCIBLE_TEST_ASSERT(test, os::has_x86_feature(words, feature_at(other)) == (other == number));
        }
        CRUCIBLE_TEST_ASSERT(test, !os::has_x86_feature(words, X86Feature::none));
    }
}

// Words with each feature but one give the level below the level of that
// feature, and the feature is the first missing feature of its level.  The
// state of a register file counts as a feature: a core whose OS does not save
// the YMM registers has v2.
template <typename Ctx>
void test_a_missing_feature_lowers_the_level(Ctx const& test) {
    X86FeatureWords const each = oracle_words_of_level(X86Level::v4);
    CRUCIBLE_TEST_ASSERT(test, os::x86_level_of(each) == X86Level::v4);
    CRUCIBLE_TEST_ASSERT(test, os::x86_level_of(words_without(X86FeatureWords{})) == X86Level::v4);
    CRUCIBLE_TEST_ASSERT(test, os::x86_level_of(X86FeatureWords{}) == X86Level::v1);
    for (std::uint8_t number = kFirstFeature; number <= kLastFeature; ++number) {
        X86Feature const feature = feature_at(number);
        X86Level const level = level_of_feature(feature);
        X86Level const below = static_cast<X86Level>(static_cast<std::uint8_t>(level) - 1);
        X86FeatureWords const removed = oracle_words_of(feature);
        X86FeatureWords const lacking = X86FeatureWords{.xcr0 = each.xcr0 & ~removed.xcr0,
                                                        .leaf1_ecx = each.leaf1_ecx & ~removed.leaf1_ecx,
                                                        .leaf7_ebx = each.leaf7_ebx & ~removed.leaf7_ebx,
                                                        .extended1_ecx = each.extended1_ecx & ~removed.extended1_ecx};
        CRUCIBLE_TEST_ASSERT(test, os::x86_level_of(lacking) == below);
        CRUCIBLE_TEST_ASSERT(test, os::first_missing_feature(lacking, level) == feature);
        CRUCIBLE_TEST_ASSERT(test, os::first_missing_feature(lacking, below) == X86Feature::none);
        CRUCIBLE_TEST_ASSERT(test, os::first_missing_feature(lacking, X86Level::v4) == feature);
    }
    // The words of exactly one level give that level.
    CRUCIBLE_TEST_ASSERT(test, os::x86_level_of(oracle_words_of_level(X86Level::v2)) == X86Level::v2);
    CRUCIBLE_TEST_ASSERT(test, os::x86_level_of(oracle_words_of_level(X86Level::v3)) == X86Level::v3);
    CRUCIBLE_TEST_ASSERT(test, os::first_missing_feature(X86FeatureWords{}, X86Level::v1) == X86Feature::none);
}

// The level of this host and each feature of it agree with GCC.
template <typename Ctx>
void test_the_host_agrees_with_gcc(Ctx const& test) {
    X86FeatureWords const host = os::read_x86_feature_words();
    CRUCIBLE_TEST_ASSERT(test, os::x86_level_of(host) == oracle_host_level());
    for (std::uint8_t number = kFirstFeature; number <= kLastFeature; ++number) {
        X86Feature const feature = feature_at(number);
        bool const is_state = oracle_words_of(feature).xcr0 != 0;
        if (is_state) {
            // GCC gives only the level of a state, and that level holds the
            // state.
            if (oracle_host_has(feature)) CRUCIBLE_TEST_ASSERT(test, os::has_x86_feature(host, feature));
            continue;
        }
        CRUCIBLE_TEST_ASSERT(test, os::has_x86_feature(host, feature) == oracle_host_has(feature));
    }
}

// The parse takes the four texts v1 to v4 and no other text.
template <typename Ctx>
void test_the_parse(Ctx const& test) {
    CRUCIBLE_TEST_ASSERT(test, os::parse_x86_level("v1").value_or(X86Level::v4) == X86Level::v1);
    CRUCIBLE_TEST_ASSERT(test, os::parse_x86_level("v2").value_or(X86Level::v1) == X86Level::v2);
    CRUCIBLE_TEST_ASSERT(test, os::parse_x86_level("v3").value_or(X86Level::v1) == X86Level::v3);
    CRUCIBLE_TEST_ASSERT(test, os::parse_x86_level("v4").value_or(X86Level::v1) == X86Level::v4);
    CRUCIBLE_TEST_ASSERT(test, os::parse_x86_level("").is_none());
    CRUCIBLE_TEST_ASSERT(test, os::parse_x86_level("v0").is_none());
    CRUCIBLE_TEST_ASSERT(test, os::parse_x86_level("v5").is_none());
    CRUCIBLE_TEST_ASSERT(test, os::parse_x86_level("V3").is_none());
    CRUCIBLE_TEST_ASSERT(test, os::parse_x86_level("v3 ").is_none());
    CRUCIBLE_TEST_ASSERT(test, os::parse_x86_level(" v3").is_none());
    CRUCIBLE_TEST_ASSERT(test, os::parse_x86_level("v33").is_none());
    CRUCIBLE_TEST_ASSERT(test, os::parse_x86_level("x86-64-v3").is_none());
}

// The pick gives the level of the host with no request, a requested level at
// or below the host, and the first missing feature of a level above it.
template <typename Ctx>
void test_the_pick(Ctx const& test) {
    namespace core = ::foundation::core;
    X86FeatureWords const v2_host = oracle_words_of_level(X86Level::v2);
    CRUCIBLE_TEST_ASSERT(test, os::pick_x86_level(v2_host, core::none).value_or(X86Level::v4) == X86Level::v2);
    CRUCIBLE_TEST_ASSERT(test,
                         os::pick_x86_level(v2_host, core::some(X86Level::v1)).value_or(X86Level::v4) == X86Level::v1);
    CRUCIBLE_TEST_ASSERT(test,
                         os::pick_x86_level(v2_host, core::some(X86Level::v2)).value_or(X86Level::v4) == X86Level::v2);
    CRUCIBLE_TEST_ASSERT(test, os::pick_x86_level(v2_host, core::some(X86Level::v3)).err().value_or(X86Feature::none)
                                   == X86Feature::avx);
    CRUCIBLE_TEST_ASSERT(test, os::pick_x86_level(v2_host, core::some(X86Level::v4)).err().value_or(X86Feature::none)
                                   == X86Feature::avx);
    X86FeatureWords const v3_host = oracle_words_of_level(X86Level::v3);
    CRUCIBLE_TEST_ASSERT(test, os::pick_x86_level(v3_host, core::some(X86Level::v4)).err().value_or(X86Feature::none)
                                   == X86Feature::avx512f);
    X86FeatureWords const v4_host = oracle_words_of_level(X86Level::v4);
    CRUCIBLE_TEST_ASSERT(test, os::pick_x86_level(v4_host, core::none).value_or(X86Level::v1) == X86Level::v4);

    CRUCIBLE_TEST_ASSERT(test, os::require_x86_level(v4_host, "") == X86Level::v4);
    CRUCIBLE_TEST_ASSERT(test, os::require_x86_level(v4_host, "v2") == X86Level::v2);
    CRUCIBLE_TEST_ASSERT(test, os::require_x86_level(v2_host, "") == X86Level::v2);
    CRUCIBLE_TEST_ASSERT(test, os::require_x86_level(v2_host, "v1") == X86Level::v1);
}

// A request above the host, and a text that is no level, end the process.
// The text names the level and the missing feature, or the text.
template <typename Ctx>
void test_a_refused_request_ends_the_process(Ctx const& test) {
    X86FeatureWords const v2_host = oracle_words_of_level(X86Level::v2);
    X86FeatureWords const v4_host = oracle_words_of_level(X86Level::v4);
    ::fixy::check::expect_dies(
        test, [&v2_host]() noexcept { static_cast<void>(os::require_x86_level(v2_host, "v3")); },
        "the request names the x86-64 level v3, and the host has no avx");
    ::fixy::check::expect_dies(
        test, [&v4_host]() noexcept { static_cast<void>(os::require_x86_level(v4_host, "v5")); },
        "the request of an x86-64 level is 'v5'");
    ::fixy::check::expect_dies(
        test, [&v4_host]() noexcept { static_cast<void>(os::require_x86_level(v4_host, "x86-64-v3")); },
        "the request of an x86-64 level is 'x86-64-v3'");
}

// The host runs this unit, so the level of its flags is at or below the level
// of the host.
template <typename Ctx>
void test_the_target_is_at_or_below_the_host(Ctx const& test) {
    CRUCIBLE_TEST_ASSERT(test, os::x86_level_of_target() <= os::x86_level_of(os::read_x86_feature_words()));
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_each_feature_has_its_bit(test);
    test_a_missing_feature_lowers_the_level(test);
    test_the_host_agrees_with_gcc(test);
    test_the_parse(test);
    test_the_pick(test);
    test_a_refused_request_ends_the_process(test);
    test_the_target_is_at_or_below_the_host(test);
    ::fixy::check::pass(test, "test_os_x86_level: each check passed\n");
    return 0;
}

#else

int main() { return 0; }

#endif
