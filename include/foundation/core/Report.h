#pragma once

// The Report family: the place of a call, the checked text of a report, the
// report to a sink, and the fatal exit.
//
//   Site                  the file, the line and the function of a call
//   Fmt<Args...>          a string literal with one placeholder for each
//                         argument, and the Site of its call
//   report(sink, fmt, args...)
//                         writes the text with the arguments to a sink:
//                         Sink::Out (standard output) or Sink::Err
//                         (standard error)
//   fatal(fmt, args...)   writes the text with the arguments and the site
//                         to standard error, and ends the process
//   unreachable()         the same, for a point that the code marks as one
//                         that control never reaches
//
// format() of foundation/core/Format.h writes a text into a FixedText.
//
// THE TEXT OF A REPORT
//   `{}` is a placeholder, and each placeholder takes the next argument.
//   `{{` writes `{`, and `}}` writes `}`.  Each other brace is refused.  The
//   constructor of Fmt is consteval: it refuses a text with no terminating
//   zero, a zero byte before the end, a stray brace, and a count of
//   placeholders that differs from the count of arguments.  The refusal
//   names its rule (detail::fmt_*), and the build stops at the call.
//
// THE ARGUMENTS
//   An argument is an integer, a bool, a TextView, a float or a double.  An
//   integer is written in decimal, and a bool as true or false.  A TextView
//   is written as its characters, so a literal argument is spelled
//   TextView{"text"}.  A character type, a long double, each other floating
//   type, a pointer, an array and each other type is refused at the call
//   (the concept Formattable).  A long double has 80 value bits on x86_64
//   and 128 on aarch64, so one value would have two texts.
//
// THE TEXT OF A FLOAT
//   A float and a double are written as the text of std::to_chars(first,
//   last, value) with no format, which std::format("{}") also writes:
//     - The shortest digits that read back to the value with round to
//       nearest, ties to even.  Of two such digit strings, the nearer one,
//       and on an equal distance the one with the even last digit
//     - The plain form (as %f) or the exponent form (as %e), whichever has
//       fewer characters, and the plain form on a tie.  A plain form with no
//       fraction holds the exact integer value of the float
//     - The exponent form writes e+XX or e-XX, with at least two digits
//     - A negative value and -0 take a minus sign: inf, -inf, nan and -nan.
//   A double takes at most 24 characters, and a float at most 15.  The
//   formatter does integer arithmetic on the bits of the value only, so the
//   rounding mode and the floating environment do not change the text.
//
// THE OUTPUT
//   The formatter uses no locale, no allocation and no global state, so one
//   text with the same arguments gives the same bytes in each build.
//   report(), fatal() and unreachable() write through the sink write of
//   src/foundation/os/ReportSink.cpp, which issues the raw write of
//   foundation/os/Syscall.h with no stdio buffer, so the text of a report is
//   written before the call returns.  A call from a signal handler is safe.  A text that fits the buffer of 512
//   bytes is written with one system call.
//
// fatal() and unreachable() end the process in each build, with each
// contract semantic.  They are [[noreturn]], so the optimizer knows that
// control does not continue after them.  unreachable() and the fatal()
// with no argument are inline functions that load the texts and the line
// into registers and call one cold function.  The fatal() with arguments is
// itself a cold function (THE TWO FORMS below).  So the hot path keeps only
// the branch to the cold call.
//
// The cold functions are in src/foundation/core/Report.cpp and
// src/foundation/ContractHandler.cpp.  A report in progress on one thread
// makes a second fatal report on that thread abort at once.

#include <foundation/core/Text.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

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

// A character type.  A concept and not a variable template, so no
// translation unit can specialize it and admit a character as an integer.
template <class T>
concept CharacterType = std::is_same_v<T, char> || std::is_same_v<T, wchar_t> || std::is_same_v<T, char8_t>
                     || std::is_same_v<T, char16_t> || std::is_same_v<T, char32_t>;

}  // namespace detail

// An integer argument of a report: an integral type that is not
// cv-qualified, not bool and not a character type.  signed char and
// unsigned char are integers, so a std::uint8_t is written as a number.
template <class T>
concept FmtInteger = std::is_integral_v<T> && std::is_same_v<T, std::remove_cv_t<T>> && !std::is_same_v<T, bool>
                  && !detail::CharacterType<T>;

// A type that a report can write.  The argument of a report binds to a
// reference, so an argument type is never a reference.  A deduced argument
// type is never cv-qualified, so each arm refuses a cv-qualified type.
template <class T>
concept Formattable = FmtInteger<T> || std::is_same_v<T, bool> || std::is_same_v<T, TextView>
                   || std::is_same_v<T, float> || std::is_same_v<T, double>;

// The streams that a report can write to.
enum class Sink : std::uint8_t {
    Out,
    Err
};

class Site;

template <class... Args>
    requires(Formattable<Args> && ...)
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
// with the checks of those Args.
template <class... Args>
    requires(Formattable<Args> && ...)
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

}  // namespace detail

namespace detail {

// The kind of a value in the argument array of a report.
enum class FmtArgKind : std::uint8_t {
    signed_integer,
    unsigned_integer,
    boolean,
    text,
    binary32,
    binary64
};

// One argument of a report, as the cold formatter reads it.  An integer, a
// bool and a floating value are in bits: a signed integer as the bits of
// its std::int64_t value, and a float or a double as its IEEE bits.  A text
// is in first and size.
struct FmtArg {
    FmtArgKind kind = FmtArgKind::unsigned_integer;
    std::uint64_t bits = 0;
    char const* first = nullptr;
    std::size_t size = 0;
};

template <class T>
    requires Formattable<T>
[[nodiscard, gnu::always_inline]] constexpr FmtArg fmt_arg_of_(T const& value) noexcept {
    if constexpr (std::is_same_v<T, bool>) {
        return FmtArg{FmtArgKind::boolean, value ? 1U : 0U, nullptr, 0};
    } else if constexpr (std::is_same_v<T, TextView>) {
        return FmtArg{FmtArgKind::text, 0, text_first_(value), value.size()};
    } else if constexpr (std::is_same_v<T, float>) {
        return FmtArg{FmtArgKind::binary32, __builtin_bit_cast(std::uint32_t, value), nullptr, 0};
    } else if constexpr (std::is_same_v<T, double>) {
        return FmtArg{FmtArgKind::binary64, __builtin_bit_cast(std::uint64_t, value), nullptr, 0};
    } else if constexpr (std::is_signed_v<T>) {
        return FmtArg{FmtArgKind::signed_integer, static_cast<std::uint64_t>(static_cast<std::int64_t>(value)), nullptr,
                      0};
    } else {
        return FmtArg{FmtArgKind::unsigned_integer, static_cast<std::uint64_t>(value), nullptr, 0};
    }
}

// Writes `foundation: fatal: TEXT` and the site to standard error.  Then it
// stops in a debugger when one is attached, and aborts.  The text takes no
// argument, and its escapes `{{` and `}}` are written as one brace.
[[noreturn, gnu::cold]] void report_fatal_(char const* text, char const* file, std::uint32_t line,
                                           char const* function) noexcept;

// The same, for a text with count arguments.
[[noreturn, gnu::cold]] void report_fatal_with_(char const* text, char const* file, std::uint32_t line,
                                                char const* function, FmtArg const* arguments,
                                                std::size_t count) noexcept;

// Writes the text with count arguments to the sink.
[[gnu::cold]] void report_(Sink sink, char const* text, FmtArg const* arguments, std::size_t count) noexcept;

// Writes the bytes from skip to skip + capacity of the formatted text into
// out, and returns the length of the whole formatted text.  A text longer
// than skip + capacity loses no byte: a call with a larger skip gives the
// next bytes.  Complexity: O(the length of the formatted text).
[[nodiscard]] std::size_t format_window_(char* out, std::size_t capacity, std::size_t skip, char const* text,
                                         FmtArg const* arguments, std::size_t count) noexcept;

// The sink write: writes count bytes to the stream of the sink.  It writes
// again after a short write and after an interrupted call, and it stops at
// each other error, because a report has no road for the error.
void write_sink_(Sink sink, char const* bytes, std::size_t count) noexcept;

}  // namespace detail

// THE FORMS OF report() AND fatal()
//   The fatal() with no argument is inline.  It loads the parts of its text
//   into registers and calls one cold function, and the caller builds no
//   object on its stack.
//
//   Each other form is a cold function that is not inline.  report() takes
//   its text by value: the caller writes the text into the argument area
//   of the call and keeps no object in its frame.  A Fmt in the frame of
//   the main of a test made GCC inline six test functions into it, under
//   the limit of large-stack-frame-growth, and the Debug build of the test
//   took 21.5 G instructions in place of 18.3 G.  The fatal() with
//   arguments takes its text by const reference, because the assert of the
//   tests gives it a text in read-only data.  Each argument is a small
//   value, so it comes by value in a register.  An inline expansion of a
//   form with arguments at each call costs the compiler more than the
//   call: a Debug build of a test with 63 asserts measured 19.5 G
//   instructions inline and 18.9 G with the cold form, against 17.7 G
//   before the formatter.

// Writes the text to the sink.  The text is written with one system call
// when it fits 512 bytes.  A search for `report(` finds each report of the
// code.
[[gnu::cold]] void report(Sink sink, Fmt<> fmt) noexcept;

// The same, with arguments.
template <class First, class... Rest>
[[gnu::cold, gnu::noinline]] void report(Sink sink, Fmt<std::type_identity_t<First>, std::type_identity_t<Rest>...> fmt,
                                         First first, Rest... rest) noexcept {
    detail::FmtArg const arguments[1 + sizeof...(Rest)] = {detail::fmt_arg_of_(first), detail::fmt_arg_of_(rest)...};
    detail::report_(sink, detail::fmt_text_(fmt), arguments, 1 + sizeof...(Rest));
}

// Writes the text and the site to standard error, and ends the process.  A
// search for `fatal(` finds each fatal exit of the code.
[[noreturn, gnu::always_inline]] inline void fatal(Fmt<> fmt) noexcept {
    detail::report_fatal_(detail::fmt_text_(fmt), detail::fmt_file_(fmt), detail::fmt_line_(fmt),
                          detail::fmt_function_(fmt));
}

// The same, with arguments.
template <class First, class... Rest>
[[noreturn, gnu::cold, gnu::noinline]] void
fatal(Fmt<std::type_identity_t<First>, std::type_identity_t<Rest>...> const& fmt, First first, Rest... rest) noexcept {
    detail::FmtArg const arguments[1 + sizeof...(Rest)] = {detail::fmt_arg_of_(first), detail::fmt_arg_of_(rest)...};
    detail::report_fatal_with_(detail::fmt_text_(fmt), detail::fmt_file_(fmt), detail::fmt_line_(fmt),
                               detail::fmt_function_(fmt), arguments, 1 + sizeof...(Rest));
}

// The point that control never reaches, such as the arm after a switch
// that handles each enumerator.  A value outside the enumerators reaches
// it, and the process ends with the site.
[[noreturn, gnu::always_inline]] inline void unreachable(Site site = Site{}) noexcept {
    detail::report_fatal_("control reached a point that the code marks as unreachable", detail::site_file_(site),
                          detail::site_line_(site), detail::site_function_(site));
}

}  // namespace foundation::core
