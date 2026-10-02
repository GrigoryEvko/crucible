#pragma once

// The Report family, its first part: the place of a call, the text of a
// report, and the fatal exit.
//
//   Site                the file, the line and the function of a call
//   Fmt<>               a string literal and the Site of its call
//   fatal(fmt)          writes the text and the site, and ends the process
//   unreachable()       the same, for a point that the code marks as one
//                       that control never reaches
//
// The constructors of Site and Fmt are consteval.  So the text and the
// names of a site are constant arrays of static storage, and a reader
// finds their end.  A default argument reads the builtins at the call
// that takes the default.
//
// A Fmt takes no argument at this time.  The formatter of the family gives
// a placeholder its meaning, and a placeholder is a brace.  So a Fmt holds
// no brace, and no text that compiles changes its meaning when the
// formatter arrives.
//
// fatal() and unreachable() end the process in each build, with each
// contract semantic.  They are [[noreturn]], so the optimizer knows that
// control does not continue after them.  Each one is an inline function
// that loads the texts and the line into registers and calls one cold
// function.  The caller builds no object on its stack, and the hot path
// keeps only the branch to the cold call.
//
// The cold function is in src/foundation/ContractHandler.cpp.  It writes
// with write(2) only, so a call from a signal handler is safe, and a report
// in progress on the same thread makes a second report abort at once.

#include <cstddef>
#include <cstdint>

namespace foundation::core {

template <class... Args>
    requires(sizeof...(Args) == 0)
class Fmt;

class Site;

[[noreturn, gnu::always_inline]] inline void fatal(Fmt<> fmt) noexcept;
[[noreturn, gnu::always_inline]] inline void unreachable(Site site) noexcept;

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
    friend void fatal(Fmt<> fmt) noexcept;
    friend void unreachable(Site site) noexcept;

    char const* file_;
    char const* function_;
    std::uint32_t line_;
};

// The text of a report: a string literal with no brace and no zero byte
// before its end, and the place of the call that gives it.  The place
// comes from scalar default arguments and not from a default Site.  A
// default argument of class type is a temporary, and the range of a for
// loop extends the life of each temporary, so the temporary would be no
// constant there.
template <class... Args>
    requires(sizeof...(Args) == 0)
class Fmt final {
public:
    template <std::size_t Length>
    consteval Fmt(char const (&text)[Length], char const* file = __builtin_FILE(), int line = __builtin_LINE(),
                  char const* function = __builtin_FUNCTION()) noexcept
        : text_{text}, site_{file, line, function} {
        // An empty text, a text with no terminating zero, a zero byte inside
        // the text, and a brace are not constant expressions here, so the
        // build stops at the call.
        if (Length < 2 || text[Length - 1] != '\0') __builtin_trap();
        for (std::size_t index = 0; index + 1 < Length; ++index) {
            if (text[index] == '\0' || text[index] == '{' || text[index] == '}') __builtin_trap();
        }
    }

private:
    friend void fatal(Fmt<> fmt) noexcept;

    char const* text_;
    Site site_;
};

namespace detail {

// Writes `foundation: fatal: TEXT` and the site to file descriptor 2.
// Then it stops in a debugger when one is attached, and aborts.
[[noreturn, gnu::cold]] void report_fatal_(char const* text, char const* file, std::uint32_t line,
                                           char const* function) noexcept;

}  // namespace detail

// Writes the text and the site to file descriptor 2, and ends the process.
// A search for `fatal(` finds each fatal exit of the code.
inline void fatal(Fmt<> fmt) noexcept {
    detail::report_fatal_(fmt.text_, fmt.site_.file_, fmt.site_.line_, fmt.site_.function_);
}

// The point that control never reaches, such as the arm after a switch
// that handles each enumerator.  A value outside the enumerators reaches
// it, and the process ends with the site.
inline void unreachable(Site site = Site{}) noexcept {
    detail::report_fatal_("control reached a point that the code marks as unreachable", site.file_, site.line_,
                          site.function_);
}

}  // namespace foundation::core
