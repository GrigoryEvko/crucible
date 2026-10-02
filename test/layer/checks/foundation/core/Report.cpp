// The compile-time checks of foundation/core/Report.h.

#include <foundation/core/Report.h>

#include <type_traits>

namespace foundation::core {

namespace detail::report_checks {

// A site is two texts and a line, and a text of a report adds one more
// text, so each one goes to a cold call in registers.
static_assert(sizeof(Site) == 3 * sizeof(void*));
static_assert(sizeof(Fmt<>) == 4 * sizeof(void*));
static_assert(std::is_trivially_copyable_v<Site> && std::is_trivially_copyable_v<Fmt<>>);

// A Site and a Fmt come only from a constant evaluation.  A text at run
// time does not convert, so each text of a report is a literal.
static_assert(!std::is_convertible_v<char const*, Fmt<>>);
static_assert(std::is_convertible_v<char const (&)[6], Fmt<>>);

// A text holds no brace, no zero byte and at least one character.  Each
// refused text fails the consteval constructor, so the build stops.  The
// probe builds one text of a list, so a refused text makes the probe no
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
        default:
            return false;
    }
}
template <int Which>
concept FormsText = requires { typename std::integral_constant<bool, forms_text(Which)>; };
static_assert(FormsText<0>);
static_assert(!FormsText<1>);
static_assert(!FormsText<2>);
static_assert(!FormsText<3>);
static_assert(!FormsText<4>);

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

// A text takes no argument at this time.
template <class... Args>
concept TakesArguments = requires { typename Fmt<Args...>; };
static_assert(TakesArguments<>);
static_assert(!TakesArguments<int>);

// fatal and unreachable never return.
static_assert(std::is_same_v<decltype(fatal(Fmt<>{"text"})), void>);
static_assert(noexcept(unreachable()));

}  // namespace detail::report_checks

}  // namespace foundation::core
