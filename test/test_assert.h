#pragma once

// The harness of the tests: the assert macro and the pass line.
//
// This header replaces <cassert> in a test.  The standard assert expands to
// nothing under NDEBUG, so release flags that reach a test build give two
// failures: the test reports a pass while it checks nothing, and a variable
// that only the stripped assert reads trips
// -Werror=unused-but-set-variable.  This assert always checks.
//
// The macro shadows the name assert, so a test needs no edit.  A test that
// includes this header must not also include <cassert>.  A later include of
// <cassert> defines assert again and gives back the stripped form.
//
// A failed assert ends the process through fixy::fatal.  The report names
// the condition and the site of the assert: its file, its line and its
// function.  fatal() aborts, so a death test still sees SIGABRT.  A passing
// assert costs one branch, and the failure path is a cold call.
//
// The text of the report and the condition are static constexpr locals of
// the assert, so they are constants in read-only data.  The failure path
// then passes an address and two constants, and it builds no object on the
// stack of the test.  With temporaries in place of the locals, AddressSanitizer
// gave each one a checked stack slot, and a Debug build of a test with 63
// asserts took 18.9 G instructions in place of 18.1 G.
//
// pass() writes the pass line of a test to standard output, through
// fixy::report.  The text takes the placeholders of fixy::Fmt.  The text
// is written before the call returns, so a pass line before a later
// failure stays in the log.

#include <fixy/Core.h>

#include <type_traits>

namespace crucible::test {

// Writes the pass line text, with its arguments, to standard output.  The
// text holds its own line break.  The function is cold and not inline, as
// the report with arguments is, so each pass line costs its caller one
// call.
template <class... Args>
[[gnu::cold, gnu::noinline]] void pass(::fixy::Fmt<std::type_identity_t<Args>...> const& text, Args... args) noexcept {
    ::fixy::report(::fixy::Sink::Out, text, args...);
}

}  // namespace crucible::test

#ifdef assert
#undef assert
#endif
#define assert(cond)                                                                           \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            static constexpr ::fixy::Fmt<::fixy::TextView> crucible_assert_text{"assert: {}"}; \
            static constexpr ::fixy::TextView crucible_assert_condition{#cond};                \
            ::fixy::fatal(crucible_assert_text, crucible_assert_condition);                    \
        }                                                                                      \
    } while (0)
