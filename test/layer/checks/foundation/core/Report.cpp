// The compile-time checks of foundation/core/Report.h.

#include <foundation/core/Report.h>

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

// A text takes an integer, a bool or a TextView.  A character, a floating
// value, a pointer, an array, an enumeration and each other type has no
// formatter, so the text refuses it.
enum class Colour : unsigned char {
    red
};
template <class... Args>
concept TakesArguments = requires { typename Fmt<Args...>; };
static_assert(TakesArguments<>);
static_assert(TakesArguments<int, unsigned long long, signed char, unsigned char, short, long>);
static_assert(TakesArguments<bool, TextView>);
static_assert(!TakesArguments<char[4]>);
static_assert(!TakesArguments<char>);
static_assert(!TakesArguments<wchar_t>);
static_assert(!TakesArguments<char8_t>);
static_assert(!TakesArguments<double>);
static_assert(!TakesArguments<float>);
static_assert(!TakesArguments<char const*>);
static_assert(!TakesArguments<int*>);
static_assert(!TakesArguments<decltype(nullptr)>);
static_assert(!TakesArguments<Colour>);
static_assert(!TakesArguments<char8_t[4]>);
static_assert(!TakesArguments<int, double>);

// Each argument reaches the cold formatter as its kind and its bits.  A
// signed value keeps its two's complement bits.
static_assert(fmt_arg_of_(-1).kind == FmtArgKind::signed_integer && fmt_arg_of_(-1).bits == ~0ULL);
static_assert(fmt_arg_of_(7U).kind == FmtArgKind::unsigned_integer && fmt_arg_of_(7U).bits == 7U);
static_assert(fmt_arg_of_(true).kind == FmtArgKind::boolean && fmt_arg_of_(true).bits == 1U);
static_assert(fmt_arg_of_(TextView{"abcd"}).kind == FmtArgKind::text && fmt_arg_of_(TextView{"abcd"}).size == 4);

// fatal and unreachable never return, and no call of the family throws.
static_assert(std::is_same_v<decltype(fatal(Fmt<>{"text"})), void>);
static_assert(std::is_same_v<decltype(fatal(Fmt<int>{"text {}"}, 3)), void>);
static_assert(noexcept(fatal(Fmt<int>{"text {}"}, 3)));
static_assert(noexcept(report(Sink::Out, Fmt<int>{"text {}"}, 3)));
static_assert(noexcept(unreachable()));

}  // namespace detail::report_checks

}  // namespace foundation::core
