// Tests of the text of a float and a double in the Report family
// (foundation/core/Report.h and foundation/core/Format.h): the shortest
// digits that read back to the value, in the form of std::to_chars with no
// format.
//
// The edge values have their exact texts.  The oracle compares the text of
// format() with the text of std::to_chars, byte for byte, and std::from_chars
// reads the oracle text back to the bits of the value.  The inputs are
// Philox-seeded bit patterns of each class of value: each bit pattern, the
// two ends and a middle of each binade, the subnormals, the band of plain
// integers above the last exact integer, and the floats near each power of
// ten.

#include <foundation/core/Format.h>
#include <foundation/core/Scalar.h>

#include <foundation/Quarantine.h>

#include "../test_assert.h"
#include "philox_stream.h"

#include <cstdint>
#include <utility>

namespace {

using ::foundation::core::decode;
using ::foundation::core::FixedText;
using ::foundation::core::format;
using ::foundation::core::TextView;

// The most characters of the text of a double, with a margin.
constexpr std::size_t float_text_capacity = 32;

[[nodiscard]] double double_of(std::uint64_t bits) {
    return decode<double>(bits).expect("each bit pattern of 64 bits is a double");
}

[[nodiscard]] float float_of(std::uint32_t bits) {
    return decode<float>(bits).expect("each bit pattern of 32 bits is a float");
}

// The text of format() for one value.
template <class Float>
[[nodiscard]] TextView text_of(FixedText<float_text_capacity>& out, Float value) {
    return format(out, "{}", value).expect("the text of a floating value fits 32 characters");
}

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ORACLE: std::to_chars and std::from_chars give the text that the Report family writes")
#include <charconv>

namespace {

// The length of the text that std::to_chars writes for value.
template <class Float>
[[nodiscard]] std::size_t oracle_length(Float value) noexcept {
    char text[64] = {};
    std::to_chars_result const written = std::to_chars(text, text + sizeof text, value);
    return written.ec == std::errc{} ? static_cast<std::size_t>(written.ptr - text) : 0U;
}

// The character at index of the text of std::to_chars, or a zero byte past
// its end.
template <class Float>
[[nodiscard]] char oracle_character(Float value, std::size_t index) noexcept {
    char text[64] = {};
    std::to_chars_result const written = std::to_chars(text, text + sizeof text, value);
    std::size_t const length = static_cast<std::size_t>(written.ptr - text);
    return index < length ? text[index] : '\0';
}

// True when std::from_chars reads the text of std::to_chars back to the bits
// of value.  A NaN reads back as a NaN.
template <class Float>
[[nodiscard]] bool oracle_reads_back(Float value) noexcept {
    char text[64] = {};
    std::to_chars_result const written = std::to_chars(text, text + sizeof text, value);
    Float back{};
    std::from_chars_result const read = std::from_chars(text, written.ptr, back);
    if (read.ec != std::errc{} || read.ptr != written.ptr) return false;
    if (__builtin_isnan(value)) return __builtin_isnan(back);
    return __builtin_memcmp(&back, &value, sizeof(Float)) == 0;
}

// The bits of the value that std::from_chars reads from "1e" and the power.
template <class Float, class Bits>
[[nodiscard]] Bits power_of_ten_bits(int power) noexcept {
    char text[16] = {'1', 'e'};
    std::to_chars_result const written = std::to_chars(text + 2, text + sizeof text, power);
    Float value{};
    static_cast<void>(std::from_chars(text, written.ptr, value));
    return __builtin_bit_cast(Bits, value);
}

}  // namespace

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

// The text of one character of a float text, as a literal.  A float text
// holds digits, a point, a sign, the letter of the exponent and the letters
// of inf and nan.
[[nodiscard]] TextView piece_of(char character) {
    switch (character) {
        case '0':
            return TextView{"0"};
        case '1':
            return TextView{"1"};
        case '2':
            return TextView{"2"};
        case '3':
            return TextView{"3"};
        case '4':
            return TextView{"4"};
        case '5':
            return TextView{"5"};
        case '6':
            return TextView{"6"};
        case '7':
            return TextView{"7"};
        case '8':
            return TextView{"8"};
        case '9':
            return TextView{"9"};
        case '.':
            return TextView{"."};
        case '-':
            return TextView{"-"};
        case '+':
            return TextView{"+"};
        case 'e':
            return TextView{"e"};
        case 'i':
            return TextView{"i"};
        case 'n':
            return TextView{"n"};
        case 'f':
            return TextView{"f"};
        case 'a':
            return TextView{"a"};
        case '\0':
            return TextView{};
        default:
            ::fixy::fatal("std::to_chars wrote a character that no float text holds");
    }
}

// The text of std::to_chars for value, in out: 32 pieces of one character
// each, and an empty piece past the end of the text.
template <class Float>
[[nodiscard]] TextView oracle_text(FixedText<float_text_capacity>& out, Float value) {
    auto const piece = [value](std::size_t index) { return piece_of(oracle_character(value, index)); };
    return format(out, "{}{}{}{}{}{}{}{}{}{}{}{}{}{}{}{}{}{}{}{}{}{}{}{}{}{}{}{}{}{}{}{}", piece(0), piece(1), piece(2),
                  piece(3), piece(4), piece(5), piece(6), piece(7), piece(8), piece(9), piece(10), piece(11), piece(12),
                  piece(13), piece(14), piece(15), piece(16), piece(17), piece(18), piece(19), piece(20), piece(21),
                  piece(22), piece(23), piece(24), piece(25), piece(26), piece(27), piece(28), piece(29), piece(30),
                  piece(31))
        .expect("the text of std::to_chars fits 32 characters");
}

// The count of the values that agree with the oracle.  A value that does not
// agree ends the test with its bits and its text.
std::uint64_t agreed_values = 0;

template <class Float>
void expect_oracle(Float value, std::uint64_t bits) {
    FixedText<float_text_capacity> ours;
    FixedText<float_text_capacity> theirs;
    TextView const text = text_of(ours, value);
    if (!(text == oracle_text(theirs, value)) || text.size() != oracle_length(value) || !oracle_reads_back(value)) {
        ::fixy::fatal("the text {} of the float with the bits {} is not the text of std::to_chars", text, bits);
    }
    ++agreed_values;
}

void expect_double(std::uint64_t bits) { expect_oracle(double_of(bits), bits); }

void expect_float(std::uint32_t bits) { expect_oracle(float_of(bits), bits); }

// The edge values and their exact texts.
void test_edge_values_have_their_texts() {
    FixedText<float_text_capacity> text;
    assert(text_of(text, 0.0) == TextView{"0"});
    assert(text_of(text, -0.0) == TextView{"-0"});
    assert(text_of(text, double_of(0x7FF0000000000000ULL)) == TextView{"inf"});
    assert(text_of(text, double_of(0xFFF0000000000000ULL)) == TextView{"-inf"});
    assert(text_of(text, double_of(0x7FF8000000000000ULL)) == TextView{"nan"});
    assert(text_of(text, double_of(0xFFF8000000000000ULL)) == TextView{"-nan"});
    assert(text_of(text, double_of(0x7FF0000000000001ULL)) == TextView{"nan"});
    assert(text_of(text, double_of(0x7FF4000000ABCDEFULL)) == TextView{"nan"});
    assert(text_of(text, double_of(0x0000000000000001ULL)) == TextView{"5e-324"});
    assert(text_of(text, double_of(0x000FFFFFFFFFFFFFULL)) == TextView{"2.225073858507201e-308"});
    assert(text_of(text, double_of(0x0010000000000000ULL)) == TextView{"2.2250738585072014e-308"});
    assert(text_of(text, double_of(0x7FEFFFFFFFFFFFFFULL)) == TextView{"1.7976931348623157e+308"});
    assert(text_of(text, double_of(0xFFEFFFFFFFFFFFFFULL)) == TextView{"-1.7976931348623157e+308"});
    assert(text_of(text, 0.1) == TextView{"0.1"});
    assert(text_of(text, 1.0 / 3.0) == TextView{"0.3333333333333333"});
    assert(text_of(text, 9.5e-5) == TextView{"9.5e-05"});
    assert(text_of(text, 1e-4) == TextView{"1e-04"});
    assert(text_of(text, 0.001) == TextView{"0.001"});
    assert(text_of(text, 1e15) == TextView{"1e+15"});
    assert(text_of(text, 1e22) == TextView{"1e+22"});
    assert(text_of(text, 1e23) == TextView{"1e+23"});
    assert(text_of(text, 123456789.0) == TextView{"123456789"});
    assert(text_of(text, 9007199254740993.0) == TextView{"9007199254740992"});
    assert(text_of(text, 123456789012345680000.0) == TextView{"123456789012345683968"});
    assert(text_of(text, 1.5e300) == TextView{"1.5e+300"});
    assert(text_of(text, 0.0f) == TextView{"0"});
    assert(text_of(text, -0.0f) == TextView{"-0"});
    assert(text_of(text, float_of(0x00000001U)) == TextView{"1e-45"});
    assert(text_of(text, float_of(0x7F7FFFFFU)) == TextView{"3.4028235e+38"});
    assert(text_of(text, float_of(0x7F800000U)) == TextView{"inf"});
    assert(text_of(text, float_of(0xFFC00000U)) == TextView{"-nan"});
    assert(text_of(text, 0.1f) == TextView{"0.1"});
    assert(text_of(text, 1e7f) == TextView{"1e+07"});
    assert(text_of(text, 16777217.0f) == TextView{"16777216"});
    assert(text_of(text, 12345679360.0f) == TextView{"12345679872"});
}

// A float in a longer text: format() and report() take a float as each other
// argument.
void test_float_mixes_with_other_arguments() {
    FixedText<64> text;
    TextView const mixed = format(text, "{} of {} at {}", 0.25, 3, 2.5f).expect("the text fits 64 characters");
    assert(mixed == TextView{"0.25 of 3 at 2.5"});
    ::fixy::report(::fixy::Sink::Out, "test_core_float_format: a ratio of {} and {}\n", 0.5, -1.25f);
}

// Each bit pattern: the high bits choose the binade, so each class of value
// occurs.
void test_random_bit_patterns_agree_with_the_oracle() {
    ::crucible::test::PhiloxStream stream{0x6F72616E676531ULL};
    for (int round = 0; round < 20000; ++round) {
        expect_double(stream.next_wide());
        expect_float(stream.next_word());
    }
}

// Each binade at its smallest mantissa, where the gap below is half of the
// gap above, at its largest mantissa and at a random mantissa, with each sign.
void test_each_binade_agrees_with_the_oracle() {
    ::crucible::test::PhiloxStream stream{0x62696E616465ULL};
    for (std::uint64_t biased = 0; biased < 0x7FFU; ++biased) {
        for (std::uint64_t sign = 0; sign < 2U; ++sign) {
            std::uint64_t const base = (sign << 63U) | (biased << 52U);
            expect_double(base);
            expect_double(base | 1U);
            expect_double(base | 0x000FFFFFFFFFFFFFULL);
            expect_double(base | (stream.next_wide() & 0x000FFFFFFFFFFFFFULL));
        }
    }
    for (std::uint32_t biased = 0; biased < 0xFFU; ++biased) {
        for (std::uint32_t sign = 0; sign < 2U; ++sign) {
            std::uint32_t const base = (sign << 31U) | (biased << 23U);
            expect_float(base);
            expect_float(base | 1U);
            expect_float(base | 0x007FFFFFU);
            expect_float(base | (stream.next_word() & 0x007FFFFFU));
        }
    }
}

// The subnormals, and the band above the last exact integer, where the plain
// form holds the exact integer: 2^53 to 2^80 for a double and 2^24 to 2^40
// for a float.
void test_subnormals_and_wide_integers_agree_with_the_oracle() {
    ::crucible::test::PhiloxStream stream{0x7375626E6F726DULL};
    for (int round = 0; round < 4000; ++round) {
        expect_double(stream.next_wide() & 0x000FFFFFFFFFFFFFULL);
        expect_float(stream.next_word() & 0x007FFFFFU);
        // The biased exponent 1023 + 53 gives the binade from 2^53 to 2^54,
        // and 127 + 24 the binade from 2^24 to 2^25.
        std::uint64_t const double_binade = 1023U + 53U + stream.below(27U);
        expect_double((double_binade << 52U) | (stream.next_wide() & 0x000FFFFFFFFFFFFFULL));
        std::uint32_t const float_binade = 127U + 24U + static_cast<std::uint32_t>(stream.below(16U));
        expect_float((float_binade << 23U) | (stream.next_word() & 0x007FFFFFU));
    }
}

// The floats near each power of ten, where the decimal exponent changes,
// and their neighbors.
void test_powers_of_ten_agree_with_the_oracle() {
    for (int power = -323; power <= 308; ++power) {
        std::uint64_t const bits = power_of_ten_bits<double, std::uint64_t>(power);
        for (std::uint64_t step = 0; step < 3U; ++step) {
            expect_double(bits + step);
            expect_double(bits - step);
        }
    }
    for (int power = -45; power <= 38; ++power) {
        std::uint32_t const bits = power_of_ten_bits<float, std::uint32_t>(power);
        for (std::uint32_t step = 0; step < 3U; ++step) {
            expect_float(bits + step);
            expect_float(bits - step);
        }
    }
}

}  // namespace

int main() {
    test_edge_values_have_their_texts();
    test_float_mixes_with_other_arguments();
    test_random_bit_patterns_agree_with_the_oracle();
    test_each_binade_agrees_with_the_oracle();
    test_subnormals_and_wide_integers_agree_with_the_oracle();
    test_powers_of_ten_agree_with_the_oracle();
    crucible::test::pass("test_core_float_format: {} values agree with std::to_chars\n", agreed_values);
    return 0;
}
