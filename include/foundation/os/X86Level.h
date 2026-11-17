#pragma once

// The x86-64 level door of the base: the level of the instruction set that
// the host runs, the level that the flags of a unit give, and the pick of a
// level for the engine.
//
// THE X86 LEVELS
//   The psABI of x86-64 names four levels of the instruction set.  Each level
//   holds each feature of the level below it:
//     v1  SSE and SSE2: -march=x86-64, the floor of each x86_64 core
//     v2  v1, CMPXCHG16B, LAHF and SAHF, POPCNT, SSE3, SSSE3, SSE4.1, SSE4.2
//     v3  v2, AVX, AVX2, BMI1, BMI2, F16C, FMA, LZCNT, MOVBE, XSAVE
//     v4  v3, AVX512F, AVX512BW, AVX512CD, AVX512DQ, AVX512VL
//   GCC gives v1 with -march=x86-64, and it refuses the word x86-64-v1.
//
//   CRUCIBLE_X86_LEVEL_OF_TARGET is the number of the highest level whose
//   features the flags of the unit give, 1 to 4.  The step of each level on
//   the ladder of THE TARGET NAMESPACE of foundation/Platform.h is
//   target_sse2, target_sse42, target_avx2 and target_avx512bw.  A target can
//   give more features than its level, as -march=native does, and the number
//   then names only the level.  The macro is in this header and not in
//   Platform.h, so a unit that does not read the level does not compile again
//   when the macro changes.
//
// The engine builds one time for each level, and the loader of the vessel
// picks the highest level that the host runs.  A level needs two facts of the
// host:
//   - CPUID sets the bit of each feature of the level and of each level below
//     it.
//   - XCR0 holds the state of each register file of the level.  The OS sets a
//     bit of XCR0 only when it saves that state at a switch of the context.
//     So a core with AVX under an OS that does not save the YMM registers
//     runs no AVX code, and its level is v2.
// GCC reads the same facts for __builtin_cpu_supports("x86-64-v3").  Each
// core of x86_64 has the features of v1, so v1 needs no check.
//
// THE PARTS
//   X86Level                   v1, v2, v3 or v4
//   X86Feature                 a feature of a level, which a refusal names
//   X86FeatureWords            the words of CPUID and of XCR0 that the check
//                              reads
//   read_x86_feature_words()   the words of the calling core
//   has_x86_feature(words, feature)
//   first_missing_feature(words, level)
//                              the first feature of the level, or of a level
//                              below it, that the words do not hold, or none
//   x86_level_of(words)        the highest level whose features the words
//                              hold
//   x86_level_of_target()      the level that the flags of the unit give
//   parse_x86_level(text)      the level of the text v1, v2, v3 or v4
//   pick_x86_level(host, request)
//                              the level of the host with no request, the
//                              requested level at or below it, or the first
//                              feature that the host does not have
//   require_x86_level(host, text)
//                              the pick from the text of a request.  An empty
//                              text is no request.  A text that is no level,
//                              and a level above the host, end the process
//                              with fatal, which names the text or the
//                              missing feature.
//
// Each function apart from the reader is a pure function of its words, so a
// test gives it the words of each kind of host.  The reader and the pick read
// no memory of the process and write none, so a loader can call them before
// it loads a library of a level.  On aarch64 the header declares nothing.
//
// This header is in the layer F0_doors of utils/rule-table.txt.

#include <foundation/core/Choice.h>
#include <foundation/core/Report.h>
#include <foundation/core/Text.h>
#include <foundation/os/Cpuid.h>

#include <cstdint>

#if defined(__x86_64__)
#if !(defined(__SSE3__) && defined(__SSSE3__) && defined(__SSE4_1__) && defined(__SSE4_2__) && defined(__POPCNT__) \
      && defined(__LAHF_SAHF__) && defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16))
#define CRUCIBLE_X86_LEVEL_OF_TARGET 1
#elif !(defined(__AVX__) && defined(__AVX2__) && defined(__BMI__) && defined(__BMI2__) && defined(__F16C__) \
        && defined(__FMA__) && defined(__LZCNT__) && defined(__MOVBE__) && defined(__XSAVE__))
#define CRUCIBLE_X86_LEVEL_OF_TARGET 2
#elif !(defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512CD__) && defined(__AVX512DQ__) \
        && defined(__AVX512VL__))
#define CRUCIBLE_X86_LEVEL_OF_TARGET 3
#else
#define CRUCIBLE_X86_LEVEL_OF_TARGET 4
#endif
#endif

namespace foundation::os {

#if defined(__x86_64__)

// The four levels of x86-64.  Each level holds each feature of the levels
// below it, so the order of the values is the order of the levels.
enum class X86Level : std::uint8_t {
    v1 = 1,
    v2 = 2,
    v3 = 3,
    v4 = 4
};

// A feature of a level of x86-64.  The names of the states are the bits of
// XCR0: xmm_state is SSE, ymm_state is AVX, and the three states of v4 are
// the opmask registers, the upper halves of ZMM0 to ZMM15, and ZMM16 to ZMM31.
enum class X86Feature : std::uint8_t {
    none = 0,
    cx16,
    lahf_sahf,
    popcnt,
    sse3,
    sse4_1,
    sse4_2,
    ssse3,
    avx,
    avx2,
    bmi1,
    bmi2,
    f16c,
    fma,
    lzcnt,
    movbe,
    xsave,
    osxsave,
    xmm_state,
    ymm_state,
    avx512f,
    avx512bw,
    avx512cd,
    avx512dq,
    avx512vl,
    opmask_state,
    zmm_hi256_state,
    hi16_zmm_state,
};

// The words that hold the bit of each feature.  A word of a leaf that the
// core does not have is zero, and XCR0 is zero when CPUID does not set
// OSXSAVE.
struct X86FeatureWords {
    // The word XCR0 of XGETBV.
    std::uint64_t xcr0 = 0;
    // The word ECX of leaf 1.
    std::uint32_t leaf1_ecx = 0;
    // The word EBX of leaf 7, subleaf 0.
    std::uint32_t leaf7_ebx = 0;
    // The word ECX of leaf 0x80000001.
    std::uint32_t extended1_ecx = 0;
    std::uint32_t pad_after_words = 0;
};

namespace detail {

// The word of X86FeatureWords that holds the bit of a feature.
enum class X86Word : std::uint8_t {
    leaf1_ecx,
    leaf7_ebx,
    extended1_ecx,
    xcr0
};

// The place of the bit of one feature, and its level.
struct X86FeatureBit {
    X86Feature feature = X86Feature::none;
    X86Level level = X86Level::v1;
    X86Word word = X86Word::leaf1_ecx;
    std::uint8_t bit = 0;
};

// The bits of each feature of v2, v3 and v4, from the Intel SDM, volume 2A
// (CPUID and XGETBV), and the AMD APM, volume 3.  The rows of one level
// follow the rows of the level below it.  first_missing_feature() reads them
// in this order.
inline constexpr X86FeatureBit kX86FeatureBits[] = {
    {X86Feature::cx16, X86Level::v2, X86Word::leaf1_ecx, 13},
    {X86Feature::lahf_sahf, X86Level::v2, X86Word::extended1_ecx, 0},
    {X86Feature::popcnt, X86Level::v2, X86Word::leaf1_ecx, 23},
    {X86Feature::sse3, X86Level::v2, X86Word::leaf1_ecx, 0},
    {X86Feature::sse4_1, X86Level::v2, X86Word::leaf1_ecx, 19},
    {X86Feature::sse4_2, X86Level::v2, X86Word::leaf1_ecx, 20},
    {X86Feature::ssse3, X86Level::v2, X86Word::leaf1_ecx, 9},
    {X86Feature::avx, X86Level::v3, X86Word::leaf1_ecx, 28},
    {X86Feature::avx2, X86Level::v3, X86Word::leaf7_ebx, 5},
    {X86Feature::bmi1, X86Level::v3, X86Word::leaf7_ebx, 3},
    {X86Feature::bmi2, X86Level::v3, X86Word::leaf7_ebx, 8},
    {X86Feature::f16c, X86Level::v3, X86Word::leaf1_ecx, 29},
    {X86Feature::fma, X86Level::v3, X86Word::leaf1_ecx, 12},
    {X86Feature::lzcnt, X86Level::v3, X86Word::extended1_ecx, 5},
    {X86Feature::movbe, X86Level::v3, X86Word::leaf1_ecx, 22},
    {X86Feature::xsave, X86Level::v3, X86Word::leaf1_ecx, 26},
    {X86Feature::osxsave, X86Level::v3, X86Word::leaf1_ecx, 27},
    {X86Feature::xmm_state, X86Level::v3, X86Word::xcr0, 1},
    {X86Feature::ymm_state, X86Level::v3, X86Word::xcr0, 2},
    {X86Feature::avx512f, X86Level::v4, X86Word::leaf7_ebx, 16},
    {X86Feature::avx512bw, X86Level::v4, X86Word::leaf7_ebx, 30},
    {X86Feature::avx512cd, X86Level::v4, X86Word::leaf7_ebx, 28},
    {X86Feature::avx512dq, X86Level::v4, X86Word::leaf7_ebx, 17},
    {X86Feature::avx512vl, X86Level::v4, X86Word::leaf7_ebx, 31},
    {X86Feature::opmask_state, X86Level::v4, X86Word::xcr0, 5},
    {X86Feature::zmm_hi256_state, X86Level::v4, X86Word::xcr0, 6},
    {X86Feature::hi16_zmm_state, X86Level::v4, X86Word::xcr0, 7},
};

// The word that holds the bit of a row.
[[nodiscard]] constexpr std::uint64_t word_of(X86FeatureWords const& words, X86Word word) noexcept {
    switch (word) {
        case X86Word::leaf1_ecx:
            return words.leaf1_ecx;
        case X86Word::leaf7_ebx:
            return words.leaf7_ebx;
        case X86Word::extended1_ecx:
            return words.extended1_ecx;
        case X86Word::xcr0:
            return words.xcr0;
        default:
            core::unreachable();
    }
}

[[nodiscard]] constexpr bool holds_bit(X86FeatureWords const& words, X86FeatureBit const& row) noexcept {
    return ((word_of(words, row.word) >> row.bit) & 1U) != 0;
}

}  // namespace detail

// True when the words hold the bit of the feature.  X86Feature::none is no
// feature of a level, so the words never hold it.  Complexity: one pass of
// the 27 rows.
[[nodiscard]] constexpr bool has_x86_feature(X86FeatureWords const& words, X86Feature feature) noexcept {
    for (detail::X86FeatureBit const& row : detail::kX86FeatureBits) {
        if (row.feature == feature) return detail::holds_bit(words, row);
    }
    return false;
}

// The first feature of the level, or of a level below it, that the words do
// not hold, in the order of the rows.  X86Feature::none when the words hold
// each one.  Complexity: one pass of the 27 rows.
[[nodiscard]] constexpr X86Feature first_missing_feature(X86FeatureWords const& words, X86Level level) noexcept {
    for (detail::X86FeatureBit const& row : detail::kX86FeatureBits) {
        if (row.level <= level && !detail::holds_bit(words, row)) return row.feature;
    }
    return X86Feature::none;
}

// The highest level whose features the words hold.  Each x86_64 core has v1.
[[nodiscard]] constexpr X86Level x86_level_of(X86FeatureWords const& words) noexcept {
    if (first_missing_feature(words, X86Level::v4) == X86Feature::none) return X86Level::v4;
    if (first_missing_feature(words, X86Level::v3) == X86Feature::none) return X86Level::v3;
    if (first_missing_feature(words, X86Level::v2) == X86Feature::none) return X86Level::v2;
    return X86Level::v1;
}

// The level that the flags of the unit give (THE X86 LEVELS).  A target can
// give more features than its level, as -march=native does, and the level
// then names only the level.
[[nodiscard]] consteval X86Level x86_level_of_target() noexcept {
    return static_cast<X86Level>(CRUCIBLE_X86_LEVEL_OF_TARGET);
}

// The words of the calling core.  The read checks the largest leaf before it
// reads leaf 7 or leaf 0x80000001, and it reads XCR0 only when CPUID sets
// OSXSAVE, because XGETBV faults on a core without it.
[[nodiscard]] inline X86FeatureWords read_x86_feature_words() noexcept {
    constexpr std::uint32_t kLeafOfFeatures = 1;
    constexpr std::uint32_t kLeafOfExtendedFeatures = 7;
    constexpr std::uint32_t kExtendedBase = 0x80000000U;
    constexpr std::uint32_t kExtendedFeatures = 0x80000001U;
    constexpr std::uint32_t kOsxsaveBit = 27;
    X86FeatureWords words{};
    std::uint32_t const largest = read_cpuid(0, 0).eax;
    if (largest >= kLeafOfFeatures) words.leaf1_ecx = read_cpuid(kLeafOfFeatures, 0).ecx;
    if (largest >= kLeafOfExtendedFeatures) words.leaf7_ebx = read_cpuid(kLeafOfExtendedFeatures, 0).ebx;
    std::uint32_t const largest_extended = read_cpuid(kExtendedBase, 0).eax;
    if (largest_extended >= kExtendedFeatures) words.extended1_ecx = read_cpuid(kExtendedFeatures, 0).ecx;
    if (((words.leaf1_ecx >> kOsxsaveBit) & 1U) != 0) words.xcr0 = read_xcr0();
    return words;
}

// The level of the text v1, v2, v3 or v4, or no value for each other text.
[[nodiscard]] constexpr core::Option<X86Level> parse_x86_level(core::TextView text) noexcept {
    if (text == core::TextView{"v1"}) return core::some(X86Level::v1);
    if (text == core::TextView{"v2"}) return core::some(X86Level::v2);
    if (text == core::TextView{"v3"}) return core::some(X86Level::v3);
    if (text == core::TextView{"v4"}) return core::some(X86Level::v4);
    return core::none;
}

// The level of the host with no request, and the requested level when the
// host has it.  A requested level above the host gives the first feature of
// that level that the host does not have.
[[nodiscard]] constexpr core::Result<X86Level, X86Feature> pick_x86_level(X86FeatureWords const& host,
                                                                          core::Option<X86Level> request) noexcept {
    X86Level const highest = x86_level_of(host);
    X86Level const wanted = static_cast<core::Option<X86Level>&&>(request).value_or(highest);
    if (wanted <= highest) return wanted;
    return core::err(first_missing_feature(host, wanted));
}

// The pick from the text of a request, such as the value of a variable of
// the environment.  An empty text is no request.  A text that is no level,
// and a level above the host, end the process with fatal.  The text of the
// fatal names the text of the request, or the level and the missing feature.
[[nodiscard]] inline X86Level require_x86_level(X86FeatureWords const& host, core::TextView request) noexcept {
    if (request.size() == 0) return x86_level_of(host);
    X86Level const wanted = parse_x86_level(request).match(
        [](X86Level level) noexcept -> X86Level { return level; },
        [request]() noexcept -> X86Level {
            core::fatal("the request of an x86-64 level is '{}', and the levels are v1, v2, v3 and v4", request);
        });
    return pick_x86_level(host, core::some(wanted))
        .match([](X86Level picked) noexcept -> X86Level { return picked; },
               [wanted](X86Feature missing) noexcept -> X86Level {
                   core::fatal("the request names the x86-64 level {}, and the host has no {}",
                               core::enum_name_text(wanted), core::enum_name_text(missing));
               });
}

#endif

}  // namespace foundation::os
