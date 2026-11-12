// The compile-time checks of foundation/core/Parse.h.

#include <foundation/core/Parse.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace foundation::core::detail::parse_checks {

// True when parse<T> reads text to expected.
template <class T>
[[nodiscard]] consteval bool reads(TextView text, T expected) {
    Result<T, ParseError> result = parse<T>(text);
    return result.is_ok() && static_cast<Result<T, ParseError>&&>(result).value_or(T{0}) == expected;
}

// True when parse<T> refuses text with kind at position.
template <class T>
[[nodiscard]] consteval bool refuses(TextView text, ParseErrorKind kind, std::size_t position) {
    Result<T, ParseError> const result = parse<T>(text);
    return result.is_err() && result.err().value_or(ParseError{}) == ParseError{kind, position};
}

// The probe is a variable template and not a concept, because it gates
// nothing: it asks which types parse accepts.
template <class T>
constexpr bool parses_to = requires(TextView text) { parse<T>(text); };

// The gate: each Integer, and no bool, no character type, no Float, no
// long double and no cv-qualified type.
static_assert(parses_to<std::int8_t> && parses_to<std::uint8_t> && parses_to<std::int64_t>);
static_assert(parses_to<std::uint64_t> && !parses_to<bool> && !parses_to<char> && !parses_to<char8_t>);
static_assert(!parses_to<wchar_t> && !parses_to<float> && !parses_to<double> && !parses_to<long double>);
static_assert(!parses_to<int const> && !parses_to<int volatile>);
static_assert(std::is_same_v<decltype(parse<int>(TextView{})), Result<int, ParseError>>);

// The error is a value that a Result takes.
static_assert(ErrorValue<ParseError> && std::is_trivially_copyable_v<ParseError> && sizeof(ParseError) == 16);
static_assert(ParseError{} == ParseError{ParseErrorKind::no_digit, 0});
static_assert(!(ParseError{ParseErrorKind::overflow, 2} == ParseError{ParseErrorKind::overflow, 3}));

// Decimal and hexadecimal values.
static_assert(reads<int>("0", 0) && reads<int>("-0", 0) && reads<int>("007", 7) && reads<int>("-42", -42));
static_assert(reads<std::uint32_t>("0x0", 0U) && reads<std::uint32_t>("0xdeadBEEF", 0xDEADBEEFU));
static_assert(reads<int>("-0x1f", -31) && reads<std::uint64_t>("000000000000000000000000000001", 1U));

// The limits of each width.
static_assert(reads<std::int8_t>("127", 127) && reads<std::int8_t>("-128", -128));
static_assert(reads<std::int8_t>("0x7f", 127) && reads<std::int8_t>("-0x80", -128));
static_assert(reads<std::uint8_t>("255", 255U) && reads<std::uint8_t>("0xFF", 255U));
static_assert(reads<std::int16_t>("32767", 32767) && reads<std::int16_t>("-32768", -32768));
static_assert(reads<std::uint16_t>("65535", 65535U));
static_assert(reads<std::int32_t>("2147483647", max_of<std::int32_t>));
static_assert(reads<std::int32_t>("-2147483648", min_of<std::int32_t>));
static_assert(reads<std::uint32_t>("4294967295", max_of<std::uint32_t>));
static_assert(reads<std::int64_t>("9223372036854775807", max_of<std::int64_t>));
static_assert(reads<std::int64_t>("-9223372036854775808", min_of<std::int64_t>));
static_assert(reads<std::int64_t>("-0x8000000000000000", min_of<std::int64_t>));
static_assert(reads<std::uint64_t>("18446744073709551615", max_of<std::uint64_t>));
static_assert(reads<std::uint64_t>("0xFFFFFFFFFFFFFFFF", max_of<std::uint64_t>));

// One past each limit: the overflow is at the first digit that leaves the
// range.
static_assert(refuses<std::int8_t>("128", ParseErrorKind::overflow, 2));
static_assert(refuses<std::int8_t>("-129", ParseErrorKind::overflow, 3));
static_assert(refuses<std::int8_t>("1280", ParseErrorKind::overflow, 2));
static_assert(refuses<std::int8_t>("0xFF", ParseErrorKind::overflow, 3));
static_assert(refuses<std::int8_t>("-0x81", ParseErrorKind::overflow, 4));
static_assert(refuses<std::uint8_t>("256", ParseErrorKind::overflow, 2));
static_assert(refuses<std::uint8_t>("0x100", ParseErrorKind::overflow, 4));
static_assert(refuses<std::int64_t>("9223372036854775808", ParseErrorKind::overflow, 18));
static_assert(refuses<std::int64_t>("-9223372036854775809", ParseErrorKind::overflow, 19));
static_assert(refuses<std::uint64_t>("18446744073709551616", ParseErrorKind::overflow, 19));
static_assert(refuses<std::uint64_t>("0x10000000000000000", ParseErrorKind::overflow, 18));

// The overflow comes before a later character that is no digit.
static_assert(refuses<std::uint8_t>("999x", ParseErrorKind::overflow, 2));

// A text with no digit.
static_assert(refuses<int>("", ParseErrorKind::no_digit, 0) && refuses<int>(TextView{}, ParseErrorKind::no_digit, 0));
static_assert(refuses<int>("-", ParseErrorKind::no_digit, 1) && refuses<int>("0x", ParseErrorKind::no_digit, 2));
static_assert(refuses<int>("-0x", ParseErrorKind::no_digit, 3) && refuses<unsigned>("", ParseErrorKind::no_digit, 0));

// A character that is no digit, at its position.
static_assert(refuses<int>("0xg", ParseErrorKind::unexpected_character, 2));
static_assert(refuses<int>("0X1", ParseErrorKind::unexpected_character, 1));
static_assert(refuses<int>("00x1", ParseErrorKind::unexpected_character, 2));
static_assert(refuses<int>("+5", ParseErrorKind::unexpected_character, 0));
static_assert(refuses<int>("--5", ParseErrorKind::unexpected_character, 1));
static_assert(refuses<int>(" 5", ParseErrorKind::unexpected_character, 0));
static_assert(refuses<int>("5 ", ParseErrorKind::unexpected_character, 1));
static_assert(refuses<int>("12a", ParseErrorKind::unexpected_character, 2));
static_assert(refuses<int>("0x1g", ParseErrorKind::unexpected_character, 3));
static_assert(refuses<int>("1\x80", ParseErrorKind::unexpected_character, 1));
static_assert(refuses<int>("\xC0", ParseErrorKind::unexpected_character, 0));

// A sign on an unsigned type, also before a zero and before a character
// that is no digit.
static_assert(refuses<unsigned>("-0", ParseErrorKind::sign_on_unsigned, 0));
static_assert(refuses<std::uint8_t>("-5", ParseErrorKind::sign_on_unsigned, 0));
static_assert(refuses<std::uint64_t>("--", ParseErrorKind::sign_on_unsigned, 0));
static_assert(refuses<unsigned>("-", ParseErrorKind::sign_on_unsigned, 0));

// The digits that never leave the range: 99 fits std::int8_t and 999 does
// not, and 15 hexadecimal digits fit std::uint64_t.
static_assert(safe_digits_<std::int8_t, 10U>() == 2 && safe_digits_<std::uint8_t, 10U>() == 2);
static_assert(safe_digits_<std::int16_t, 10U>() == 4 && safe_digits_<std::int32_t, 10U>() == 9);
static_assert(safe_digits_<std::uint32_t, 10U>() == 9 && safe_digits_<std::int64_t, 10U>() == 18);
static_assert(safe_digits_<std::uint64_t, 10U>() == 19 && safe_digits_<std::uint64_t, 16U>() == 15);
static_assert(safe_digits_<std::int64_t, 16U>() == 15 && safe_digits_<std::int8_t, 16U>() == 1);

// The digits of each base.  A to F with the bit 0x20 are a to f, and the
// bytes next to each range are no digit.
static_assert(digit_value_<10U>('0') == 0U && digit_value_<10U>('9') == 9U && digit_value_<10U>('a') == 10U);
static_assert(digit_value_<10U>('/') == 10U && digit_value_<10U>(':') == 10U);
static_assert(digit_value_<16U>('a') == 10U && digit_value_<16U>('F') == 15U && digit_value_<16U>('g') == 16U);
static_assert(digit_value_<16U>('`') == 16U && digit_value_<16U>('@') == 16U && digit_value_<16U>('G') == 16U);
static_assert(digit_value_<16U>('\xE1') == 16U && digit_value_<16U>('\xC1') == 16U);

}  // namespace foundation::core::detail::parse_checks
