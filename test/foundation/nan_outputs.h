#pragma once

// The values outside the error bound of a sum, for the tests of the rule of
// the canonical NaN (the values outside the bound of foundation/core/Reduce.h):
// the NaNs and the infinities that a run draws, and the bits that a sum form
// gives for an output.

#include <fixy/Core.h>

#include "philox_stream.h"

#include <cstddef>
#include <cstdint>

namespace crucible::test {

inline constexpr std::uint32_t kCanonicalFloatNan = 0x7FC00000u;
inline constexpr std::uint64_t kCanonicalDoubleNan = 0x7FF8000000000000u;

[[nodiscard]] inline std::uint32_t float_bits(float value) noexcept { return ::fixy::bit_cast<std::uint32_t>(value); }
[[nodiscard]] inline std::uint64_t float_bits(double value) noexcept { return ::fixy::bit_cast<std::uint64_t>(value); }

// The bits of an output of a sum form whose value is value: the canonical NaN
// for each NaN, and the bits of value for each other value.
[[nodiscard]] inline std::uint32_t output_bits(float value) noexcept {
    return ::fixy::fp::is_nan(value) ? kCanonicalFloatNan : float_bits(value);
}
[[nodiscard]] inline std::uint64_t output_bits(double value) noexcept {
    return ::fixy::fp::is_nan(value) ? kCanonicalDoubleNan : float_bits(value);
}

// A NaN or an infinity from drawn bits: a quiet NaN with a drawn payload and
// a drawn sign, a signaling NaN with a drawn payload, or an infinity with a
// drawn sign.
[[nodiscard]] inline float special_float(std::uint64_t bits) noexcept {
    std::uint32_t const sign = static_cast<std::uint32_t>(bits >> 63) << 31;
    std::uint32_t const payload = static_cast<std::uint32_t>(bits >> 8) & 0x3FFFFFu;
    switch (bits % 3) {
        case 0:
            return ::fixy::bit_cast<float>(sign | 0x7FC00000u | payload);
        case 1:
            return ::fixy::bit_cast<float>(0x7F800000u | payload | 1u);
        default:
            return ::fixy::bit_cast<float>(sign | 0x7F800000u);
    }
}

[[nodiscard]] inline double special_double(std::uint64_t bits) noexcept {
    std::uint64_t const sign = (bits >> 63) << 63;
    std::uint64_t const payload = (bits >> 8) & 0x7FFFFFFFFFFFFu;
    switch (bits % 3) {
        case 0:
            return ::fixy::bit_cast<double>(sign | 0x7FF8000000000000u | payload);
        case 1:
            return ::fixy::bit_cast<double>(0x7FF0000000000000u | payload | 1u);
        default:
            return ::fixy::bit_cast<double>(sign | 0x7FF0000000000000u);
    }
}

// Puts a drawn NaN or infinity into about one element in 61 of run, at
// positions that the stream of seed draws.  Complexity: O(n).
template <class T>
void put_specials(::fixy::View<T> run, std::uint64_t seed) noexcept {
    PhiloxStream stream{seed};
    for (T& value : run) {
        std::uint64_t const bits = stream.next_wide();
        if (bits % 61 != 0) continue;
        if constexpr (sizeof(T) == sizeof(float)) {
            value = special_float(stream.next_wide());
        } else {
            value = special_double(stream.next_wide());
        }
    }
}

}  // namespace crucible::test
