#pragma once

// The fatal exit of the Report family: the place of a call, the checked text
// of a report, and the exits that take no argument.
//
//   Site                  the file, the line and the function of a call
//   Fmt<Args...>          a string literal with one placeholder for each
//                         argument, and the Site of its call
//   fatal(fmt)            writes the text and the site to standard error,
//                         and ends the process
//   unreachable()         the same, for a point that the code marks as one
//                         that control never reaches
//
// This header names no text type, so the Choice family and the cursor of
// foundation/core/Cursor.h can end the process below foundation/core/Text.h.
// foundation/core/Report.h gives the reports and the fatal exits that take
// arguments, and the concept Formattable that refuses each other argument
// type at the call.
//
// THE TEXT OF A REPORT
//   `{}` is a placeholder, and each placeholder takes the next argument.
//   `{{` writes `{`, and `}}` writes `}`.  Each other brace is refused.  The
//   constructor of Fmt is consteval: it refuses a text with no terminating
//   zero, a zero byte before the end, a stray brace, and a count of
//   placeholders that differs from the count of arguments.  The refusal
//   names its rule (detail::fmt_*), and the build stops at the call.
//
// fatal() and unreachable() end the process in each build, with each
// contract semantic.  They are [[noreturn]], so the optimizer knows that
// control does not continue after them.  They are inline functions that load
// the texts and the line into registers and call one cold function, so the
// hot path keeps only the branch to the cold call.  The cold function is in
// src/foundation/core/Report.cpp.

#include <cstddef>
#include <cstdint>

namespace foundation::core {

namespace detail {

// The consteval constructor of Fmt calls one of these functions when its
// text breaks a rule.  No function has a definition, so the call is no
// constant expression, and the error of the compiler names the rule.
void fmt_text_is_empty() noexcept;
void fmt_text_has_no_terminating_zero() noexcept;
void fmt_text_holds_a_zero_byte_before_its_end() noexcept;
void fmt_text_holds_a_brace_that_is_no_placeholder_and_no_escape() noexcept;
void fmt_text_has_more_placeholders_than_arguments() noexcept;
void fmt_text_has_fewer_placeholders_than_arguments() noexcept;

// Checks the text of a Fmt: a string literal of length bytes, with its
// terminating zero, and with one placeholder for each of the arguments.
// It is a function and not a part of the constructor template, so the
// check compiles one time for each translation unit and not one time for
// each length of a literal.
consteval void check_fmt_text_(char const* text, std::size_t length, std::size_t arguments) noexcept {
    if (length < 2) fmt_text_is_empty();
    if (text[length - 1] != '\0') fmt_text_has_no_terminating_zero();
    std::size_t placeholders = 0;
    for (std::size_t index = 0; index + 1 < length; ++index) {
        char const byte = text[index];
        if (byte == '\0') {
            fmt_text_holds_a_zero_byte_before_its_end();
        } else if (byte == '{' && text[index + 1] == '}') {
            ++placeholders;
            ++index;
        } else if ((byte == '{' || byte == '}') && text[index + 1] == byte) {
            ++index;
        } else if (byte == '{' || byte == '}') {
            fmt_text_holds_a_brace_that_is_no_placeholder_and_no_escape();
        }
    }
    if (placeholders > arguments) fmt_text_has_more_placeholders_than_arguments();
    if (placeholders < arguments) fmt_text_has_fewer_placeholders_than_arguments();
}

}  // namespace detail

class Site;

template <class... Args>
class Fmt;

namespace detail {

// The doors of the functions of the family to the parts of a Site and of a
// Fmt.  Each part comes out by value, so no caller keeps a reference into a
// Site or a Fmt.  The constructors of Site and Fmt are consteval, so each
// text is a constant of static storage and cannot dangle.  The rule owners
// keeps each caller outside the base out of detail.
[[nodiscard]] constexpr char const* site_file_(Site const& site) noexcept;
[[nodiscard]] constexpr char const* site_function_(Site const& site) noexcept;
[[nodiscard]] constexpr std::uint32_t site_line_(Site const& site) noexcept;

template <class... Args>
[[nodiscard]] constexpr char const* fmt_text_(Fmt<Args...> const& fmt) noexcept;
template <class... Args>
[[nodiscard]] constexpr char const* fmt_file_(Fmt<Args...> const& fmt) noexcept;
template <class... Args>
[[nodiscard]] constexpr char const* fmt_function_(Fmt<Args...> const& fmt) noexcept;
template <class... Args>
[[nodiscard]] constexpr std::uint32_t fmt_line_(Fmt<Args...> const& fmt) noexcept;

}  // namespace detail

// The place of a call.  The default arguments read the place where the
// constructor runs, which is the call that takes a default Site.
class Site final {
public:
    consteval Site(char const* file = __builtin_FILE(), int line = __builtin_LINE(),
                   char const* function = __builtin_FUNCTION()) noexcept
        : file_{file}, function_{function}, line_{static_cast<std::uint32_t>(line)} {
        // The constructor walks to the terminating zero of each name.  A
        // read of a null name, or past the end of a constant array with no
        // zero, is no constant expression, so the build stops at the call.
        // The report then never reads past the end of a name.
        for (char const* cursor = file; *cursor != '\0'; ++cursor) {}
        for (char const* cursor = function; *cursor != '\0'; ++cursor) {}
    }

private:
    friend constexpr char const* detail::site_file_(Site const& site) noexcept;
    friend constexpr char const* detail::site_function_(Site const& site) noexcept;
    friend constexpr std::uint32_t detail::site_line_(Site const& site) noexcept;

    char const* file_;
    char const* function_;
    std::uint32_t line_;
};

// The text of a report and the place of the call that gives it.  The place
// comes from scalar default arguments and not from a default Site.  A
// default argument of class type is a temporary, and the range of a for
// loop extends the life of each temporary, so the temporary would be no
// constant there.
//
// A call takes the text as Fmt<std::type_identity_t<Args>...>, so the
// arguments of the call give Args, and the literal converts to the Fmt
// with the checks of those Args.  Each function of foundation/core/Report.h
// and foundation/core/Format.h that writes the arguments asks for
// Formattable of each one, so a Fmt of each other type writes nothing.
template <class... Args>
class Fmt final {
public:
    template <std::size_t Length>
    consteval Fmt(char const (&text)[Length], char const* file = __builtin_FILE(), int line = __builtin_LINE(),
                  char const* function = __builtin_FUNCTION()) noexcept
        : text_{text}, site_{file, line, function} {
        detail::check_fmt_text_(text, Length, sizeof...(Args));
    }

private:
    template <class... Others>
    friend constexpr char const* detail::fmt_text_(Fmt<Others...> const& fmt) noexcept;
    template <class... Others>
    friend constexpr char const* detail::fmt_file_(Fmt<Others...> const& fmt) noexcept;
    template <class... Others>
    friend constexpr char const* detail::fmt_function_(Fmt<Others...> const& fmt) noexcept;
    template <class... Others>
    friend constexpr std::uint32_t detail::fmt_line_(Fmt<Others...> const& fmt) noexcept;

    char const* text_;
    Site site_;
};

namespace detail {

constexpr char const* site_file_(Site const& site) noexcept { return site.file_; }
constexpr char const* site_function_(Site const& site) noexcept { return site.function_; }
constexpr std::uint32_t site_line_(Site const& site) noexcept { return site.line_; }

template <class... Args>
constexpr char const* fmt_text_(Fmt<Args...> const& fmt) noexcept {
    return fmt.text_;
}
template <class... Args>
constexpr char const* fmt_file_(Fmt<Args...> const& fmt) noexcept {
    return site_file_(fmt.site_);
}
template <class... Args>
constexpr char const* fmt_function_(Fmt<Args...> const& fmt) noexcept {
    return site_function_(fmt.site_);
}
template <class... Args>
constexpr std::uint32_t fmt_line_(Fmt<Args...> const& fmt) noexcept {
    return site_line_(fmt.site_);
}

// Writes `foundation: fatal: TEXT` and the site to standard error.  Then it
// stops in a debugger when one is attached, and aborts.  The text takes no
// argument, and its escapes `{{` and `}}` are written as one brace.
[[noreturn, gnu::cold]] void report_fatal_(char const* text, char const* file, std::uint32_t line,
                                           char const* function) noexcept;

}  // namespace detail

// Writes the text and the site to standard error, and ends the process.  A
// search for `fatal(` finds each fatal exit of the code.
[[noreturn, gnu::always_inline]] inline void fatal(Fmt<> fmt) noexcept {
    detail::report_fatal_(detail::fmt_text_(fmt), detail::fmt_file_(fmt), detail::fmt_line_(fmt),
                          detail::fmt_function_(fmt));
}

// The point that control never reaches, such as the arm after a switch
// that handles each enumerator.  A value outside the enumerators reaches
// it, and the process ends with the site.
[[noreturn, gnu::always_inline]] inline void unreachable(Site site = Site{}) noexcept {
    detail::report_fatal_("control reached a point that the code marks as unreachable", detail::site_file_(site),
                          detail::site_line_(site), detail::site_function_(site));
}

}  // namespace foundation::core
