#pragma once

// The Scalar family: the storage formats of a floating value in 16 and 8
// bits, and their conversions with float.
//
//   f16                 IEEE 754 binary16: 1 sign bit, 5 exponent bits and 10
//                       trailing bits, with the bias 15
//   bf16                bfloat16: the upper 16 bits of an IEEE 754 binary32,
//                       with 8 exponent bits and 7 trailing bits
//   fp8_e4m3            E4M3 of the OCP 8-bit floating point specification,
//                       with the bias 7: no infinity, and one NaN for each
//                       sign, S.1111.111.  The highest value is 448.  PyTorch
//                       names it float8_e4m3fn
//   fp8_e5m2            E5M2 of the OCP specification, with the bias 15: the
//                       infinities and the NaNs of IEEE 754.  The highest
//                       value is 57344
//   StorageFloat<T>     one of the four types
//
//   T::from_bits(word)  the value of a word, and value.raw() the word
//   widen<To>(v)        v as a float or a double.  Each value of each format
//                       is a value of float, so the widen is exact
//   round_nonsaturating<To>(x)
//                       x rounded to the nearest value of To, ties to even.
//                       A magnitude that rounds above the highest value, and
//                       an infinity, give an infinity, and a NaN for fp8_e4m3
//   round_saturating<To>(x)
//                       the same rounding, but a magnitude that rounds above
//                       the highest value, and an infinity, give the highest
//                       value with the sign of x
//
// A storage type holds its bits and nothing else.  It has no arithmetic and
// no compare: arithmetic happens in float after a widen, and a compare of
// bits goes through raw().  No float converts to a storage type without a
// call that names its rounding.  A round takes a float only, because a
// double rounded to float and then to a storage type rounds two times.
//
// THE ROUNDING.  Each round obeys IEEE 754 and section 5.2.1 of the OCP
// specification: it rounds the significand to the trailing bits of To with
// ties to even, with the subnormals of To and no flush of a subnormal input
// or output.  Then a magnitude above the highest value of To is an
// overflow.  So the boundary of the overflow is the tie between the highest
// value and one unit above it:
//
//   f16        65520 and above overflow.  The highest code 0x7BFF is odd
//   bf16       0x7F7F8000 and above overflow.  The highest code is odd
//   fp8_e4m3   464 rounds to 448, and above 464 overflows.  The highest
//              code 0x7E is even, and one unit above it is 480
//   fp8_e5m2   61440 and above overflow.  The highest code 0x7B is odd
//
// The OCP specification asks for the two modes of overflow, so each has its
// own name, and no caller gets one of them without its name.  IEEE 754 gives
// f16 and bf16 the mode of round_nonsaturating.  round_saturating of f16 and
// bf16 is the same clamp, for a kernel that names one mode for each format.
//
// THE NaN.  Each conversion keeps the sign of a NaN.  A round of a NaN gives
// the quiet NaN of To with the top bits of the payload of x: the exponent
// field with each bit set, the top trailing bit set, and the next trailing
// bits from the top bits of the significand of x.  fp8_e4m3 has one NaN for
// each sign, so its round of a NaN is S.1111.111.  A widen of a NaN of f16,
// fp8_e4m3 and fp8_e5m2 puts its trailing bits at the top of the significand
// and sets the quiet bit, as the convertFormat of IEEE 754 does with a
// signaling NaN.  bf16 is the upper half of a binary32, so its widen is the
// shift by 16, and a signaling NaN stays signaling.
//
// THE CODE.  Each conversion is a function of the bits.  The portable code
// does integer operations only, so it reads no rounding mode, no flush mode
// and no flag of the floating-point environment, and it raises no flag.  Each
// conversion is constexpr, and a constant evaluation uses the portable code.
//
// A run on x86_64 with F16C uses vcvtps2ph and vcvtph2ps for f16.  The
// immediate 0 of vcvtps2ph selects the round to nearest even, and its bit 2
// is clear, so the instruction does not read MXCSR.RC.  vcvtps2ph ignores
// MXCSR.FTZ, and vcvtph2ps ignores MXCSR.DAZ.  DAZ can make a binary32
// subnormal input of vcvtps2ph a zero, but each such input rounds to a zero
// of f16 with its sign, so the bits are the same.  test_core_storage_float
// shows the bits of the portable code under each of the 16 states of the
// rounding mode, DAZ and FTZ, and test_core_storage_float_exhaustive shows
// them on each of the 2^32 inputs.  The cast (_Float16)x of GCC is vcvtps2ph
// with the immediate 4, which reads MXCSR.RC, so this header does not use
// it.  The instructions set the status flags of MXCSR, and no code of
// Crucible reads them.
//
// bf16 and the two fp8 formats use the portable code on each target, and
// aarch64 uses it for f16 too.  THE DISPATCH below gives the reasons.

#include <foundation/Platform.h>
#include <foundation/core/Bits.h>
#include <foundation/core/Number.h>

#include <concepts>
#include <cstdint>

namespace foundation::core {

namespace detail {

// The parameters of one storage format.  A widen of bf16 is the shift into
// the upper half of a binary32, and each other format quiets a NaN.
struct Binary16Format final {
    using Word = std::uint16_t;
    static constexpr int exponent_bits = 5;
    static constexpr int trailing_bits = 10;
    static constexpr bool has_infinity = true;
    static constexpr bool is_upper_half_of_binary32 = false;
};

struct Bfloat16Format final {
    using Word = std::uint16_t;
    static constexpr int exponent_bits = 8;
    static constexpr int trailing_bits = 7;
    static constexpr bool has_infinity = true;
    static constexpr bool is_upper_half_of_binary32 = true;
};

struct Float8E4M3Format final {
    using Word = std::uint8_t;
    static constexpr int exponent_bits = 4;
    static constexpr int trailing_bits = 3;
    static constexpr bool has_infinity = false;
    static constexpr bool is_upper_half_of_binary32 = false;
};

struct Float8E5M2Format final {
    using Word = std::uint8_t;
    static constexpr int exponent_bits = 5;
    static constexpr int trailing_bits = 2;
    static constexpr bool has_infinity = true;
    static constexpr bool is_upper_half_of_binary32 = false;
};

template <class Format>
concept IsStorageFormat = std::same_as<Format, Binary16Format> || std::same_as<Format, Bfloat16Format>
                       || std::same_as<Format, Float8E4M3Format> || std::same_as<Format, Float8E5M2Format>;

// The overflow rule of a round.
enum class Saturation : std::uint8_t {
    nonsaturating = 0,
    saturating = 1,
};

}  // namespace detail

// The bits of a value of one storage format.  The default value is +0.
template <class Format>
    requires detail::IsStorageFormat<Format>
class StorageFloatOf final {
public:
    using FormatType = Format;
    using Word = typename Format::Word;

    constexpr StorageFloatOf() noexcept = default;

    [[nodiscard]] static constexpr StorageFloatOf from_bits(Word bits) noexcept { return StorageFloatOf{bits}; }

    [[nodiscard]] constexpr Word raw() const noexcept { return bits_; }

    bool operator==(StorageFloatOf const&) const =
        delete("compare two storage values as numbers after widen, or as bits through raw()");

private:
    constexpr explicit StorageFloatOf(Word bits) noexcept : bits_{bits} {}

    Word bits_ = 0;
};

using f16 = StorageFloatOf<detail::Binary16Format>;
using bf16 = StorageFloatOf<detail::Bfloat16Format>;
using fp8_e4m3 = StorageFloatOf<detail::Float8E4M3Format>;
using fp8_e5m2 = StorageFloatOf<detail::Float8E5M2Format>;

template <class T>
concept StorageFloat =
    std::same_as<T, f16> || std::same_as<T, bf16> || std::same_as<T, fp8_e4m3> || std::same_as<T, fp8_e5m2>;

namespace detail {

// The facts of a format that its parameters give.  A code is a magnitude:
// the exponent field above the trailing field, with no sign bit.
template <class Format>
struct StorageFacts final {
    static constexpr int exponent_bits = Format::exponent_bits;
    static constexpr int trailing_bits = Format::trailing_bits;
    static constexpr int width = 1 + exponent_bits + trailing_bits;
    static constexpr std::uint32_t bias = (std::uint32_t{1} << (exponent_bits - 1)) - 1;
    static constexpr std::uint32_t exponent_field = (std::uint32_t{1} << exponent_bits) - 1;
    static constexpr std::uint32_t trailing_mask = (std::uint32_t{1} << trailing_bits) - 1;
    static constexpr std::uint32_t magnitude_mask = (std::uint32_t{1} << (width - 1)) - 1;
    static constexpr std::uint32_t quiet_bit = std::uint32_t{1} << (trailing_bits - 1);
    // The trailing bits of a binary32 that a round drops.
    static constexpr int dropped_bits = 23 - trailing_bits;
    // The code of the highest finite value, and the code that a
    // nonsaturating overflow gives: the infinity, or the NaN of fp8_e4m3.
    static constexpr std::uint32_t highest =
        Format::has_infinity ? (((exponent_field - 1) << trailing_bits) | trailing_mask) : magnitude_mask - 1;
    static constexpr std::uint32_t overflow = Format::has_infinity ? exponent_field << trailing_bits : magnitude_mask;
    // The binary32 exponent field of the lowest normal value of the format.
    static constexpr std::uint32_t lowest_normal_field = 128 - bias;
};

inline constexpr std::uint32_t binary32_magnitude_mask = 0x7FFFFFFFu;
inline constexpr std::uint32_t binary32_infinity = 0x7F800000u;
inline constexpr std::uint32_t binary32_trailing_mask = 0x007FFFFFu;
inline constexpr std::uint32_t binary32_quiet_bit = 0x00400000u;

// True when the magnitude code of the format is a NaN.
template <class Format>
[[nodiscard]] constexpr bool is_nan_code(std::uint32_t magnitude) noexcept {
    using Facts = StorageFacts<Format>;
    if constexpr (Format::has_infinity) {
        return magnitude > Facts::overflow;
    } else {
        return magnitude == Facts::magnitude_mask;
    }
}

// The binary32 bits of a word of the format, with integer operations only.
// A subnormal of the format is a normal binary32 for each format other than
// bf16, so its leading one moves into the hidden bit.
template <class Format>
[[nodiscard]] constexpr std::uint32_t widen_portable(typename Format::Word word) noexcept {
    using Facts = StorageFacts<Format>;
    std::uint32_t const raw = word;
    if constexpr (Format::is_upper_half_of_binary32) {
        return raw << 16;
    } else {
        std::uint32_t const sign = (raw >> (Facts::width - 1)) << 31;
        std::uint32_t const magnitude = raw & Facts::magnitude_mask;
        std::uint32_t const exponent = magnitude >> Facts::trailing_bits;
        std::uint32_t const trailing = magnitude & Facts::trailing_mask;
        if (is_nan_code<Format>(magnitude)) {
            return sign | binary32_infinity | binary32_quiet_bit | (trailing << Facts::dropped_bits);
        }
        if (Format::has_infinity && exponent == Facts::exponent_field) return sign | binary32_infinity;
        if (exponent == 0) {
            if (trailing == 0) return sign;
            int const top = ilog2(trailing).expect("the test above found a trailing field above zero");
            auto const field =
                static_cast<std::uint32_t>(128 - static_cast<int>(Facts::bias) - Facts::trailing_bits + top);
            std::uint32_t const fraction = (trailing << (23 - top)) & binary32_trailing_mask;
            return sign | (field << 23) | fraction;
        }
        return sign | ((exponent + 127 - Facts::bias) << 23) | (trailing << Facts::dropped_bits);
    }
}

// The value of significand / 2^distance, rounded to nearest with ties to
// even.  The caller makes sure that 0 < distance < 32 and that the sum does
// not pass 2^32.
[[nodiscard]] constexpr std::uint32_t shift_to_nearest_even(std::uint32_t significand, int distance) noexcept {
    std::uint32_t const half_less_one = (std::uint32_t{1} << (distance - 1)) - 1;
    std::uint32_t const odd = (significand >> distance) & 1u;
    return (significand + half_less_one + odd) >> distance;
}

// The magnitude code of a finite binary32 magnitude after the round, with
// no bound above: a code above StorageFacts::highest is an overflow.  A
// magnitude in the normal range of the format moves to the bias of the
// format, and the round drops its low trailing bits.  A carry out of the
// trailing bits steps the exponent field, which is the correct code.  A
// smaller magnitude is the integer count of the lowest subnormal of the
// format, rounded.
template <class Format>
[[nodiscard]] constexpr std::uint32_t round_finite_magnitude(std::uint32_t magnitude) noexcept {
    using Facts = StorageFacts<Format>;
    std::uint32_t const field = magnitude >> 23;
    if (field >= Facts::lowest_normal_field) {
        std::uint32_t const rebased = magnitude - ((127 - Facts::bias) << 23);
        return shift_to_nearest_even(rebased, Facts::dropped_bits);
    }
    std::uint32_t const significand = field == 0 ? magnitude : ((magnitude & binary32_trailing_mask) | 0x00800000u);
    std::uint32_t const effective_field = field == 0 ? 1 : field;
    // The value is significand * 2^(field - 150), and the lowest subnormal
    // of the format is 2^(1 - bias - trailing_bits).
    auto const distance =
        static_cast<int>(151 - Facts::bias - static_cast<std::uint32_t>(Facts::trailing_bits) - effective_field);
    // A significand is below 2^24, so a distance of 25 or more is below one
    // half of the lowest subnormal.
    if (distance >= 25) return 0;
    return shift_to_nearest_even(significand, distance);
}

// The word of the format for the binary32 bits, with integer operations
// only.  A NaN comes first, because the round of its bits could carry into
// the sign bit.
template <class Format, Saturation Mode>
[[nodiscard]] constexpr typename Format::Word round_portable(std::uint32_t bits) noexcept {
    using Facts = StorageFacts<Format>;
    using Word = typename Format::Word;
    std::uint32_t const sign = (bits >> 31) << (Facts::width - 1);
    std::uint32_t const magnitude = bits & binary32_magnitude_mask;
    if (magnitude > binary32_infinity) {
        if constexpr (Format::has_infinity) {
            std::uint32_t const payload = (magnitude & binary32_trailing_mask) >> Facts::dropped_bits;
            return static_cast<Word>(sign | Facts::overflow | Facts::quiet_bit | payload);
        } else {
            return static_cast<Word>(sign | Facts::magnitude_mask);
        }
    }
    std::uint32_t const code =
        magnitude == binary32_infinity ? Facts::highest + 1 : round_finite_magnitude<Format>(magnitude);
    if (code > Facts::highest) {
        if constexpr (Mode == Saturation::saturating) {
            return static_cast<Word>(sign | Facts::highest);
        } else {
            return static_cast<Word>(sign | Facts::overflow);
        }
    }
    return static_cast<Word>(sign | code);
}

#if defined(__x86_64__)

using HardwareFloatLanes [[gnu::vector_size(16)]] = float;
using HardwareHalfLanes [[gnu::vector_size(16)]] = short;

// vcvtps2ph with the immediate 0: the round to nearest even, with bit 2
// clear, so MXCSR.RC is not read.  The target attribute lets a test call the
// instruction in a build with no F16C.  A run calls it only on a host with
// F16C.
[[nodiscard, gnu::target("f16c")]] inline std::uint16_t round_f16_by_f16c(std::uint32_t bits) noexcept {
    HardwareFloatLanes const lanes{__builtin_bit_cast(float, bits), 0.0F, 0.0F, 0.0F};
    HardwareHalfLanes const halves = __builtin_ia32_vcvtps2ph(lanes, 0);
    return static_cast<std::uint16_t>(halves[0]);
}

[[nodiscard, gnu::target("f16c")]] inline std::uint32_t widen_f16_by_f16c(std::uint16_t word) noexcept {
    HardwareHalfLanes const halves{static_cast<short>(word), 0, 0, 0, 0, 0, 0, 0};
    HardwareFloatLanes const lanes = __builtin_ia32_vcvtph2ps(halves);
    return __builtin_bit_cast(std::uint32_t, lanes[0]);
}

// The saturating round of F16C: an infinity of vcvtps2ph comes only from an
// overflow or from an infinity, and each gives the highest value.
[[nodiscard, gnu::target("f16c")]] inline std::uint16_t round_f16_saturating_by_f16c(std::uint32_t bits) noexcept {
    std::uint16_t const word = round_f16_by_f16c(bits);
    if ((word & 0x7FFFu) == 0x7C00u) return static_cast<std::uint16_t>((word & 0x8000u) | 0x7BFFu);
    return word;
}

#endif

// THE DISPATCH.  widen_bits and round_bits choose the arm of a conversion:
// F16C for f16 on x86_64, and the portable code in a constant evaluation and
// on each other target and format.
//
// aarch64 has no arm of FCVT or BFCVT.  The two instructions have no
// rounding immediate: they read the rounding mode and the AHP, DN and FZ
// bits of FPCR, so their result is not a function of the bits alone.  And no
// exhaustive run on a real aarch64 CPU shows their bits equal to the
// portable code.  The gate only parses aarch64, and the run under qemu shows
// the model of the architecture, not a CPU.  Such an arm can come back only
// when the floating-point mode is part of the type of the caller and an
// exhaustive run on a real aarch64 host shows equal bits.
//
// AVX-512 BF16 has no arm: vcvtneps2bf16 flushes each binary32 subnormal to
// a zero with its sign, which the round to nearest even does not.
//
// The arm follows F16C of the target, so widen_bits, round_bits and the
// three conversions that call them are in the target namespace (THE TARGET
// NAMESPACE of foundation/Platform.h).  A unit of x86-64 and a unit of
// x86-64-v3 in one program then keep their own bodies, and the link cannot
// give the body of F16C to the unit of x86-64.  Each arm gives the same bits.
inline namespace CRUCIBLE_SIMD_TARGET {

template <class Format>
[[nodiscard]] constexpr std::uint32_t widen_bits(typename Format::Word word) noexcept {
#if defined(__x86_64__) && defined(__F16C__)
    if constexpr (std::same_as<Format, Binary16Format>) {
        if !consteval {
            return widen_f16_by_f16c(word);
        }
    }
#endif
    return widen_portable<Format>(word);
}

template <class Format, Saturation Mode>
[[nodiscard]] constexpr typename Format::Word round_bits(std::uint32_t bits) noexcept {
#if defined(__x86_64__) && defined(__F16C__)
    if constexpr (std::same_as<Format, Binary16Format>) {
        if !consteval {
            if constexpr (Mode == Saturation::saturating) {
                return round_f16_saturating_by_f16c(bits);
            } else {
                return round_f16_by_f16c(bits);
            }
        }
    }
#endif
    return round_portable<Format, Mode>(bits);
}

}  // namespace CRUCIBLE_SIMD_TARGET

// The binary64 bits of binary32 bits, with integer operations only, so a
// subnormal and a signaling NaN keep their value and their bits.
[[nodiscard]] constexpr std::uint64_t widen_binary32_bits(std::uint32_t bits) noexcept {
    std::uint64_t const sign = std::uint64_t{bits >> 31} << 63;
    std::uint32_t const field = (bits >> 23) & 0xFFu;
    std::uint64_t const fraction = bits & binary32_trailing_mask;
    if (field == 0xFFu) return sign | 0x7FF0000000000000u | (fraction << 29);
    if (field == 0) {
        if (fraction == 0) return sign;
        // The value is fraction * 2^-149, a normal binary64.
        int const top = ilog2(fraction).expect("the test above found a fraction above zero");
        auto const double_field = static_cast<std::uint64_t>(1023 - 149 + top);
        std::uint64_t const double_fraction = (fraction << (52 - top)) & 0x000FFFFFFFFFFFFFu;
        return sign | (double_field << 52) | double_fraction;
    }
    return sign | (std::uint64_t{field + 896u} << 52) | (fraction << 29);
}

}  // namespace detail

// The three conversions call the arm of the target (THE DISPATCH), so they
// are in the target namespace too.
inline namespace CRUCIBLE_SIMD_TARGET {

template <Float To, StorageFloat From>
[[nodiscard]] constexpr To widen(From value) noexcept {
    std::uint32_t const bits = detail::widen_bits<typename From::FormatType>(value.raw());
    if constexpr (std::same_as<To, float>) {
        return __builtin_bit_cast(float, bits);
    } else {
        return __builtin_bit_cast(double, detail::widen_binary32_bits(bits));
    }
}

template <StorageFloat To, std::same_as<float> From>
[[nodiscard]] constexpr To round_nonsaturating(From value) noexcept {
    return To::from_bits(detail::round_bits<typename To::FormatType, detail::Saturation::nonsaturating>(
        __builtin_bit_cast(std::uint32_t, value)));
}

template <StorageFloat To, std::same_as<float> From>
[[nodiscard]] constexpr To round_saturating(From value) noexcept {
    return To::from_bits(detail::round_bits<typename To::FormatType, detail::Saturation::saturating>(
        __builtin_bit_cast(std::uint32_t, value)));
}

}  // namespace CRUCIBLE_SIMD_TARGET

}  // namespace foundation::core
