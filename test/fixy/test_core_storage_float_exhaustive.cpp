// The exhaustive test of the rounds of foundation/core/StorageFloat.h: each
// of the 2^32 binary32 inputs, in PARTS parts.  The command is
//
//   test_core_storage_float_exhaustive PART PARTS
//
// and part PART takes each input whose value modulo PARTS is PART.  PARTS is
// a power of two.  CTest runs each part as one test.  A subnormal input costs
// about two times a normal one, and the stride gives each part the same
// share of the subnormals, so each part has the same cost.
//
// For each input, each format and each mode:
//
//   - The product arm gives the bits of the portable code.  In a build with
//     F16C, the product arm of f16 is vcvtps2ph.
//   - The word holds the rule of the mode.  The test builds the value of
//     each code of the format from its exponent and trailing fields, with no
//     call of the base.  The word is the code nearest to the input, a tie
//     goes to the even code, and a magnitude above the tie of the highest
//     value and one unit above it gives the infinity, or the NaN of
//     fp8_e4m3, with no saturation, and the highest value with it.  A NaN
//     gives the quiet NaN with the sign and the top bits of the payload.
//   - On a host with F16C, vcvtps2ph gives the bits of the portable round of
//     f16 in each mode.
//   - The casts (_Float16)x and (__bf16)x of GCC give the bits of the
//     nonsaturating round of f16 and bf16.  With no -march, each cast is a
//     call of libgcc.
//   - On a host with AVX-512 BF16, vcvtneps2bf16 gives the bits of the
//     portable round of bf16 on each input that is not a binary32
//     subnormal.  On each subnormal that does not round to zero, it gives a
//     zero with the sign.  So the base does not use it.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/Args.h>
#include <foundation/Quarantine.h>
#include <foundation/core/StorageFloat.h>

#include <cstdint>

namespace {

namespace core = ::foundation::core;
namespace detail = ::foundation::core::detail;
using detail::Saturation;

using Test = ::fixy::TestRunnerCtx;

template <class T>
using FactsOf = detail::StorageFacts<typename T::FormatType>;

// The counts of the oracles that ran on the part.
struct Counts final {
    std::uint64_t inputs = 0;
    std::uint64_t f16c_inputs = 0;
    std::uint64_t avx512_bf16_inputs = 0;
    std::uint64_t avx512_bf16_flushes = 0;
};

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ORACLE: the half casts of GCC and the conversion instructions of the host")

[[nodiscard]] std::uint16_t round_by_half_cast(std::uint32_t bits) noexcept {
    _Float16 const half = static_cast<_Float16>(::fixy::bit_cast<float>(bits));
    return __builtin_bit_cast(std::uint16_t, half);
}

[[nodiscard]] std::uint16_t round_by_bfloat16_cast(std::uint32_t bits) noexcept {
    __bf16 const half = static_cast<__bf16>(::fixy::bit_cast<float>(bits));
    return __builtin_bit_cast(std::uint16_t, half);
}

#if defined(__x86_64__)

[[nodiscard]] bool host_has_f16c() noexcept { return __builtin_cpu_supports("f16c") != 0; }

[[nodiscard]] bool host_has_avx512_bf16() noexcept {
    return __builtin_cpu_supports("avx512bf16") != 0 && __builtin_cpu_supports("avx512vl") != 0;
}

[[gnu::noipa, gnu::target("f16c"), nodiscard]] std::uint16_t round_by_f16c(std::uint32_t bits) noexcept {
    return detail::round_f16_by_f16c(bits);
}

[[gnu::noipa, gnu::target("f16c"), nodiscard]] std::uint16_t round_saturating_by_f16c(std::uint32_t bits) noexcept {
    return detail::round_f16_saturating_by_f16c(bits);
}

using Bfloat16Lanes [[gnu::vector_size(16)]] = __bf16;

[[gnu::noipa, gnu::target("avx512bf16,avx512vl"), nodiscard]] std::uint16_t
round_by_avx512_bf16(std::uint32_t bits) noexcept {
    detail::HardwareFloatLanes const lanes{::fixy::bit_cast<float>(bits), 0.0F, 0.0F, 0.0F};
    Bfloat16Lanes const result =
        __builtin_ia32_cvtneps2bf16_v4sf_mask(lanes, Bfloat16Lanes{}, static_cast<unsigned char>(0xFF));
    return __builtin_bit_cast(std::uint16_t, result[0]);
}

#endif

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// 2^count as a double, from its bits.  The caller makes sure that the power
// is a normal double.
[[nodiscard]] double power_of_two(int count) noexcept {
    return ::fixy::bit_cast<double>(static_cast<std::uint64_t>(1023 + count) << 52);
}

// The value of a finite magnitude code of the format, from its fields.
template <class T>
[[nodiscard]] double value_of_code(std::uint32_t code) noexcept {
    constexpr int trailing_bits = T::FormatType::trailing_bits;
    constexpr int bias = (1 << (T::FormatType::exponent_bits - 1)) - 1;
    auto const exponent = static_cast<int>(code >> trailing_bits);
    std::uint32_t const trailing = code & ((std::uint32_t{1} << trailing_bits) - 1);
    if (exponent == 0) return static_cast<double>(trailing) * power_of_two(1 - bias - trailing_bits);
    double const significand = static_cast<double>((std::uint32_t{1} << trailing_bits) + trailing);
    return significand * power_of_two(exponent - bias - trailing_bits);
}

// The value one unit above the highest finite value.
template <class T>
[[nodiscard]] double value_above_highest() noexcept {
    using Facts = FactsOf<T>;
    double const highest = value_of_code<T>(Facts::highest);
    return highest + (highest - value_of_code<T>(Facts::highest - 1));
}

// True when two values that are not NaN are the same value.  Each value of
// the oracle is exact, so the order decides a tie with no tolerance.
[[nodiscard]] bool is_same_value(double left, double right) noexcept { return !(left < right) && !(right < left); }

// True when the word is the round of the binary32 input in the mode, by the
// rule of the header, from the values of the codes.
template <class T, Saturation Mode>
[[nodiscard]] bool holds_rule(std::uint32_t input, std::uint32_t word) noexcept {
    using Facts = FactsOf<T>;
    std::uint32_t const sign = (input >> 31) << (Facts::width - 1);
    std::uint32_t const input_magnitude = input & 0x7FFFFFFFu;
    if (input_magnitude > 0x7F800000u) {
        if constexpr (T::FormatType::has_infinity) {
            return word
                == (sign | Facts::overflow | Facts::quiet_bit
                    | ((input_magnitude & 0x007FFFFFu) >> Facts::dropped_bits));
        } else {
            return word == (sign | Facts::magnitude_mask);
        }
    }
    if ((word & ~Facts::magnitude_mask) != sign) return false;
    std::uint32_t const code = word & Facts::magnitude_mask;
    if (input_magnitude == 0x7F800000u) {
        return code == (Mode == Saturation::saturating ? Facts::highest : Facts::overflow);
    }
    double const magnitude = static_cast<double>(::fixy::bit_cast<float>(input_magnitude));
    double const highest = value_of_code<T>(Facts::highest);
    if (code > Facts::highest) {
        // Only the overflow code of the nonsaturating mode: the magnitude is
        // past the tie of the highest value and one unit above it, or on the
        // tie when the highest code is odd.
        if (Mode == Saturation::saturating || code != Facts::overflow) return false;
        double const tie = (highest + value_above_highest<T>()) / 2;
        return magnitude > tie || (is_same_value(magnitude, tie) && (Facts::highest & 1u) != 0);
    }
    double const value = value_of_code<T>(code);
    bool const is_even = (code & 1u) == 0;
    if (magnitude >= value) {
        if (Mode == Saturation::saturating && code == Facts::highest) return true;
        double const above = code == Facts::highest ? value_above_highest<T>() : value_of_code<T>(code + 1);
        double const to_value = magnitude - value;
        double const to_above = above - magnitude;
        return to_value < to_above || (is_same_value(to_value, to_above) && is_even);
    }
    double const below = value_of_code<T>(code - 1);
    double const to_value = value - magnitude;
    double const to_below = magnitude - below;
    return to_value < to_below || (is_same_value(to_value, to_below) && is_even);
}

template <class T, Saturation Mode>
void check_round(Test const& test, std::uint32_t input) {
    float const value = ::fixy::bit_cast<float>(input);
    std::uint32_t const word = Mode == Saturation::saturating ? core::round_saturating<T>(value).raw()
                                                              : core::round_nonsaturating<T>(value).raw();
    CRUCIBLE_TEST_ASSERT(test, (word == detail::round_portable<typename T::FormatType, Mode>(input)));
    CRUCIBLE_TEST_ASSERT(test, (holds_rule<T, Mode>(input, word)));
}

#if defined(__x86_64__)

// The AVX-512 BF16 check of check_input reads it, so it exists only where
// that check does.
[[nodiscard]] bool is_binary32_subnormal(std::uint32_t input) noexcept {
    std::uint32_t const magnitude = input & 0x7FFFFFFFu;
    return magnitude != 0 && magnitude < 0x00800000u;
}

#endif

void check_input(Test const& test, std::uint32_t input, bool has_f16c, bool has_avx512_bf16, Counts& counts) {
    check_round<core::f16, Saturation::nonsaturating>(test, input);
    check_round<core::f16, Saturation::saturating>(test, input);
    check_round<core::bf16, Saturation::nonsaturating>(test, input);
    check_round<core::bf16, Saturation::saturating>(test, input);
    check_round<core::fp8_e4m3, Saturation::nonsaturating>(test, input);
    check_round<core::fp8_e4m3, Saturation::saturating>(test, input);
    check_round<core::fp8_e5m2, Saturation::nonsaturating>(test, input);
    check_round<core::fp8_e5m2, Saturation::saturating>(test, input);

    std::uint16_t const half = detail::round_portable<detail::Binary16Format, Saturation::nonsaturating>(input);
    std::uint16_t const bfloat = detail::round_portable<detail::Bfloat16Format, Saturation::nonsaturating>(input);
    CRUCIBLE_TEST_ASSERT(test, round_by_half_cast(input) == half);
    CRUCIBLE_TEST_ASSERT(test, round_by_bfloat16_cast(input) == bfloat);
    ++counts.inputs;
#if defined(__x86_64__)
    if (has_f16c) {
        CRUCIBLE_TEST_ASSERT(test, round_by_f16c(input) == half);
        CRUCIBLE_TEST_ASSERT(test, (round_saturating_by_f16c(input)
                                    == detail::round_portable<detail::Binary16Format, Saturation::saturating>(input)));
        ++counts.f16c_inputs;
    }
    if (has_avx512_bf16) {
        std::uint16_t const flushed = round_by_avx512_bf16(input);
        if (flushed != bfloat) {
            CRUCIBLE_TEST_ASSERT(test, is_binary32_subnormal(input) && (flushed & 0x7FFFu) == 0);
            ++counts.avx512_bf16_flushes;
        }
        ++counts.avx512_bf16_inputs;
    }
#else
    static_cast<void>(has_f16c);
    static_cast<void>(has_avx512_bf16);
#endif
}

[[nodiscard]] std::uint32_t argument_at(::fixy::os::Args const& arguments, std::size_t position) noexcept {
    return ::fixy::parse<std::uint32_t>(arguments.get(position).expect("the command is: PART PARTS").view())
        .expect("PART and PARTS are decimal integers");
}

// Checks each input of part PART of PARTS.
int run_part(::fixy::os::Args const& arguments) {
    Test const test{::foundation::effects::testing::test()};
    std::uint32_t const part = argument_at(arguments, 1);
    std::uint32_t const parts = argument_at(arguments, 2);
    CRUCIBLE_TEST_ASSERT(test, ::fixy::has_single_bit(parts) && parts <= 65536 && part < parts);

    bool has_f16c = false;
    bool has_avx512_bf16 = false;
#if defined(__x86_64__)
    has_f16c = host_has_f16c();
    has_avx512_bf16 = host_has_avx512_bf16();
#endif
    Counts counts{};
    for (std::uint64_t input = part; input < (std::uint64_t{1} << 32); input += parts) {
        check_input(test, static_cast<std::uint32_t>(input), has_f16c, has_avx512_bf16, counts);
    }
    // At most 65536 parts, so each part holds at least 127 subnormals of each
    // sign that do not round to zero, and vcvtneps2bf16 flushes each one.
    if (has_avx512_bf16) {
        CRUCIBLE_TEST_ASSERT(test, counts.avx512_bf16_flushes > 0);
    }
    ::fixy::check::pass(test,
                        "part {} of {}: {} inputs, {} through F16C, {} through AVX-512 BF16, of which {} flush a "
                        "subnormal to zero\n",
                        std::uint64_t{part}, std::uint64_t{parts}, counts.inputs, counts.f16c_inputs,
                        counts.avx512_bf16_inputs, counts.avx512_bf16_flushes);
    return 0;
}

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: main takes the arguments of the C runtime as a count and an array of texts")
int main(int argc, char** argv) { return run_part(::fixy::os::args(argc, argv)); }
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
