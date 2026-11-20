// The compile-time checks of foundation/core/Report.h.

#include <foundation/core/Report.h>

#include <foundation/core/Format.h>
#include <foundation/core/Parse.h>
#include <foundation/core/Region.h>
#include <foundation/core/Verbs.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace foundation::core {

namespace detail::report_checks {

// A site is two texts and a line, and a text of a report adds one more
// text, so each one goes to a cold call in registers.  The arguments add no
// byte to the text.
static_assert(sizeof(Site) == 3 * sizeof(void*));
static_assert(sizeof(Fmt<>) == 4 * sizeof(void*));
static_assert(sizeof(Fmt<int, TextView>) == sizeof(Fmt<>));
static_assert(std::is_trivially_copyable_v<Site> && std::is_trivially_copyable_v<Fmt<>>);

// A Site and a Fmt come only from a constant evaluation.  A text at run
// time does not convert, so each text of a report is a literal.
static_assert(!std::is_convertible_v<char const*, Fmt<>>);
static_assert(std::is_convertible_v<char const (&)[6], Fmt<>>);

// The parts of a Fmt are checked for its count of arguments.  No Fmt comes
// from the parts of a different Fmt, and no assignment writes the parts of
// a Fmt, also through its base.
static_assert(!std::is_constructible_v<Fmt<int>, FmtParts const&>);
static_assert(!std::is_constructible_v<Fmt<int>, Fmt<int, int> const&>);
static_assert(!std::is_copy_assignable_v<FmtParts> && !std::is_copy_assignable_v<Fmt<>>);
static_assert(!std::is_move_assignable_v<FmtParts> && !std::is_move_assignable_v<Fmt<int>>);
static_assert(std::is_base_of_v<FmtParts, Fmt<int, TextView>>);

// A text has a placeholder `{}` for each argument, and `{{` and `}}` for a
// brace.  It holds no other brace, no zero byte and at least one character.
// Each refused text fails the consteval constructor, so the build stops.
// The probe builds one text of a list, so a refused text makes the probe no
// constant expression.
[[nodiscard]] consteval bool forms_text(int which) noexcept {
    switch (which) {
        case 0:
            static_cast<void>(Fmt<>{"the queue is full"});
            return true;
        case 1:
            static_cast<void>(Fmt<>{"the queue holds {} items"});
            return true;
        case 2:
            static_cast<void>(Fmt<>{"the queue is full }"});
            return true;
        case 3:
            static_cast<void>(Fmt<>{"the queue\0 is full"});
            return true;
        case 4:
            static_cast<void>(Fmt<>{""});
            return true;
        case 5:
            static_cast<void>(Fmt<int>{"the queue holds {} items"});
            return true;
        case 6:
            static_cast<void>(Fmt<>{"a {{brace}} pair"});
            return true;
        case 7:
            static_cast<void>(Fmt<int, bool>{"only {} of two"});
            return true;
        case 8:
            static_cast<void>(Fmt<int>{"a {x} spec"});
            return true;
        case 9:
            static_cast<void>(Fmt<int>{"{}{}"});
            return true;
        case 10:
            static_cast<void>(Fmt<int, TextView, bool>{"{}={}:{}"});
            return true;
        case 11:
            static_cast<void>(Fmt<>{"a { brace"});
            return true;
        case 12:
            static_cast<void>(Fmt<int>{"{{}}"});
            return true;
        default:
            return false;
    }
}
template <int Which>
concept FormsText = requires { typename std::integral_constant<bool, forms_text(Which)>; };
static_assert(FormsText<0> && FormsText<5> && FormsText<6> && FormsText<10>);
static_assert(!FormsText<1>);
static_assert(!FormsText<2>);
static_assert(!FormsText<3>);
static_assert(!FormsText<4>);
static_assert(!FormsText<7>);
static_assert(!FormsText<8>);
static_assert(!FormsText<9>);
static_assert(!FormsText<11>);
static_assert(!FormsText<12>);

// A site walks each name to its terminating zero, so a null name and a
// name with no zero stop the build.  The two letters are one character
// each with no zero after the first.
struct TwoLetters {
    char first = 'a';
    char second = 'b';
};
inline constexpr TwoLetters two_letters{};
[[nodiscard]] consteval bool builds_site(int which) noexcept {
    switch (which) {
        case 0:
            static_cast<void>(Site{});
            return true;
        case 1:
            static_cast<void>(Site{"file.cpp", 3, "function"});
            return true;
        case 2:
            static_cast<void>(Site{nullptr, 3, "function"});
            return true;
        case 3:
            static_cast<void>(Site{"file.cpp", 3, &two_letters.first});
            return true;
        default:
            return false;
    }
}
template <int Which>
concept BuildsSite = requires { typename std::integral_constant<bool, builds_site(Which)>; };
static_assert(BuildsSite<0> && BuildsSite<1>);
static_assert(!BuildsSite<2>);
static_assert(!BuildsSite<3>);

// A report takes an integer, a bool, a TextView, a float or a double.  A
// character, a long double, each other floating type, a pointer, an array,
// an enumeration and each other type has no formatter, so Formattable
// refuses it.  Each function that writes the arguments of a text asks for
// Formattable of each one: report(), fatal() with arguments and format().
// The fixtures neg_core_fmt_argument_* and neg_core_format_argument_* show
// the refusal of each function.  A Fmt of such a type compiles, because
// foundation/core/Fatal.h names no text type, and no function writes it.
enum class Colour : unsigned char {
    red
};
template <class... Args>
concept TakesArguments = (Formattable<Args> && ...);
static_assert(TakesArguments<>);
static_assert(TakesArguments<int, unsigned long long, signed char, unsigned char, short, long>);
static_assert(TakesArguments<bool, TextView>);
static_assert(TakesArguments<float, double>);
static_assert(!TakesArguments<char[4]>);
static_assert(!TakesArguments<char>);
static_assert(!TakesArguments<wchar_t>);
static_assert(!TakesArguments<char8_t>);
static_assert(!TakesArguments<long double>);
static_assert(!TakesArguments<_Float16>);
static_assert(!TakesArguments<_Float32>);
static_assert(!TakesArguments<_Float64>);
static_assert(!TakesArguments<__bf16>);
static_assert(!TakesArguments<char const*>);
static_assert(!TakesArguments<int*>);
static_assert(!TakesArguments<decltype(nullptr)>);
static_assert(!TakesArguments<Colour>);
static_assert(!TakesArguments<char8_t[4]>);
static_assert(!TakesArguments<int, long double>);
// A deduced argument type is never cv-qualified, so the gate refuses each
// cv-qualified type, an integer as a bool.
static_assert(!TakesArguments<int const> && !TakesArguments<int volatile> && !TakesArguments<unsigned const>);
static_assert(!TakesArguments<bool const> && !TakesArguments<double const> && !TakesArguments<TextView const>);
// A Fmt of a refused type is a type of the same size, and no function writes
// its arguments.
static_assert(sizeof(Fmt<long double>) == sizeof(Fmt<>));

// A Hex of each width and a HexBytes are arguments, and a Hex is the value
// of one word.  A cv-qualified Hex is refused as each other cv-qualified
// type, and no other class passes for a Hex: the concept asks for the Hex of
// the width of the type.
struct LooksLikeHex {
    static constexpr std::size_t width = 8;
};
struct LooksLikeHexBytes {
    using report_mark = LooksLikeHexBytes;
};
static_assert(TakesArguments<Hex<2>, Hex<3>, Hex<8>, Hex<16>, HexBytes>);
static_assert(!TakesArguments<Hex<8> const> && !TakesArguments<HexBytes const> && !TakesArguments<LooksLikeHex>);
static_assert(!TakesArguments<LooksLikeHexBytes>);
static_assert(sizeof(Hex<16>) == sizeof(std::uint64_t) && std::is_trivially_copyable_v<Hex<16>>);

// The width holds each value of the type, so no digit is cut.  A value with
// more digits than the width, a signed value, a bool and a character are
// refused, and no Hex converts from a number with no call.
static_assert(std::is_constructible_v<Hex<2>, unsigned char> && std::is_constructible_v<Hex<16>, unsigned char>);
static_assert(std::is_constructible_v<Hex<8>, std::uint32_t> && std::is_constructible_v<Hex<16>, std::uint64_t>);
static_assert(!std::is_constructible_v<Hex<8>, std::uint64_t> && !std::is_constructible_v<Hex<3>, std::uint16_t>);
static_assert(!std::is_constructible_v<Hex<16>, std::int64_t> && !std::is_constructible_v<Hex<16>, int>);
static_assert(!std::is_constructible_v<Hex<16>, bool> && !std::is_constructible_v<Hex<16>, char>);
static_assert(!std::is_constructible_v<Hex<16>, double> && !std::is_convertible_v<std::uint64_t, Hex<16>>);
static_assert(!std::is_convertible_v<View<std::byte const>, HexBytes>);

// Each Hex reaches the cold formatter as its value and its width.
static_assert(fmt_arg_of_(Hex<16>{0xABCDU}).kind == FmtArgKind::hex_digits);
static_assert(fmt_arg_of_(Hex<16>{0xABCDU}).bits == 0xABCDU && fmt_arg_of_(Hex<16>{0xABCDU}).size == 16);
static_assert(fmt_arg_of_(Hex<2>{static_cast<unsigned char>(0xFFU)}).size == 2);

// Each argument reaches the cold formatter as its kind and its bits.  A
// signed value keeps its two's complement bits.
static_assert(fmt_arg_of_(-1).kind == FmtArgKind::signed_integer && fmt_arg_of_(-1).bits == ~0ULL);
static_assert(fmt_arg_of_(7U).kind == FmtArgKind::unsigned_integer && fmt_arg_of_(7U).bits == 7U);
static_assert(fmt_arg_of_(true).kind == FmtArgKind::boolean && fmt_arg_of_(true).bits == 1U);
static_assert(fmt_arg_of_(TextView{"abcd"}).kind == FmtArgKind::text && fmt_arg_of_(TextView{"abcd"}).size == 4);
static_assert(fmt_arg_of_(1.0f).kind == FmtArgKind::binary32 && fmt_arg_of_(1.0f).bits == 0x3F800000U);
static_assert(fmt_arg_of_(-2.0).kind == FmtArgKind::binary64 && fmt_arg_of_(-2.0).bits == 0xC000000000000000ULL);

// fatal and unreachable never return, and no call of the family throws.
static_assert(std::is_same_v<decltype(fatal(Fmt<>{"text"})), void>);
static_assert(std::is_same_v<decltype(fatal(Fmt<int>{"text {}"}, 3)), void>);
static_assert(noexcept(fatal(Fmt<int>{"text {}"}, 3)));
static_assert(noexcept(report(Sink::Out, Fmt<int>{"text {}"}, 3)));
static_assert(noexcept(unreachable()));

// Each public member of a type of the family is a shared verb, a language
// protocol or a name of the family list of plan section 11.8: each name of
// the API table, and the enumeration and the two members kind and position
// of ParseError, the classes Hex and HexBytes, the width of a Hex, and the
// mark of a HexBytes.
inline constexpr TextView report_names{"Site Fmt report Sink fatal unreachable format Formattable parse ParseError "
                                       "ParseErrorKind kind position Hex HexBytes width report_mark"};
CRUCIBLE_CLOSED_SURFACE(report_names, Site);
CRUCIBLE_CLOSED_SURFACE(report_names, Hex<8>);
CRUCIBLE_CLOSED_SURFACE(report_names, HexBytes);
CRUCIBLE_CLOSED_SURFACE(report_names, Fmt<>);
CRUCIBLE_CLOSED_SURFACE(report_names, Fmt<int, TextView, bool>);
CRUCIBLE_CLOSED_SURFACE(report_names, Fmt<float, double>);
CRUCIBLE_CLOSED_SURFACE(report_names, ParseError);

}  // namespace detail::report_checks

}  // namespace foundation::core
