// Tests of parse() of the Report family (foundation/core/Parse.h) at run
// time.
//
// The test builds each text one character at a time.  A reference reads the
// same characters: a second form of the grammar, with the magnitude in 64
// bits and the overflow test of a divide.  parse must give the value or the
// error of the reference, with its kind and its position.  The oracle
// std::from_chars reads the same characters, and parse must agree with it on
// each text that it reads to its end.  The texts are:
//
//   - each value of the 8-bit and the 16-bit types, and 1000 values past
//     each limit, in decimal and in hexadecimal
//   - Philox-seeded values of each Integer type, in the two bases
//   - Philox-seeded texts of digits, signs, prefixes and other characters.

#include <foundation/core/Format.h>
#include <foundation/core/Parse.h>

#include <foundation/Quarantine.h>

#include "../test_assert.h"
#include "philox_stream.h"

#include <cstdint>

namespace {

using ::foundation::core::FixedText;
using ::foundation::core::format;
using ::foundation::core::max_of;
using ::foundation::core::min_of;
using ::foundation::core::parse;
using ::foundation::core::ParseError;
using ::foundation::core::ParseErrorKind;
using ::foundation::core::Result;
using ::foundation::core::SignedInteger;
using ::foundation::core::TextView;

// The most characters of a test text.
constexpr std::size_t text_capacity = 48;

// The text of one character of a test text, as a literal.
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
        case 'a':
            return TextView{"a"};
        case 'b':
            return TextView{"b"};
        case 'c':
            return TextView{"c"};
        case 'd':
            return TextView{"d"};
        case 'e':
            return TextView{"e"};
        case 'f':
            return TextView{"f"};
        case 'A':
            return TextView{"A"};
        case 'B':
            return TextView{"B"};
        case 'C':
            return TextView{"C"};
        case 'D':
            return TextView{"D"};
        case 'E':
            return TextView{"E"};
        case 'F':
            return TextView{"F"};
        case 'x':
            return TextView{"x"};
        case 'X':
            return TextView{"X"};
        case '-':
            return TextView{"-"};
        case '+':
            return TextView{"+"};
        case ' ':
            return TextView{" "};
        case 'g':
            return TextView{"g"};
        case 'G':
            return TextView{"G"};
        case '/':
            return TextView{"/"};
        case ':':
            return TextView{":"};
        case '@':
            return TextView{"@"};
        case '`':
            return TextView{"`"};
        case '\x80':
            return TextView{"\x80"};
        case '\xFF':
            return TextView{"\xFF"};
        default:
            ::fixy::fatal("the test put a character that no piece holds");
    }
}

// The characters that the random texts take, and the count of each class.
// The first 22 are the digits of base 16, and the others are no digit of
// either base.
constexpr char alphabet_character(std::uint64_t index) {
    switch (index) {
        case 0:
            return '0';
        case 1:
            return '1';
        case 2:
            return '2';
        case 3:
            return '3';
        case 4:
            return '4';
        case 5:
            return '5';
        case 6:
            return '6';
        case 7:
            return '7';
        case 8:
            return '8';
        case 9:
            return '9';
        case 10:
            return 'a';
        case 11:
            return 'b';
        case 12:
            return 'c';
        case 13:
            return 'd';
        case 14:
            return 'e';
        case 15:
            return 'f';
        case 16:
            return 'A';
        case 17:
            return 'B';
        case 18:
            return 'C';
        case 19:
            return 'D';
        case 20:
            return 'E';
        case 21:
            return 'F';
        case 22:
            return 'x';
        case 23:
            return 'X';
        case 24:
            return '-';
        case 25:
            return '+';
        case 26:
            return ' ';
        case 27:
            return 'g';
        case 28:
            return 'G';
        case 29:
            return '/';
        case 30:
            return ':';
        case 31:
            return '@';
        case 32:
            return '`';
        case 33:
            return '\x80';
        default:
            return '\xFF';
    }
}

constexpr std::uint64_t alphabet_size = 35;

// The characters of a hexadecimal digit and of a decimal digit.
[[nodiscard]] char hex_digit_character(std::uint64_t value, bool is_upper) {
    if (value < 10U) return alphabet_character(value);
    return alphabet_character(is_upper ? value + 6U : value);
}

// A text that grows by one character at a time.  format() writes a full
// text, so each append writes the other buffer.
class TextBuilder {
    FixedText<text_capacity> first_;
    FixedText<text_capacity> second_;
    bool is_in_first_ = true;

public:
    void append(char character) {
        FixedText<text_capacity>& from = is_in_first_ ? first_ : second_;
        FixedText<text_capacity>& to = is_in_first_ ? second_ : first_;
        static_cast<void>(format(to, "{}{}", from.view(), piece_of(character)).expect("a test text fits its buffer"));
        is_in_first_ = !is_in_first_;
    }

    [[nodiscard]] TextView view() const { return is_in_first_ ? first_.view() : second_.view(); }
};

// The grammar of parse(), read one character at a time.  The magnitude
// collects in 64 bits, and a digit overflows when the magnitude is more than
// (limit - digit) / base.  The first error stays.
template <class T>
class Reference {
    std::uint64_t magnitude_ = 0;
    std::uint64_t base_ = 10;
    std::size_t position_ = 0;
    std::size_t sign_length_ = 0;
    std::uint64_t digits_ = 0;
    bool is_negative_ = false;
    bool has_error_ = false;
    ParseError error_{};

    void refuse(ParseErrorKind kind, std::size_t position) {
        if (has_error_) return;
        has_error_ = true;
        error_ = ParseError{kind, position};
    }

    // The largest magnitude of the sign: max_of<T> + 1 is the magnitude of
    // min_of<T> in two's complement.
    [[nodiscard]] std::uint64_t limit() const {
        if (is_negative_) return static_cast<std::uint64_t>(max_of<T>) + 1U;
        return static_cast<std::uint64_t>(max_of<T>);
    }

    // The value of a digit of the base, or the base for each other
    // character.
    [[nodiscard]] std::uint64_t digit_of(char character) const {
        std::uint64_t value = base_;
        if (character >= '0' && character <= '9') value = static_cast<std::uint64_t>(character - '0');
        if (base_ == 16U && character >= 'a' && character <= 'f') {
            value = static_cast<std::uint64_t>(character - 'a') + 10U;
        }
        if (base_ == 16U && character >= 'A' && character <= 'F') {
            value = static_cast<std::uint64_t>(character - 'A') + 10U;
        }
        return value;
    }

public:
    void put(char character) {
        std::size_t const position = position_++;
        if (has_error_) return;
        if (position == 0 && character == '-') {
            if constexpr (SignedInteger<T>) {
                is_negative_ = true;
                sign_length_ = 1;
            } else {
                refuse(ParseErrorKind::sign_on_unsigned, 0);
            }
            return;
        }
        // "0x" right after the sign is the prefix: the 0 was the first digit,
        // and it goes.
        if (character == 'x' && base_ == 10U && position == sign_length_ + 1 && digits_ == 1U && magnitude_ == 0U) {
            base_ = 16;
            digits_ = 0;
            return;
        }
        std::uint64_t const digit = digit_of(character);
        if (digit >= base_) {
            refuse(ParseErrorKind::unexpected_character, position);
            return;
        }
        if (magnitude_ > (limit() - digit) / base_) {
            refuse(ParseErrorKind::overflow, position);
            return;
        }
        magnitude_ = magnitude_ * base_ + digit;
        ++digits_;
    }

    // The result at the end of the text.
    [[nodiscard]] Result<T, ParseError> result() {
        if (digits_ == 0U) refuse(ParseErrorKind::no_digit, position_);
        if (has_error_) return ::foundation::core::err(error_);
        if (is_negative_) return static_cast<T>(std::uint64_t{0} - magnitude_);
        return static_cast<T>(magnitude_);
    }
};

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ORACLE: std::from_chars reads the integer that parse reads from a text")
#include <charconv>

namespace {

// The verdict of std::from_chars on a whole text.
template <class T>
struct OracleVerdict {
    bool reads_to_end = false;
    bool is_out_of_range = false;
    T value{};
};

// The characters of a test text for std::from_chars.  A "0x" after the
// optional sign goes, and the rest reads in base 16.  std::from_chars takes
// a sign after the prefix, and the grammar of parse does not, so such a text
// reads to no end.
class OracleText {
    char text_[text_capacity] = {};
    std::size_t length_ = 0;

public:
    void put(char character) noexcept {
        if (length_ < text_capacity) text_[length_] = character;
        ++length_;
    }

    template <class T>
    [[nodiscard]] OracleVerdict<T> read() const noexcept {
        OracleVerdict<T> verdict{};
        std::size_t const length = length_ < text_capacity ? length_ : text_capacity;
        char stripped[text_capacity] = {};
        std::size_t kept = 0;
        std::size_t index = 0;
        int base = 10;
        if (length > 0 && text_[0] == '-') stripped[kept++] = text_[index++];
        if (length - index >= 2 && text_[index] == '0' && text_[index + 1] == 'x') {
            base = 16;
            index += 2;
            if (index < length && (text_[index] == '-' || text_[index] == '+')) return verdict;
        }
        while (index < length)
            stripped[kept++] = text_[index++];
        std::from_chars_result const read = std::from_chars(stripped, stripped + kept, verdict.value, base);
        verdict.reads_to_end = read.ptr == stripped + kept;
        verdict.is_out_of_range = read.ec == std::errc::result_out_of_range;
        if (read.ec != std::errc{} && !verdict.is_out_of_range) verdict.reads_to_end = false;
        return verdict;
    }
};

}  // namespace

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

// The count of the texts whose results agree.
std::uint64_t agreed_texts = 0;
std::uint64_t oracle_texts = 0;

// The text of a result, for the message of a failure.
template <class T>
[[nodiscard]] TextView text_of_result(FixedText<64>& out, Result<T, ParseError> const& result) {
    return result
        .match([&out](T const& value) { return format(out, "the value {}", value); },
               [&out](ParseError const& error) {
                   return format(out, "the error {} at {}", static_cast<int>(error.kind), error.position);
               })
        .expect("the text of a result fits 64 characters");
}

template <class T>
[[nodiscard]] bool same_result(Result<T, ParseError> const& left, Result<T, ParseError> const& right) {
    return left.match(
        [&right](T const& value) {
            return right.match([value](T const& other) { return other == value; },
                               [](ParseError const&) { return false; });
        },
        [&right](ParseError const& error) {
            return right.match([](T const&) { return false; },
                               [error](ParseError const& other) { return other == error; });
        });
}

// A text of characters with its reference and its oracle.
template <class T>
class Case {
    TextBuilder text_;
    Reference<T> reference_;
    OracleText oracle_;

public:
    void put(char character) {
        text_.append(character);
        reference_.put(character);
        oracle_.put(character);
    }

    // parse must give the result of the reference, and agree with the
    // oracle on each text that the oracle reads to its end.
    void expect_agreement() {
        TextView const text = text_.view();
        Result<T, ParseError> const ours = parse<T>(text);
        Result<T, ParseError> const expected = reference_.result();
        if (!same_result(ours, expected)) {
            FixedText<64> ours_text;
            FixedText<64> expected_text;
            ::fixy::fatal("parse of the text \"{}\" gives {}, and the reference gives {}", text,
                          text_of_result(ours_text, ours), text_of_result(expected_text, expected));
        }
        ++agreed_texts;
        OracleVerdict<T> const verdict = oracle_.template read<T>();
        if (!verdict.reads_to_end) {
            if (ours.is_ok()) ::fixy::fatal("parse reads the text \"{}\", and std::from_chars does not", text);
            return;
        }
        bool const agrees = verdict.is_out_of_range
                              ? ours.err().value_or(ParseError{}).kind == ParseErrorKind::overflow
                              : ours.match([&verdict](T const& value) { return value == verdict.value; },
                                           [](ParseError const&) { return false; });
        if (!agrees) ::fixy::fatal("parse of the text \"{}\" does not agree with std::from_chars", text);
        ++oracle_texts;
    }
};

// The decimal text of value, and its hexadecimal text with leading zeros.
template <class T>
void expect_value_texts(std::int64_t value, std::uint64_t leading_zeros, bool is_upper) {
    bool const is_negative = value < 0;
    std::uint64_t const magnitude =
        is_negative ? std::uint64_t{0} - static_cast<std::uint64_t>(value) : static_cast<std::uint64_t>(value);
    Case<T> decimal;
    if (is_negative) decimal.put('-');
    bool is_leading = true;
    for (std::uint64_t power = 1000000000000000000ULL; power > 0U; power /= 10U) {
        std::uint64_t const digit = magnitude / power % 10U;
        if (digit == 0U && is_leading && power > 1U) continue;
        is_leading = false;
        decimal.put(hex_digit_character(digit, false));
    }
    decimal.expect_agreement();

    Case<T> hexadecimal;
    if (is_negative) hexadecimal.put('-');
    hexadecimal.put('0');
    hexadecimal.put('x');
    for (std::uint64_t zero = 0; zero < leading_zeros; ++zero)
        hexadecimal.put('0');
    is_leading = true;
    for (int shift = 60; shift >= 0; shift -= 4) {
        std::uint64_t const digit = (magnitude >> shift) & 0xFU;
        if (digit == 0U && is_leading && shift > 0) continue;
        is_leading = false;
        hexadecimal.put(hex_digit_character(digit, is_upper));
    }
    hexadecimal.expect_agreement();
}

// Each value of a short type, and 1000 values past each limit.
template <class T>
void expect_each_short_value() {
    std::int64_t const lowest = std::int64_t{min_of<T>} - 1000;
    std::int64_t const highest = std::int64_t{max_of<T>} + 1000;
    for (std::int64_t value = lowest; value <= highest; ++value) {
        std::uint64_t const bits = static_cast<std::uint64_t>(value);
        expect_value_texts<T>(value, bits % 3U, (bits & 4U) != 0U);
    }
}

void test_each_short_value_reads_back() {
    expect_each_short_value<std::int8_t>();
    expect_each_short_value<std::uint8_t>();
    expect_each_short_value<std::int16_t>();
    expect_each_short_value<std::uint16_t>();
}

// The magnitude of a 64-bit value can be 2^63, so the wide values take the
// text of the magnitude through a separate path.
template <class T>
void expect_wide_value(std::uint64_t magnitude, bool is_negative, std::uint64_t leading_zeros, bool is_upper) {
    Case<T> decimal;
    if (is_negative) decimal.put('-');
    for (std::uint64_t zero = 0; zero < leading_zeros; ++zero)
        decimal.put('0');
    bool is_leading = true;
    for (std::uint64_t power = 10000000000000000000ULL; power > 0U; power /= 10U) {
        std::uint64_t const digit = magnitude / power % 10U;
        if (digit == 0U && is_leading && power > 1U) continue;
        is_leading = false;
        decimal.put(hex_digit_character(digit, false));
    }
    decimal.expect_agreement();

    Case<T> hexadecimal;
    if (is_negative) hexadecimal.put('-');
    hexadecimal.put('0');
    hexadecimal.put('x');
    is_leading = true;
    for (int shift = 60; shift >= 0; shift -= 4) {
        std::uint64_t const digit = (magnitude >> shift) & 0xFU;
        if (digit == 0U && is_leading && shift > 0) continue;
        is_leading = false;
        hexadecimal.put(hex_digit_character(digit, is_upper));
    }
    hexadecimal.expect_agreement();
}

// Philox-seeded magnitudes of each width, with each sign, and the
// magnitudes next to the limits of the 64-bit types.
template <class T>
void expect_random_values(std::uint64_t seed) {
    ::foundation::test::PhiloxStream stream{seed};
    for (int round = 0; round < 20000; ++round) {
        std::uint64_t const width = 1U + stream.below(64U);
        std::uint64_t const mask = width == 64U ? ~std::uint64_t{0} : (std::uint64_t{1} << width) - 1U;
        std::uint64_t const choice = stream.next_wide();
        expect_wide_value<T>(stream.next_wide() & mask, (choice & 1U) != 0U, (choice >> 1) % 3U, (choice & 8U) != 0U);
    }
    for (std::uint64_t step = 0; step < 3U; ++step) {
        for (int sign = 0; sign < 2; ++sign) {
            expect_wide_value<T>(9223372036854775807ULL + step, sign == 1, 0, false);
            expect_wide_value<T>(~std::uint64_t{0} - step, sign == 1, 0, true);
        }
    }
}

void test_random_values_read_back() {
    expect_random_values<std::int8_t>(0x7061727365303031ULL);
    expect_random_values<std::uint8_t>(0x7061727365303032ULL);
    expect_random_values<std::int16_t>(0x7061727365303033ULL);
    expect_random_values<std::uint16_t>(0x7061727365303034ULL);
    expect_random_values<std::int32_t>(0x7061727365303035ULL);
    expect_random_values<std::uint32_t>(0x7061727365303036ULL);
    expect_random_values<std::int64_t>(0x7061727365303037ULL);
    expect_random_values<std::uint64_t>(0x7061727365303038ULL);
}

// One random text of a seed: an optional sign, an optional prefix, and up to
// 24 characters that are mostly digits.  The same seed gives the same text
// for each type.
template <class T>
void expect_random_text(std::uint64_t seed) {
    ::foundation::test::PhiloxStream stream{seed};
    Case<T> text;
    if (stream.below(3U) == 0U) text.put('-');
    if (stream.below(3U) == 0U) {
        text.put('0');
        text.put('x');
    }
    std::uint64_t const length = stream.below(25U);
    for (std::uint64_t index = 0; index < length; ++index) {
        std::uint64_t const roll = stream.below(100U);
        if (roll < 60U) {
            text.put(alphabet_character(stream.below(10U)));
        } else if (roll < 90U) {
            text.put(alphabet_character(stream.below(22U)));
        } else {
            text.put(alphabet_character(stream.below(alphabet_size)));
        }
    }
    text.expect_agreement();
}

void test_random_texts_agree_with_the_reference_and_the_oracle() {
    ::foundation::test::PhiloxStream seeds{0x7061727365746578ULL};
    for (int round = 0; round < 20000; ++round) {
        std::uint64_t const seed = seeds.next_wide();
        expect_random_text<std::int8_t>(seed);
        expect_random_text<std::uint8_t>(seed);
        expect_random_text<std::int16_t>(seed);
        expect_random_text<std::uint16_t>(seed);
        expect_random_text<std::int32_t>(seed);
        expect_random_text<std::uint32_t>(seed);
        expect_random_text<std::int64_t>(seed);
        expect_random_text<std::uint64_t>(seed);
    }
}

// A text that a program builds at run time, in a longer report.
void test_parse_reads_a_formatted_text() {
    FixedText<32> text;
    TextView const written = format(text, "{}", -9223372036854775807LL - 1).expect("the text fits 32 characters");
    assert(parse<std::int64_t>(written).value_or(0) == min_of<std::int64_t>);
    ParseError const unsigned_error = parse<std::uint64_t>(written).err().value_or(ParseError{});
    assert(unsigned_error == (ParseError{ParseErrorKind::sign_on_unsigned, 0}));
    ::fixy::report(::fixy::Sink::Out, "test_core_parse: \"{}\" reads back\n", written);
}

static_assert(alphabet_character(21) == 'F' && alphabet_character(22) == 'x');
static_assert(alphabet_character(alphabet_size - 1U) == '\xFF');

}  // namespace

int main() {
    test_parse_reads_a_formatted_text();
    test_each_short_value_reads_back();
    test_random_values_read_back();
    test_random_texts_agree_with_the_reference_and_the_oracle();
    crucible::test::pass("test_core_parse: {} texts agree with the reference, and {} with std::from_chars\n",
                         agreed_texts, oracle_texts);
    return 0;
}
