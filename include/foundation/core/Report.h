#pragma once

// The Report family: the report to a sink, and the fatal exit with
// arguments.
//
//   report(sink, fmt, args...)
//                         writes the text with the arguments to a sink:
//                         Sink::Out (standard output) or Sink::Err
//                         (standard error)
//   fatal(fmt, args...)   writes the text with the arguments and the site
//                         to standard error, and ends the process
//
// foundation/core/Fatal.h gives Site, Fmt<Args...>, the fatal() with no
// argument and unreachable(), with the rules of the text of a report.  It
// names no text type, so a family below foundation/core/Text.h can end the
// process.  This header includes it.  format() of foundation/core/Format.h
// writes a text into a FixedText.
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
// control does not continue after them.  The fatal() with arguments is
// itself a cold function (THE TWO FORMS below).  So the hot path keeps only
// the branch to the cold call.
//
// The cold functions are in src/foundation/core/Report.cpp and
// src/foundation/ContractHandler.cpp.  A report in progress on one thread
// makes a second fatal report on that thread abort at once.

#include <foundation/core/Fatal.h>
#include <foundation/core/Number.h>
#include <foundation/core/Text.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace foundation::core {

// A type that a report can write: an Integer and a Float of the Scalar
// family, a bool and a TextView.  An Integer is not bool and not a
// character type, and signed char and unsigned char are integers, so a
// std::uint8_t is written as a number.  The argument of a report binds to
// a reference, so an argument type is never a reference.  A deduced
// argument type is never cv-qualified, so each arm refuses a cv-qualified
// type.  Each function that writes the arguments of a Fmt asks for this
// concept of each one.
template <class T>
concept Formattable = Integer<T> || Float<T> || std::is_same_v<T, bool> || std::is_same_v<T, TextView>;

// The streams that a report can write to.
enum class Sink : std::uint8_t {
    Out,
    Err
};

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

// Writes the text with count arguments and the site to standard error, as
// report_fatal_ of foundation/core/Fatal.h does for a text with no argument.
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
//   The fatal() with no argument is inline (foundation/core/Fatal.h).  It
//   loads the parts of its text into registers and calls one cold function,
//   and the caller builds no object on its stack.
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
    requires(Formattable<First> && (Formattable<Rest> && ...))
[[gnu::cold, gnu::noinline]] void report(Sink sink, Fmt<std::type_identity_t<First>, std::type_identity_t<Rest>...> fmt,
                                         First first, Rest... rest) noexcept {
    detail::FmtArg const arguments[1 + sizeof...(Rest)] = {detail::fmt_arg_of_(first), detail::fmt_arg_of_(rest)...};
    detail::report_(sink, detail::fmt_text_(fmt), arguments, 1 + sizeof...(Rest));
}

// Writes the text with the arguments and the site to standard error, and
// ends the process.  A search for `fatal(` finds each fatal exit of the
// code.
template <class First, class... Rest>
    requires(Formattable<First> && (Formattable<Rest> && ...))
[[noreturn, gnu::cold, gnu::noinline]] void
fatal(Fmt<std::type_identity_t<First>, std::type_identity_t<Rest>...> const& fmt, First first, Rest... rest) noexcept {
    detail::FmtArg const arguments[1 + sizeof...(Rest)] = {detail::fmt_arg_of_(first), detail::fmt_arg_of_(rest)...};
    detail::report_fatal_with_(detail::fmt_text_(fmt), detail::fmt_file_(fmt), detail::fmt_line_(fmt),
                               detail::fmt_function_(fmt), arguments, 1 + sizeof...(Rest));
}

}  // namespace foundation::core
