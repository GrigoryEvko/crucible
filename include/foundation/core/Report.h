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
//   An argument is an integer, a bool, a TextView, a float, a double, a
//   Hex, a HexBytes, a Fixed or a Padded.  An integer is written in decimal,
//   and a bool as true or false.  A TextView is written as its characters,
//   so a literal argument is spelled TextView{"text"}.  A character type, a
//   long double, each other floating type, a pointer, an array and each
//   other type is refused at the call (the concept Formattable).  A long
//   double has 80 value bits on x86_64 and 128 on aarch64, so one value
//   would have two texts.
//
// THE HEXADECIMAL TEXT
//   Hex<Width>{value} writes an unsigned integer as Width hexadecimal
//   digits: lower case, the most significant digit first, with the zeros
//   that lead, and with no prefix.  Width is 2 to 16, and it holds each
//   value of the type of the value, so no digit is ever cut: Hex<8> takes a
//   value of at most 32 bits, and Hex<16> each unsigned integer.  A signed
//   value is refused, because one set of bits has two texts.
//   HexBytes{bytes} writes each byte of a View of bytes as two such digits,
//   in the order of the View.  The digits come from put_hex_digits_ of
//   foundation/core/Text.h, the digit writer of append_hex, so a report and
//   append_hex write the same digits for one value.
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
// THE FIXED TEXT OF A FLOAT
//   Fixed<Decimals, Width, Side, Sign>{value} writes a double, or a float as
//   the double of the same value, with Decimals digits after the point: the
//   exact decimal value of the binary value, rounded to that count of
//   digits, ties to even.  That is the text of "%.Nf" of the printf of the C
//   library in the default rounding mode.  Decimals 0 writes no point.  A
//   value whose sign bit is set takes a minus sign, also -0 and a value that
//   rounds to zero.  SignMark::Always also writes a plus sign before each
//   other value, as the flag + of printf does.  An infinity is written inf
//   and a NaN nan, after the sign.  Then blanks fill the text to Width
//   bytes: before the text for Align::Right, as printf does, and after it
//   for Align::Left, as the flag - of printf does.  A longer text is not
//   cut.  Decimals is at most 20, and Width at most 255.  The digits come
//   from integer arithmetic on the bits of the value, so the rounding mode,
//   the locale and the floating environment do not change the text.
//
// THE PADDED TEXT
//   Padded<Width, Side>{value} writes a TextView or an integer, and blanks
//   to Width bytes on the side that Side gives, as "%Ns", "%Nd" and "%Nu" of
//   printf do, and Align::Left as the flag - of printf does.  The width
//   counts bytes, as printf counts the bytes of %s, so a character of two
//   bytes of UTF-8 counts two.  A longer text is not cut.  Width is at most
//   255.
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

// An unsigned integer as Width hexadecimal digits (THE HEXADECIMAL TEXT).
// The width is a template argument, so it is part of the type and of the
// call, and a check at compile time proves that the width holds each value
// of the type.  The smallest type takes two digits, so the width is 2 to 16.
template <std::size_t Width>
    requires(Width >= 2 && Width <= 16)
class Hex final {
    std::uint64_t value_ = 0;

public:
    // The count of the digits of the text.
    static constexpr std::size_t width = Width;

    template <UnsignedInteger T>
        requires(sizeof(T) <= sizeof(std::uint64_t) && sizeof(T) * 2 <= Width)
    constexpr explicit Hex(T value) noexcept : value_{value} {}

    template <UnsignedInteger T>
        requires(sizeof(T) * 2 > Width)
    Hex(T) = delete("the width of this Hex holds fewer digits than a value of this type, so a digit would be cut: "
                    "narrow the value to a type that the width holds, or give a larger width");

    template <class T>
        requires(!UnsignedInteger<T>)
    Hex(T) = delete("a Hex takes an unsigned integer: a signed value has two texts for one set of bits, so "
                    "give the unsigned value of its bits");

    // The value whose digits the report writes.
    [[nodiscard]] constexpr std::uint64_t get() const noexcept { return value_; }
};

// The side of a text in its width (THE FIXED TEXT OF A FLOAT and THE PADDED
// TEXT): Right puts the blanks before the text, and Left after it.
enum class Align : std::uint8_t {
    Left,
    Right,
};

// The sign of a fixed text: a minus sign for a value whose sign bit is set,
// and for Always also a plus sign before each other value (THE FIXED TEXT OF
// A FLOAT).
enum class SignMark : std::uint8_t {
    NegativeOnly,
    Always,
};

namespace detail {

// The mark of HexBytes, the bytes of a View as two hexadecimal digits each
// (THE HEXADECIMAL TEXT).  foundation/core/Region.h defines HexBytes beside
// the View that it holds, because that header includes this one, so this
// header knows the class only by its mark.  Only HexBytes names the mark.
struct HexBytesMark final {};

// The most decimals of a Fixed, and the widest width of a Fixed or a Padded.
inline constexpr std::size_t fixed_decimals_max = 20;
inline constexpr std::size_t padded_width_max = 255;

// The layout word of a Fixed or a Padded in the argument of the cold
// formatter: the width in bits 0 to 15, Align::Left in bit 16,
// SignMark::Always in bit 17, and the decimals in bits 24 to 31.
[[nodiscard]] constexpr std::uint64_t fmt_layout_(std::size_t decimals, std::size_t width, Align side,
                                                  SignMark sign) noexcept {
    return static_cast<std::uint64_t>(width) | (side == Align::Left ? std::uint64_t{1} << 16U : 0U)
         | (sign == SignMark::Always ? std::uint64_t{1} << 17U : 0U) | (static_cast<std::uint64_t>(decimals) << 24U);
}

// What a Padded holds: a text, or the bits of a signed or an unsigned
// integer.
enum class PaddedSource : std::uint8_t {
    text,
    signed_integer,
    unsigned_integer,
};

struct PaddedValue {
    PaddedSource source = PaddedSource::text;
    std::uint64_t bits = 0;
    TextView text{};
};

}  // namespace detail

// A Hex of each width, and no other type.  The concept reads the width of
// the type and asks that the type is the Hex of that width, so no class of
// a caller passes for a Hex.
template <class T>
concept HexDigits = requires { requires std::is_same_v<T, Hex<T::width>>; };

// HexBytes: the class that names the mark of the base, with no cv-qualifier.
template <class T>
concept HexByteRun = requires {
    requires std::is_same_v<T, std::remove_cv_t<T>> && std::is_same_v<typename T::report_mark, detail::HexBytesMark>;
};

// A float or a double with a fixed count of decimals (THE FIXED TEXT OF A
// FLOAT).  The counts are template arguments, so they are part of the type
// and of the call, and a check at compile time bounds them.  The class holds
// the bits of the double, so a float costs one exact conversion.
template <std::size_t Decimals, std::size_t Width = 0, Align Side = Align::Right,
          SignMark Sign = SignMark::NegativeOnly>
    requires(Decimals <= detail::fixed_decimals_max && Width <= detail::padded_width_max)
class Fixed final {
    std::uint64_t bits_ = 0;

public:
    // The count of the digits after the point, the width in bytes, the side
    // of the text in the width, and the sign.
    static constexpr std::size_t decimals = Decimals;
    static constexpr std::size_t width = Width;
    static constexpr Align side = Side;
    static constexpr SignMark sign = Sign;

    constexpr explicit Fixed(double value) noexcept : bits_{__builtin_bit_cast(std::uint64_t, value)} {}

    // A float has a double of the same value, as printf promotes it.
    constexpr explicit Fixed(float value) noexcept
        : bits_{__builtin_bit_cast(std::uint64_t, static_cast<double>(value))} {}

    template <class T>
        requires(!std::is_same_v<T, double> && !std::is_same_v<T, float> && !std::is_same_v<T, Fixed>)
    Fixed(T) =
        delete("a Fixed takes a float or a double: write an integer as itself, and give a long double or another "
               "floating type as the double that you round it to");

    // The bits of the double whose text the report writes.
    [[nodiscard]] constexpr std::uint64_t get() const noexcept { return bits_; }
};

// A text or an integer with blanks to a width (THE PADDED TEXT).  The width
// is a template argument, so it is part of the type and of the call.
template <std::size_t Width, Align Side = Align::Right>
    requires(Width <= detail::padded_width_max)
class Padded final {
    detail::PaddedValue value_{};

public:
    // The width in bytes, and the side of the text in the width.
    static constexpr std::size_t width = Width;
    static constexpr Align side = Side;

    constexpr explicit Padded(TextView text) noexcept : value_{detail::PaddedSource::text, 0, text} {}

    template <Integer T>
    constexpr explicit Padded(T value) noexcept
        : value_{std::is_signed_v<T> ? detail::PaddedSource::signed_integer : detail::PaddedSource::unsigned_integer,
                 static_cast<std::uint64_t>(value), TextView{}} {}

    template <class T>
        requires(!Integer<T> && !std::is_same_v<T, TextView> && !std::is_same_v<T, Padded>)
    Padded(T) = delete("a Padded takes a TextView or an integer: give a float or a double through Fixed, which has a "
                       "width of its own, and a literal as TextView{\"text\"}");

    // The text or the integer whose text the report writes.
    [[nodiscard]] constexpr detail::PaddedValue get() const noexcept { return value_; }
};

// A Fixed of each count, and a Padded of each width, and no other type.  The
// concept reads the counts of the type and asks that the type is the class
// of those counts, so no class of a caller passes for one.
template <class T>
concept FixedDigits = requires { requires std::is_same_v<T, Fixed<T::decimals, T::width, T::side, T::sign>>; };

template <class T>
concept PaddedDigits = requires { requires std::is_same_v<T, Padded<T::width, T::side>>; };

// A type that a report can write: an Integer and a Float of the Scalar
// family, a bool, a TextView, a Hex, a HexBytes, a Fixed and a Padded.  An
// Integer is not bool and not a character type, and signed char and unsigned
// char are integers, so a std::uint8_t is written as a number.  The argument
// of a report binds to a reference, so an argument type is never a
// reference.  A deduced argument type is never cv-qualified, so each arm
// refuses a cv-qualified type.  Each function that writes the arguments of a
// Fmt asks for this concept of each one.
template <class T>
concept Formattable = Integer<T> || Float<T> || std::is_same_v<T, bool> || std::is_same_v<T, TextView> || HexDigits<T>
                   || HexByteRun<T> || FixedDigits<T> || PaddedDigits<T>;

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
    binary64,
    hex_digits,
    hex_bytes,
    fixed_binary64,
    padded_text,
    padded_signed,
    padded_unsigned
};

// One argument of a report, as the cold formatter reads it.  An integer, a
// bool and a floating value are in bits: a signed integer as the bits of
// its std::int64_t value, and a float or a double as its IEEE bits.  A text
// is in first and size.  A Hex is its value in bits and its width in size,
// and a HexBytes is its bytes in first and size.  A Fixed is the bits of its
// double and its layout word (fmt_layout_) in size.  A Padded text is its
// text in first and size and its layout word in bits, and a Padded integer
// is its bits and its layout word in size.
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
    } else if constexpr (HexDigits<T>) {
        return FmtArg{FmtArgKind::hex_digits, value.get(), nullptr, T::width};
    } else if constexpr (HexByteRun<T>) {
        // A char may read the bytes of each object, so the formatter reads
        // the std::byte elements of the View as characters.  An empty View
        // has no first element to name.
        auto const bytes = value.view();
        if (bytes.size() == 0) return FmtArg{FmtArgKind::hex_bytes, 0, nullptr, 0};
        return FmtArg{FmtArgKind::hex_bytes, 0,
                      static_cast<char const*>(static_cast<void const*>(__builtin_addressof(*bytes.begin()))),
                      bytes.size()};
    } else if constexpr (FixedDigits<T>) {
        return FmtArg{FmtArgKind::fixed_binary64, value.get(), nullptr,
                      fmt_layout_(T::decimals, T::width, T::side, T::sign)};
    } else if constexpr (PaddedDigits<T>) {
        PaddedValue const padded = value.get();
        std::uint64_t const layout = fmt_layout_(0, T::width, T::side, SignMark::NegativeOnly);
        if (padded.source == PaddedSource::text) {
            return FmtArg{FmtArgKind::padded_text, layout, text_first_(padded.text), padded.text.size()};
        }
        FmtArgKind const kind =
            padded.source == PaddedSource::signed_integer ? FmtArgKind::padded_signed : FmtArgKind::padded_unsigned;
        return FmtArg{kind, padded.bits, nullptr, layout};
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
