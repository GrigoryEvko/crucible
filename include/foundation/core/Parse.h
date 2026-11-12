#pragma once

// The Report family: the integer that a text holds.
//
//   parse<T>(text)   the value of the integer of text as a T, or the
//                    ParseError of the first character that the grammar
//                    refuses
//
// THE GRAMMAR
//   An optional '-' for a signed T.  Then "0x" and at least one hexadecimal
//   digit (0 to 9, a to f, A to F), or at least one decimal digit.  The text
//   holds no white space, no '+' and no character after the last digit.  A
//   zero before the other digits is a digit, so "007" is 7, and "-0" is 0
//   for a signed T.  "0X" is no prefix, and no prefix gives octal or binary.
//   A byte of 0x80 or more is no digit.  This is the grammar of
//   std::from_chars with base 10, with the prefix "0x" for base 16, when
//   the text holds no character after its last digit.
//
// THE ERROR
//   A ParseError holds a kind and a position in the text:
//
//   no_digit               the text ends before its first digit.  The
//                          position is the size of the text, so a position
//                          can be equal to size()
//   unexpected_character   the first character that is no digit, at its
//                          position
//   sign_on_unsigned       a '-' at position 0 for an unsigned T
//   overflow               the first digit that takes the value out of the
//                          range of T, at its position
//
// The walk stops at the first error, so a text with two errors gives the
// error of the first one.
//
// THE VALUE
//   The value collects in T: a multiply by the base and an add of the digit
//   for a positive text, and a multiply and a subtract for a negative text.
//   So the minimum of a signed T reads exactly.  The first digits, which
//   cannot leave the range of T, take the modular operations.  Each other
//   digit takes checked_mul and checked_add or checked_sub, so the first
//   digit that leaves the range gives the overflow.  The base is a constant
//   of each loop, so the multiply needs no divide and no table.  parse is
//   one pass over the text, with no allocation, no locale and no global
//   state, and a constant evaluation can call it.  It reads the text only
//   through starts_with(), window() and the loop of the Region family.

#include <foundation/core/Algorithms.h>
#include <foundation/core/Arithmetic.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Compare.h>
#include <foundation/core/Number.h>
#include <foundation/core/Region.h>
#include <foundation/core/Text.h>

#include <cstddef>
#include <cstdint>

namespace foundation::core {

enum class ParseErrorKind : std::uint8_t {
    no_digit,
    unexpected_character,
    sign_on_unsigned,
    overflow
};

// The error of parse().  The kind comes first, and 7 bytes of padding
// follow it, so the class has 16 bytes, the largest error that a Result
// takes.  Two errors are equal when their kinds and their positions are
// equal, and the padding takes no part in the compare.
struct ParseError final {
    ParseErrorKind kind = ParseErrorKind::no_digit;
    std::size_t position = 0;

    [[nodiscard]] friend constexpr bool operator==(ParseError, ParseError) noexcept = default;
};

namespace detail {

// The value of character as a digit of Base, or Base when the character is
// no digit of Base.  An ASCII letter of A to F with the bit 0x20 is the
// same letter in lower case, and no other byte becomes a to f.
template <unsigned Base>
    requires(Base == 10U || Base == 16U)
[[nodiscard]] constexpr unsigned digit_value_(char character) noexcept {
    unsigned const code = static_cast<unsigned char>(character);
    if (code - unsigned{'0'} < 10U) return code - unsigned{'0'};
    if constexpr (Base == 16U) {
        unsigned const lower = code | 0x20U;
        if (lower - unsigned{'a'} < 6U) return lower - unsigned{'a'} + 10U;
    }
    return Base;
}

// The count of the digits of Base that never leave the range of T.  The
// loop keeps Base^count <= max_of<T>, so the largest value of count digits,
// Base^count - 1, is less than max_of<T>.  For std::uint64_t in base 16 the
// count is 15 and not 16, which costs one checked digit.
template <Integer T, unsigned Base>
[[nodiscard]] consteval std::size_t safe_digits_() noexcept {
    std::uint64_t const largest = static_cast<std::uint64_t>(max_of<T>);
    std::uint64_t power = 1;
    std::size_t count = 0;
    while (power <= largest / Base) {
        power *= Base;
        ++count;
    }
    return count;
}

// The characters of text after its first count characters.  The caller
// makes sure that count <= text.size(), so the window always fits.
[[nodiscard]] constexpr TextView text_after_(TextView text, std::size_t count) noexcept {
    return text.window(count, text.size() - count).value_or(TextView{});
}

// The digits in Base after the sign and the prefix.  first is the position
// of the first digit in the text of parse(), and is_negative is false for
// an unsigned T.  The first safe_digits_ digits cannot leave the range of
// T, so they take the modular operations with no test of an overflow.
// Complexity: linear in the count of the digits.
template <Integer T, unsigned Base>
[[nodiscard]] constexpr Result<T, ParseError> parse_digits_(TextView digits, std::size_t first,
                                                            bool is_negative) noexcept {
    if (digits.size() == 0) return err(ParseError{ParseErrorKind::no_digit, first});
    std::size_t const unchecked = min(digits.size(), safe_digits_<T, Base>());
    T value{0};
    std::size_t position = first;
    for (char const character : digits.window(0, unchecked).value_or(TextView{})) {
        unsigned const digit = digit_value_<Base>(character);
        if (digit >= Base) return err(ParseError{ParseErrorKind::unexpected_character, position});
        T const scaled = wrapping_mul(value, static_cast<T>(Base));
        value = is_negative ? wrapping_sub(scaled, static_cast<T>(digit)) : wrapping_add(scaled, static_cast<T>(digit));
        ++position;
    }
    for (char const character : text_after_(digits, unchecked)) {
        unsigned const digit = digit_value_<Base>(character);
        if (digit >= Base) return err(ParseError{ParseErrorKind::unexpected_character, position});
        ParseError const overflow{ParseErrorKind::overflow, position};
        CRUCIBLE_TRY(scaled, checked_mul(value, static_cast<T>(Base)).ok_or(overflow));
        if (is_negative) {
            CRUCIBLE_TRY(lowered, checked_sub(scaled, static_cast<T>(digit)).ok_or(overflow));
            value = lowered;
        } else {
            CRUCIBLE_TRY(raised, checked_add(scaled, static_cast<T>(digit)).ok_or(overflow));
            value = raised;
        }
        ++position;
    }
    return value;
}

}  // namespace detail

template <Integer T>
[[nodiscard]] constexpr Result<T, ParseError> parse(TextView text) noexcept {
    bool const is_negative = starts_with(text, "-");
    if constexpr (UnsignedInteger<T>) {
        if (is_negative) return err(ParseError{ParseErrorKind::sign_on_unsigned, 0});
    }
    std::size_t const sign = is_negative ? 1 : 0;
    TextView const magnitude = detail::text_after_(text, sign);
    if (starts_with(magnitude, "0x")) {
        return detail::parse_digits_<T, 16U>(detail::text_after_(magnitude, 2), sign + 2, is_negative);
    }
    return detail::parse_digits_<T, 10U>(magnitude, sign, is_negative);
}

}  // namespace foundation::core
