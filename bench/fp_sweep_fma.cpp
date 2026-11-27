// The inputs and the oracles of the fma sweep (bench/fp_sweep_fma.h).
//
// Each round draws one case of each class from a splitmix stream with a
// fixed seed, so each run checks the same cases.  The classes of a double:
//   0  the exponent of c is the exponent of a * b plus a gap in [-110, 110]
//   1  c is minus the rounded product, also with its last bits changed: the
//      result is the error of the product
//   2  a product of two values of 26 bits is exact, and c is half of its last
//      place: an exact tie, also with a small change
//   3  each limit of fma_by_pairs_ with an offset in [-2, 2]: a factor at
//      2^995, a product at 2^1021 and at 2^-970, an addend at 2^1022, also
//      with each bit of the significands set
//   4  a product near 2^-1000 and c near minus the product: subnormal results
//   5  the zeros of the two signs, also as the result of a cancellation
//   6  sums near the largest value, the infinities and the NaNs
//   7  c is a power of two and a * b is a small value of the other sign: a
//      result just below a power of two
//   8  each bit of the three inputs from the stream
// The classes of a float are 0, 1, 2 (values of 12 bits), 4 (near 2^-140),
// 5, 6, 7 and 8 in the range of a float.  Complexity: count rounds of about
// twenty cases, each with three fma: the form, the exact form and the
// instruction.

#include "fp_sweep_fma.h"

#include <fixy/Core.h>
#include <foundation/core/Float.h>

#include <cstdint>

namespace fp_sweep {

namespace {

// The count of the mismatches that a sweep writes.
constexpr std::uint64_t kShownMismatches = 8;

// A splitmix stream of 64-bit words.
struct Stream {
    std::uint64_t state = 0;

    [[nodiscard]] std::uint64_t next() noexcept {
        state += 0x9E3779B97F4A7C15u;
        std::uint64_t word = state;
        word = (word ^ (word >> 30)) * 0xBF58476D1CE4E5B9u;
        word = (word ^ (word >> 27)) * 0x94D049BB133111EBu;
        return word ^ (word >> 31);
    }
};

// The counts of one sweep.
struct Tally {
    std::uint64_t checked = 0;
    std::uint64_t against_exact = 0;
    std::uint64_t against_instruction = 0;
    std::uint64_t between_oracles = 0;
};

[[nodiscard]] std::uint64_t bits_of(double value) noexcept { return ::fixy::bit_cast<std::uint64_t>(value); }
[[nodiscard]] std::uint32_t bits_of(float value) noexcept { return ::fixy::bit_cast<std::uint32_t>(value); }

// The two results agree when they have the same bits, also for a NaN: each
// NaN result of fp::fma and of the exact form is the canonical NaN.
template <class T>
[[nodiscard]] bool agree(T left, T right) noexcept {
    return bits_of(left) == bits_of(right);
}

// A normal double of a sign, an unbiased exponent in [-1022, 1023] and the
// low 52 bits of a word as its fraction.
[[nodiscard]] double double_of(std::uint64_t fraction, int exponent, bool is_negative) noexcept {
    std::uint64_t const sign = is_negative ? std::uint64_t{1} << 63 : 0;
    auto const biased = static_cast<std::uint64_t>(::fixy::max(-1022, ::fixy::min(exponent, 1023)) + 1023);
    return ::fixy::bit_cast<double>(sign | (biased << 52) | (fraction & 0xFFFFFFFFFFFFFu));
}

// A normal float of a sign, an unbiased exponent in [-126, 127] and the low
// 23 bits of a word as its fraction.
[[nodiscard]] float float_of(std::uint64_t fraction, int exponent, bool is_negative) noexcept {
    std::uint32_t const sign = is_negative ? 0x80000000u : 0u;
    auto const biased = static_cast<std::uint32_t>(::fixy::max(-126, ::fixy::min(exponent, 127)) + 127);
    return ::fixy::bit_cast<float>(sign | (biased << 23) | static_cast<std::uint32_t>(fraction & 0x7FFFFFu));
}

// A draw in [low, low + span) of a word.
[[nodiscard]] int draw_in(std::uint64_t word, int low, int span) noexcept {
    return low + static_cast<int>(word % static_cast<std::uint64_t>(span));
}

[[nodiscard]] bool bit_of(std::uint64_t word, int position) noexcept { return ((word >> position) & 1u) != 0; }

// Compares the form of the baseline unit with the exact form and with the
// instruction of this unit.
template <class T>
void check_case(int class_index, T left, T right, T addend, Tally& tally) noexcept {
    T const form = fma_of_baseline_unit(left, right, addend);
    T const exact = ::foundation::core::detail::fma_by_integers_(left, right, addend);
    T const instruction = ::foundation::core::fp::fma(left, right, addend);
    ++tally.checked;
    bool const is_exact = agree(form, exact);
    if (!is_exact) ++tally.against_exact;
    if (!agree(form, instruction)) ++tally.against_instruction;
    if (!agree(exact, instruction)) ++tally.between_oracles;
    if (!is_exact && tally.against_exact <= kShownMismatches) {
        ::fixy::report(::fixy::Sink::Err, "  class {}: fma({}, {}, {}) gives {}, the exact form gives {}\n",
                       class_index, bits_of(left), bits_of(right), bits_of(addend), bits_of(form), bits_of(exact));
    }
}

// The cases of one round of the double classes.
void check_double_round(Stream& stream, Tally& tally) noexcept {
    std::uint64_t const first = stream.next();
    std::uint64_t const second = stream.next();
    std::uint64_t const third = stream.next();
    std::uint64_t const choice = stream.next();
    int const left_exponent = draw_in(choice, -200, 401);
    int const right_exponent = draw_in(choice >> 16, -200, 401);
    double const left = double_of(first, left_exponent, bit_of(choice, 50));
    double const right = double_of(second, right_exponent, bit_of(choice, 51));
    double const addend =
        double_of(third, left_exponent + right_exponent + draw_in(choice >> 32, -110, 221), bit_of(choice, 52));
    check_case(0, left, right, addend, tally);

    double const product = left * right;
    check_case(1, left, right, -product, tally);
    check_case(1, left, right, ::fixy::bit_cast<double>(bits_of(-product) ^ (third & 0xFFu)), tally);

    double const short_left = double_of(first & ~std::uint64_t{0x7FFFFFFu}, draw_in(choice, -20, 41), false);
    double const short_right = double_of(second & ~std::uint64_t{0x7FFFFFFu}, draw_in(choice >> 8, -20, 41), true);
    double const exact_product = short_left * short_right;
    double const tie = ::fixy::bit_cast<double>((bits_of(exact_product) & 0x8000000000000000u)
                                                | ((((bits_of(exact_product) >> 52) & 0x7FFu) - 53) << 52));
    check_case(2, short_left, short_right, tie, tally);
    check_case(2, short_left, short_right, -tie, tally);
    check_case(2, short_left, short_right, tie + tie * 0x1p-50, tally);

    int const offset = draw_in(choice >> 40, -2, 5);
    std::uint64_t const ones = bit_of(choice, 47) ? ~std::uint64_t{0} : first;
    double const random_addend = double_of(third, draw_in(choice >> 20, -1000, 2000), bit_of(choice, 53));
    check_case(3, double_of(ones, 995 + offset, false), double_of(second, -100, false), random_addend, tally);
    check_case(3, double_of(ones, 995, bit_of(choice, 54)), double_of(ones, 26 + offset, false),
               double_of(third, 1021 - draw_in(choice >> 24, 0, 60), bit_of(choice, 55)), tally);
    double const low_left = double_of(first, -485, false);
    double const low_right = double_of(second, -485 + offset, bit_of(choice, 56));
    check_case(3, low_left, low_right, -(low_left * low_right), tally);
    check_case(3, low_left, low_right, double_of(third, -970 + offset + draw_in(choice >> 28, -60, 120), false), tally);
    check_case(3, double_of(ones, 995, false), double_of(ones, 26, false), double_of(ones, 1022 + offset, false),
               tally);

    int const small_exponent = draw_in(choice, -520, 60);
    double const small_left = double_of(first, small_exponent, false);
    double const small_right = double_of(second, -1000 - small_exponent + draw_in(third, -30, 60), false);
    check_case(4, small_left, small_right,
               -(small_left * small_right) * (1.0 + 0x1p-40 * static_cast<double>(third & 0xFFu)), tally);
    check_case(4, small_left, small_right, ::fixy::bit_cast<double>(third & 0x800FFFFFFFFFFFFFu), tally);

    double const zero = bit_of(choice, 1) ? -0.0 : 0.0;
    double const other_zero = bit_of(choice, 2) ? -0.0 : 0.0;
    check_case(5, zero, right, other_zero, tally);
    check_case(5, left, bit_of(choice, 3) ? -1.0 : 1.0, other_zero, tally);
    check_case(5, -0.0, left, -0.0, tally);

    check_case(6, double_of(first, 1000 + draw_in(choice, 0, 24), false),
               double_of(second, draw_in(third, 0, 24), false),
               double_of(third, 1022 + draw_in(choice >> 8, 0, 2), bit_of(choice, 57)), tally);
    check_case(6, ::fixy::infinity_of<double>, right, addend, tally);
    check_case(6, left, right, ::fixy::quiet_nan_of<double>, tally);

    int const power_exponent = draw_in(choice >> 12, -900, 1800);
    int const tiny_left_exponent = draw_in(choice >> 4, -30, 30);
    double const power = double_of(0, power_exponent, bit_of(choice, 58));
    double const tiny_left = double_of(first, tiny_left_exponent, !bit_of(choice, 58));
    double const tiny_right = double_of(second, power_exponent - draw_in(third, 52, 9) - tiny_left_exponent, false);
    check_case(7, tiny_left, tiny_right, power, tally);

    check_case(8, ::fixy::bit_cast<double>(first), ::fixy::bit_cast<double>(second), ::fixy::bit_cast<double>(third),
               tally);
}

// The cases of one round of the float classes.
void check_float_round(Stream& stream, Tally& tally) noexcept {
    std::uint64_t const first = stream.next();
    std::uint64_t const second = stream.next();
    std::uint64_t const third = stream.next();
    std::uint64_t const choice = stream.next();
    int const left_exponent = draw_in(choice, -50, 101);
    int const right_exponent = draw_in(choice >> 8, -50, 101);
    float const left = float_of(first, left_exponent, bit_of(choice, 40));
    float const right = float_of(second, right_exponent, bit_of(choice, 41));
    float const addend =
        float_of(third, left_exponent + right_exponent + draw_in(choice >> 16, -30, 61), bit_of(choice, 42));
    check_case(0, left, right, addend, tally);

    float const product = left * right;
    check_case(1, left, right, -product, tally);
    check_case(1, left, right, ::fixy::bit_cast<float>(bits_of(-product) ^ static_cast<std::uint32_t>(third & 0xFu)),
               tally);

    float const short_left = float_of(first & ~std::uint64_t{0xFFFu}, draw_in(choice, -10, 21), false);
    float const short_right = float_of(second & ~std::uint64_t{0xFFFu}, draw_in(choice >> 8, -10, 21), true);
    float const exact_product = short_left * short_right;
    float const tie = ::fixy::bit_cast<float>((bits_of(exact_product) & 0x80000000u)
                                              | ((((bits_of(exact_product) >> 23) & 0xFFu) - 24u) << 23));
    check_case(2, short_left, short_right, tie, tally);
    check_case(2, short_left, short_right, -tie, tally);

    int const small_exponent = draw_in(choice, -80, 40);
    float const small_left = float_of(first, small_exponent, false);
    float const small_right = float_of(second, -140 - small_exponent + draw_in(third, -10, 20), false);
    check_case(4, small_left, small_right, -(small_left * small_right) * 1.0001f, tally);
    check_case(4, ::fixy::bit_cast<float>(static_cast<std::uint32_t>(first & 0x807FFFFFu)), right,
               ::fixy::bit_cast<float>(static_cast<std::uint32_t>(third & 0x807FFFFFu)), tally);

    check_case(5, bit_of(choice, 1) ? -0.0f : 0.0f, right, bit_of(choice, 2) ? -0.0f : 0.0f, tally);
    check_case(5, -0.0f, left, -0.0f, tally);

    check_case(6, float_of(first, 120 + draw_in(choice, 0, 8), false), float_of(second, draw_in(third, 0, 8), false),
               float_of(third, 127, bit_of(choice, 43)), tally);
    check_case(6, ::fixy::infinity_of<float>, right, addend, tally);

    float const power = float_of(0, draw_in(choice >> 12, -100, 200), bit_of(choice, 44));
    int const power_exponent = static_cast<int>((bits_of(power) >> 23) & 0xFFu) - 127;
    float const tiny_left = float_of(first, draw_in(choice >> 4, -8, 16), !bit_of(choice, 44));
    int const tiny_left_exponent = static_cast<int>((bits_of(tiny_left) >> 23) & 0xFFu) - 127;
    check_case(7, tiny_left, float_of(second, power_exponent - draw_in(third, 23, 6) - tiny_left_exponent, false),
               power, tally);

    check_case(8, ::fixy::bit_cast<float>(static_cast<std::uint32_t>(first)),
               ::fixy::bit_cast<float>(static_cast<std::uint32_t>(second)),
               ::fixy::bit_cast<float>(static_cast<std::uint32_t>(third)), tally);
}

[[nodiscard]] int report_tally(::fixy::TextView kind, Tally const& tally) noexcept {
    ::fixy::report(::fixy::Sink::Out,
                   "fp_sweep fma {}: {} cases, {} against the exact form, {} against the instruction, {} between "
                   "the two oracles\n",
                   kind, tally.checked, tally.against_exact, tally.against_instruction, tally.between_oracles);
    bool const holds = tally.against_exact == 0 && tally.against_instruction == 0 && tally.between_oracles == 0;
    return holds ? 0 : 1;
}

}  // namespace

int sweep_fma_float(std::uint64_t count) noexcept {
    if (!is_baseline_unit_unfused()) {
        ::fixy::report(::fixy::Sink::Err, "fp_sweep fma float: the baseline unit does not run the form in doubles: it "
                                          "has a fused multiply-add, no floor crucible_fp_strict, or fast math\n");
        return 1;
    }
    Stream stream{0x5EED00000000F10Au};
    Tally tally{};
    for (std::uint64_t round = 0; round < count; ++round)
        check_float_round(stream, tally);
    return report_tally("float", tally);
}

int sweep_fma_double(std::uint64_t count) noexcept {
    if (!is_baseline_unit_unfused()) {
        ::fixy::report(::fixy::Sink::Err, "fp_sweep fma double: the baseline unit does not run the form in doubles: it "
                                          "has a fused multiply-add, no floor crucible_fp_strict, or fast math\n");
        return 1;
    }
    Stream stream{0x5EED00000000D0B1u};
    Tally tally{};
    for (std::uint64_t round = 0; round < count; ++round)
        check_double_round(stream, tally);
    return report_tally("double", tally);
}

}  // namespace fp_sweep
