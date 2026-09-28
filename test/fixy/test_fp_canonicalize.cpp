// The projection of fixy/fp/Canonicalize.h, run on values that the
// compiler cannot fold.
//
// The header pins each case with a static_assert, and a static_assert
// reaches only constant evaluation.  This file runs the same projection
// on the host at run time, and it compares every result with an oracle
// that reads the IEEE 754 fields of the bit pattern and calls nothing
// from <cmath>.  A NaN is an exponent of all ones with a mantissa that is
// not zero, a zero is a pattern with no bit other than the sign, and each
// other pattern passes through.
//
// The float patterns are checked on a prime stride, and the float NaN
// payloads on a second, finer stride in both signs.  The double patterns
// are checked on a fixed stream of words, with the exponent forced to all
// ones in half of them, so a NaN payload is drawn about once in two words.

#include <fixy/fp/Canonicalize.h>

#include <bit>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace fp = fixy::fp;

namespace {

// A read through a volatile prevents a fold of the call into the constant
// that the header already pinned.
[[nodiscard]] std::uint32_t opaque32(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink = bits;
    return sink;
}

[[nodiscard]] std::uint64_t opaque64(std::uint64_t bits) noexcept {
    volatile std::uint64_t sink = bits;
    return sink;
}

[[nodiscard]] constexpr std::uint32_t oracle32(std::uint32_t bits) noexcept {
    constexpr std::uint32_t exponent_mask = 0x7F800000U;
    constexpr std::uint32_t mantissa_mask = 0x007FFFFFU;
    if ((bits & exponent_mask) == exponent_mask && (bits & mantissa_mask) != 0) return fp::kCanonicalQNaN32;
    if ((bits & 0x7FFFFFFFU) == 0) return 0;
    return bits;
}

[[nodiscard]] constexpr std::uint64_t oracle64(std::uint64_t bits) noexcept {
    constexpr std::uint64_t exponent_mask = 0x7FF0000000000000ULL;
    constexpr std::uint64_t mantissa_mask = 0x000FFFFFFFFFFFFFULL;
    if ((bits & exponent_mask) == exponent_mask && (bits & mantissa_mask) != 0) return fp::kCanonicalQNaN64;
    if ((bits & 0x7FFFFFFFFFFFFFFFULL) == 0) return 0;
    return bits;
}

inline constexpr fp::CanonicalizeRecipeSpec kStrictSpec{fp::RoundingMode::RN,
                                                        fp::ReductionDeterminism::BITEXACT_STRICT};
inline constexpr fp::CanonicalizeRecipeSpec kTensorCoreSpec{fp::RoundingMode::RN,
                                                            fp::ReductionDeterminism::BITEXACT_TC};

// The plain projection and the two admitted gated forms answer as the
// oracle does for one float pattern.
[[nodiscard]] bool float_pattern_projects(std::uint32_t bits) noexcept {
    const float value = std::bit_cast<float>(opaque32(bits));
    const std::uint32_t want = oracle32(bits);
    const bool agrees = fp::canonicalize(value) == want && fp::canonicalize_for<kStrictSpec>(value) == want
                     && fp::canonicalize_for<kTensorCoreSpec>(value) == want;
    if (!agrees) std::fprintf(stderr, "float pattern 0x%08X does not project to 0x%08X\n", bits, want);
    return agrees;
}

[[nodiscard]] bool double_pattern_projects(std::uint64_t bits) noexcept {
    const double value = std::bit_cast<double>(opaque64(bits));
    const std::uint64_t want = oracle64(bits);
    const bool agrees = fp::canonicalize(value) == want && fp::canonicalize_for<kStrictSpec>(value) == want
                     && fp::canonicalize_for<kTensorCoreSpec>(value) == want;
    if (!agrees) {
        std::fprintf(stderr, "double pattern 0x%016llX does not project to 0x%016llX\n",
                     static_cast<unsigned long long>(bits), static_cast<unsigned long long>(want));
    }
    return agrees;
}

// The float NaN payloads on a prime stride, under an exponent of all
// ones, in each sign, and the two payloads at the ends.  Complexity:
// about 2^24 / 31 projections.
[[nodiscard]] bool float_nans_project() noexcept {
    for (std::uint32_t mantissa = 1; mantissa <= 0x007FFFFFU; mantissa += 31) {
        if (!float_pattern_projects(0x7F800000U | mantissa)) return false;
        if (!float_pattern_projects(0xFF800000U | mantissa)) return false;
    }
    return float_pattern_projects(0x7FFFFFFFU) && float_pattern_projects(0xFFC00000U);
}

// The float patterns on a prime stride, and the edges the stride can
// miss.  Complexity: about 2^32 / 8191 projections.
[[nodiscard]] bool strided_float_patterns_project() noexcept {
    for (std::uint64_t bits = 0; bits <= 0xFFFFFFFFULL; bits += 8191) {
        if (!float_pattern_projects(static_cast<std::uint32_t>(bits))) return false;
    }
    const float edges[] = {0.0f,
                           -0.0f,
                           std::numeric_limits<float>::infinity(),
                           -std::numeric_limits<float>::infinity(),
                           std::numeric_limits<float>::denorm_min(),
                           -std::numeric_limits<float>::denorm_min(),
                           std::numeric_limits<float>::min(),
                           std::numeric_limits<float>::max(),
                           -std::numeric_limits<float>::max()};
    for (const float edge : edges) {
        if (!float_pattern_projects(std::bit_cast<std::uint32_t>(edge))) return false;
    }
    return true;
}

// A fixed stream of words, the same on every platform: xorshift64.
[[nodiscard]] constexpr std::uint64_t next_word(std::uint64_t& state) noexcept {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

// Complexity: O(samples).
[[nodiscard]] bool sampled_double_patterns_project(std::uint32_t samples) noexcept {
    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    for (std::uint32_t sample = 0; sample < samples; ++sample) {
        const std::uint64_t word = next_word(state);
        if (!double_pattern_projects(word)) return false;
        if (!double_pattern_projects(word | 0x7FF0000000000000ULL)) return false;
    }
    const std::uint64_t edges[] = {0x0000000000000000ULL, 0x8000000000000000ULL, 0x7FF0000000000000ULL,
                                   0xFFF0000000000000ULL, 0x7FF0000000000001ULL, 0x7FF8000000000000ULL,
                                   0x7FFFFFFFFFFFFFFFULL, 0xFFF8000000000000ULL, 0xFFFFFFFFFFFFFFFFULL,
                                   0x0000000000000001ULL, 0x8000000000000001ULL, 0x0010000000000000ULL,
                                   0x7FEFFFFFFFFFFFFFULL};
    for (const std::uint64_t edge : edges) {
        if (!double_pattern_projects(edge)) return false;
    }
    return true;
}

}  // namespace

int main() {
    if (!float_nans_project()) return 1;
    if (!strided_float_patterns_project()) return 2;
    if (!sampled_double_patterns_project(1U << 17)) return 3;
    return 0;
}
