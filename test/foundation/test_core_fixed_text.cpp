// The differential test of the fixed text and the padded text of the Report
// family against the printf of the C library.
//
// For each value, each count of decimals and each width, side and sign, the
// text of Fixed is the text of printf "%.*f" with the same flags, and the
// text of Padded is the text of "%*s", "%*lld" or "%*llu".  The values are
// the edge values of a double, random bits of a fixed seed, which cover each
// exponent with the infinities, the NaNs and the subnormals, values of a
// middle magnitude, and decimal fractions that hold exact ties.  The base
// side also runs in the three other rounding modes, and it gives the text of
// the default mode in each.  Each call of the C library stays in the one
// opt-out region of the class ORACLE, and each function of the region takes
// and gives plain values and texts of the base only.

#include <foundation/core/Array.h>
#include <foundation/core/Format.h>
#include <foundation/core/Index.h>
#include <foundation/core/Report.h>
#include <foundation/core/Scalar.h>
#include <foundation/core/Text.h>

#include <foundation/Platform.h>
#include <foundation/Quarantine.h>

#include "philox_stream.h"

#include <cfenv>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace {

namespace core = ::foundation::core;
using ::crucible::test::PhiloxStream;

// The capacity of a text of the test: a sign, the 309 digits of the largest
// double, a point, 20 decimals and a width of at most 255.
constexpr std::size_t kTextBytes = 600;
using Text = core::FixedText<kTextBytes>;

// The flags of one printf form: the side of the text in its width, and the
// sign.
struct Form {
    core::Align side = core::Align::Right;
    core::SignMark sign = core::SignMark::NegativeOnly;
};

// The rounding modes of the floating environment.
enum class Rounding : std::uint8_t {
    Nearest,
    Upward,
    Downward,
    TowardZero,
};

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ORACLE: the printf of the C library gives the fixed text and the padded text")
namespace {

// The bytes of a C text into a text of the base.
[[nodiscard]] Text text_of(char const* bytes, int count) noexcept {
    Text text{};
    for (int index = 0; index < count && index < static_cast<int>(kTextBytes); ++index) {
        static_cast<void>(text.append(bytes[index]));
    }
    return text;
}

// The text of printf "%*.*f" of the value with the flags of the form.
[[nodiscard]] Text printf_fixed(double value, std::size_t decimals, std::size_t width, Form form) noexcept {
    char bytes[kTextBytes] = {};
    bool const is_left = form.side == core::Align::Left;
    bool const is_plus = form.sign == core::SignMark::Always;
    // The bounds give the compiler the range of each argument, so it can see
    // that the longest text fits the buffer.
    int const field = width > 255 ? 255 : static_cast<int>(width);
    int const digits = decimals > 20 ? 20 : static_cast<int>(decimals);
    int count = 0;
    if (is_left && is_plus) {
        count = std::snprintf(bytes, sizeof bytes, "%-+*.*f", field, digits, value);
    } else if (is_left) {
        count = std::snprintf(bytes, sizeof bytes, "%-*.*f", field, digits, value);
    } else if (is_plus) {
        count = std::snprintf(bytes, sizeof bytes, "%+*.*f", field, digits, value);
    } else {
        count = std::snprintf(bytes, sizeof bytes, "%*.*f", field, digits, value);
    }
    return text_of(bytes, count);
}

// The text of printf "%*s" of the characters of a literal, with the side of
// the form.
[[nodiscard]] Text printf_text(char const* characters, std::size_t width, core::Align side) noexcept {
    char bytes[kTextBytes] = {};
    int const count = std::snprintf(bytes, sizeof bytes, side == core::Align::Left ? "%-*s" : "%*s",
                                    static_cast<int>(width), characters);
    return text_of(bytes, count);
}

// The text of printf "%*lld" of a signed value, and of "%*llu" of an
// unsigned value, with the side of the form.
[[nodiscard]] Text printf_signed(std::int64_t value, std::size_t width, core::Align side) noexcept {
    char bytes[kTextBytes] = {};
    int const count = std::snprintf(bytes, sizeof bytes, side == core::Align::Left ? "%-*lld" : "%*lld",
                                    static_cast<int>(width), static_cast<long long>(value));
    return text_of(bytes, count);
}

[[nodiscard]] Text printf_unsigned(std::uint64_t value, std::size_t width, core::Align side) noexcept {
    char bytes[kTextBytes] = {};
    int const count = std::snprintf(bytes, sizeof bytes, side == core::Align::Left ? "%-*llu" : "%*llu",
                                    static_cast<int>(width), static_cast<unsigned long long>(value));
    return text_of(bytes, count);
}

// Sets the rounding mode of the floating environment.
void set_rounding(Rounding mode) noexcept {
    int const flag = mode == Rounding::Upward     ? FE_UPWARD
                   : mode == Rounding::Downward   ? FE_DOWNWARD
                   : mode == Rounding::TowardZero ? FE_TOWARDZERO
                                                  : FE_TONEAREST;
    CRUCIBLE_FATAL_INVARIANT(std::fesetround(flag) == 0);
}

// The literals of the padded texts, the same characters as kPaddedTexts.
constexpr char const* kPaddedLiterals[] = {"", "a", "abc", "µs", "a name longer than the widths of the test"};

// The text of printf "%*s" of the literal at a position of kPaddedLiterals.
[[nodiscard]] Text printf_literal(std::size_t position, std::size_t width, core::Align side) noexcept {
    CRUCIBLE_FATAL_INVARIANT(position < sizeof kPaddedLiterals / sizeof kPaddedLiterals[0]);
    return printf_text(kPaddedLiterals[position], width, side);
}

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

// The texts of the padded test, in the order of kPaddedLiterals.
constexpr core::FixedArray<core::TextView, 5> kPaddedTexts{core::TextView{""}, core::TextView{"a"},
                                                           core::TextView{"abc"}, core::TextView{"µs"},
                                                           core::TextView{"a name longer than the widths of the test"}};

// The count of the checks that ran, for the report at the end.
std::uint64_t checks_run = 0;

// Ends the test with both texts when the base text is not the oracle text.
void expect_same(core::TextView base, core::TextView oracle, core::TextView what, std::uint64_t value_bits) noexcept {
    ++checks_run;
    if (base == oracle) return;
    core::fatal("the {} of the value with the bits {} is '{}', and printf gives '{}'", what, value_bits, base, oracle);
}

// The text of one Fixed form through format().
template <std::size_t Decimals, std::size_t Width, core::Align Side, core::SignMark Sign>
[[nodiscard]] Text base_fixed(double value) noexcept {
    Text text{};
    static_cast<void>(core::format(text, "{}", core::Fixed<Decimals, Width, Side, Sign>{value}));
    return text;
}

// Checks one Fixed form of the value against printf.
template <std::size_t Decimals, std::size_t Width, core::Align Side, core::SignMark Sign>
void check_fixed_form(double value) noexcept {
    Text const base = base_fixed<Decimals, Width, Side, Sign>(value);
    Text const oracle = printf_fixed(value, Decimals, Width, Form{.side = Side, .sign = Sign});
    expect_same(base.view(), oracle.view(), core::TextView{"fixed text"}, core::bit_cast<std::uint64_t>(value));
}

// The five forms of one count of decimals: no width, a width with each
// side, and a plus sign with each side.
template <std::size_t Decimals>
void check_fixed_forms(double value) noexcept {
    check_fixed_form<Decimals, 0, core::Align::Right, core::SignMark::NegativeOnly>(value);
    check_fixed_form<Decimals, 12, core::Align::Right, core::SignMark::NegativeOnly>(value);
    check_fixed_form<Decimals, 12, core::Align::Left, core::SignMark::NegativeOnly>(value);
    check_fixed_form<Decimals, 9, core::Align::Right, core::SignMark::Always>(value);
    check_fixed_form<Decimals, 30, core::Align::Left, core::SignMark::Always>(value);
}

// Each count of decimals from Decimals to 20, each in its five forms.
template <std::size_t Decimals>
void check_each_decimals(double value) noexcept {
    check_fixed_forms<Decimals>(value);
    if constexpr (Decimals < 20) check_each_decimals<Decimals + 1>(value);
}

// The plain form of the counts of decimals from Decimals to Last.
template <std::size_t Decimals, std::size_t Last>
void check_plain_decimals(double value) noexcept {
    check_fixed_form<Decimals, 0, core::Align::Right, core::SignMark::NegativeOnly>(value);
    if constexpr (Decimals < Last) check_plain_decimals<Decimals + 1, Last>(value);
}

// The edge values: the zeros, the ties at each place, the ends of the
// binades, the subnormals, the largest double, the infinities and the NaNs
// with each sign, and values that round to a zero with a sign.
void test_edge_values() noexcept {
    constexpr core::FixedArray<double, 54> kEdges{
        0.0,
        -0.0,
        1.0,
        -1.0,
        0.5,
        1.5,
        2.5,
        -2.5,
        3.5,
        0.125,
        0.375,
        -0.125,
        0.005,
        0.015,
        0.025,
        0.045,
        -0.001,
        -0.0049,
        0.0049999,
        999.995,
        12.345,
        13.005,
        14.995,
        25.0049,
        99.9999,
        1000.004,
        0.1,
        0.2,
        0.3,
        1.0 / 3.0,
        2.0 / 3.0,
        9007199254740992.0,
        9007199254740993.0,
        18446744073709551616.0,
        1e15,
        1e16,
        1e17,
        1e21,
        1e22,
        1e23,
        1e100,
        1e300,
        1e308,
        -1e308,
        1.7976931348623157e308,
        2.2250738585072014e-308,
        2.2250738585072009e-308,
        4.9406564584124654e-324,
        1e-300,
        1e-20,
        5e-21,
        1.5e-20,
        123456789.123456789,
        3450000000.0,
    };
    for (double const value : kEdges) {
        check_each_decimals<0>(value);
        check_each_decimals<0>(-value);
    }
    constexpr core::FixedArray<std::uint64_t, 9> kSpecialBits{
        0x7FF0000000000000ULL,  // inf
        0xFFF0000000000000ULL,  // -inf
        0x7FF8000000000000ULL,  // a quiet NaN
        0xFFF8000000000000ULL,  // a quiet NaN with the sign bit
        0x7FF0000000000001ULL,  // a signaling NaN
        0x0000000000000001ULL,  // the smallest subnormal
        0x000FFFFFFFFFFFFFULL,  // the largest subnormal
        0x0010000000000000ULL,  // the smallest normal
        0x7FEFFFFFFFFFFFFFULL,  // the largest finite value
    };
    for (std::uint64_t const bits : kSpecialBits) {
        check_each_decimals<0>(core::bit_cast<double>(bits));
    }
}

// Random bits of a fixed seed: each exponent, the subnormals, the
// infinities and the NaNs, each count of decimals in each form.
void test_random_bits() noexcept {
    PhiloxStream stream{0x6D2B79F5A0C3E1B7ULL};
    for (std::uint32_t draw = 0; draw < 1500; ++draw) {
        check_each_decimals<0>(core::bit_cast<double>(stream.next_wide()));
    }
}

// Values of a middle magnitude, from 2^-40 to 2^40: random mantissas with
// a random exponent near the point.
void test_middle_magnitudes() noexcept {
    PhiloxStream stream{0x1F83D9ABFB41BD6BULL};
    for (std::uint32_t draw = 0; draw < 12000; ++draw) {
        std::uint64_t const mantissa = stream.next_wide() & 0x000FFFFFFFFFFFFFULL;
        std::uint64_t const exponent = 1023U - 40U + stream.below(81);
        std::uint64_t const sign = (stream.next_word() & 1U) == 0U ? 0U : 0x8000000000000000ULL;
        double const value = core::bit_cast<double>(sign | (exponent << 52U) | mantissa);
        check_plain_decimals<0, 20>(value);
        check_fixed_form<2, 8, core::Align::Right, core::SignMark::NegativeOnly>(value);
        check_fixed_form<2, 6, core::Align::Right, core::SignMark::Always>(value);
        check_fixed_form<1, 4, core::Align::Right, core::SignMark::NegativeOnly>(value);
    }
}

// Decimal fractions: k / 2^j holds an exact tie at many counts of decimals,
// and k / 10^j is the nearest double to a decimal fraction, whose exact value
// lies just above or just below the tie.
void test_decimal_ties() noexcept {
    PhiloxStream stream{0x9E3779B97F4A7C15ULL};
    constexpr core::FixedArray<double, 7> kTenths{1.0, 10.0, 100.0, 1000.0, 10000.0, 100000.0, 1000000.0};
    for (std::uint32_t draw = 0; draw < 20000; ++draw) {
        auto const numerator = static_cast<double>(stream.below(2000000001));
        double const binary_tie = numerator / static_cast<double>(std::uint64_t{1} << stream.below(12));
        double const decimal =
            numerator / kTenths.at(core::Index<7>::check(stream.below(7)).expect("a draw below 7 is an index"));
        check_plain_decimals<0, 8>(binary_tie);
        check_plain_decimals<0, 8>(-binary_tie);
        check_plain_decimals<0, 8>(decimal);
    }
}

// A float writes the text of the double of the same value, as printf
// promotes a float.
void test_floats() noexcept {
    PhiloxStream stream{0xD1B54A32D192ED03ULL};
    for (std::uint32_t draw = 0; draw < 4000; ++draw) {
        float const value = core::bit_cast<float>(stream.next_word());
        Text base{};
        static_cast<void>(core::format(base, "{}", core::Fixed<3>{value}));
        Text const oracle = printf_fixed(static_cast<double>(value), 3, 0, Form{});
        expect_same(base.view(), oracle.view(), core::TextView{"fixed text of a float"},
                    core::bit_cast<std::uint64_t>(static_cast<double>(value)));
    }
}

// The base side runs in each rounding mode, and the oracle in the default
// mode: the text does not change with the mode.
void test_rounding_modes() noexcept {
    PhiloxStream stream{0x94D049BB133111EBULL};
    constexpr core::FixedArray<Rounding, 3> kModes{Rounding::Upward, Rounding::Downward, Rounding::TowardZero};
    for (Rounding const mode : kModes) {
        for (std::uint32_t draw = 0; draw < 1000; ++draw) {
            double const value = static_cast<double>(stream.below(2000000001)) / 1024.0;
            set_rounding(mode);
            Text const base = base_fixed<3, 0, core::Align::Right, core::SignMark::NegativeOnly>(value);
            set_rounding(Rounding::Nearest);
            Text const oracle = printf_fixed(value, 3, 0, Form{});
            expect_same(base.view(), oracle.view(), core::TextView{"fixed text in a rounding mode"},
                        core::bit_cast<std::uint64_t>(value));
        }
    }
}

// Checks one padded text of each literal against printf.
template <std::size_t Width, core::Align Side>
void check_padded_texts() noexcept {
    std::size_t position = 0;
    for (core::TextView const text : kPaddedTexts) {
        Text base{};
        static_cast<void>(core::format(base, "[{}]", core::Padded<Width, Side>{text}));
        Text const printed = printf_literal(position, Width, Side);
        Text oracle{};
        static_cast<void>(oracle.append('['));
        static_cast<void>(oracle.append(printed.view()));
        static_cast<void>(oracle.append(']'));
        expect_same(base.view(), oracle.view(), core::TextView{"padded text"}, position);
        ++position;
    }
}

// Checks the padded text of each integer against printf.
template <std::size_t Width, core::Align Side>
void check_padded_integers() noexcept {
    constexpr core::FixedArray<std::int64_t, 8> kSigned{
        0, 1, -1, 7, -42, 1234567, -9223372036854775807LL - 1, 9223372036854775807LL};
    constexpr core::FixedArray<std::uint64_t, 5> kUnsigned{0U, 1U, 42U, 1234567U, 18446744073709551615ULL};
    for (std::int64_t const value : kSigned) {
        Text base{};
        static_cast<void>(core::format(base, "{}", core::Padded<Width, Side>{value}));
        Text const oracle = printf_signed(value, Width, Side);
        expect_same(base.view(), oracle.view(), core::TextView{"padded signed integer"},
                    core::bit_cast<std::uint64_t>(value));
        Text narrow{};
        static_cast<void>(core::format(narrow, "{}", core::Padded<Width, Side>{static_cast<std::int8_t>(value)}));
        Text const narrow_oracle = printf_signed(static_cast<std::int8_t>(value), Width, Side);
        expect_same(narrow.view(), narrow_oracle.view(), core::TextView{"padded signed byte"},
                    core::bit_cast<std::uint64_t>(value));
    }
    for (std::uint64_t const value : kUnsigned) {
        Text base{};
        static_cast<void>(core::format(base, "{}", core::Padded<Width, Side>{value}));
        Text const oracle = printf_unsigned(value, Width, Side);
        expect_same(base.view(), oracle.view(), core::TextView{"padded unsigned integer"}, value);
    }
}

template <std::size_t Width>
void check_padded_width() noexcept {
    check_padded_texts<Width, core::Align::Right>();
    check_padded_texts<Width, core::Align::Left>();
    check_padded_integers<Width, core::Align::Right>();
    check_padded_integers<Width, core::Align::Left>();
}

void test_padded() noexcept {
    check_padded_width<0>();
    check_padded_width<1>();
    check_padded_width<2>();
    check_padded_width<3>();
    check_padded_width<4>();
    check_padded_width<9>();
    check_padded_width<24>();
    check_padded_width<38>();
    check_padded_width<255>();
}

// A text longer than a small buffer is cut by format() with Full<char>, and
// the buffer holds its first characters, as for each other argument.
void test_cut_in_a_small_buffer() noexcept {
    core::FixedText<8> small{};
    bool const is_full = core::format(small, "{}", core::Fixed<5, 12>{3.25}).is_err();
    CRUCIBLE_FATAL_INVARIANT(is_full && small.view() == core::TextView{"     3.2"});
    core::FixedText<4> tiny{};
    bool const is_text_full =
        core::format(tiny, "{}", core::Padded<6, core::Align::Left>{core::TextView{"ab"}}).is_err();
    CRUCIBLE_FATAL_INVARIANT(is_text_full && tiny.view() == core::TextView{"ab  "});
    checks_run += 2;
}

}  // namespace

int main() {
    test_edge_values();
    test_random_bits();
    test_middle_magnitudes();
    test_decimal_ties();
    test_floats();
    test_rounding_modes();
    test_padded();
    test_cut_in_a_small_buffer();
    core::report(core::Sink::Out, "test_core_fixed_text: {} texts agree with printf\n", checks_run);
    return 0;
}
