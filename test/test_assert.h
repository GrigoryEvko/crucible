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
//
// element_at(view, position) gives one element of a View, and run_of gives
// a new array of the arena that holds some values.  A test builds and reads
// the arrays of a traced op with them.

#include <fixy/Core.h>

#include <cstddef>
#include <type_traits>

namespace crucible::test {

// Writes the pass line text to standard output.  The text holds its own
// line break.  The text comes by value, so the caller writes it into the
// argument area of the call and keeps no object in its frame, as a call of
// fixy::report with no argument does.
[[gnu::cold, gnu::noinline]] inline void pass(::fixy::Fmt<> text) noexcept { ::fixy::report(::fixy::Sink::Out, text); }

// The same, with arguments.  The function is cold and not inline, as the
// report with arguments is, so each pass line costs its caller one call.
template <class First, class... Rest>
[[gnu::cold, gnu::noinline]] void pass(::fixy::Fmt<std::type_identity_t<First>, std::type_identity_t<Rest>...> text,
                                       First first, Rest... rest) noexcept {
    ::fixy::report(::fixy::Sink::Out, text, first, rest...);
}

// The element at position of a View, for a test that reads or writes one
// element by its position.  A position at or past the count ends the
// process.
template <class T>
[[nodiscard]] T& element_at(::fixy::View<T> view, std::size_t position) noexcept {
    return *view.get(position).expect("a test reads an element at or past the count of its View");
}

// The values in a new array of the arena, in order.  The alloc argument is
// the allocation capability of the test.
template <class Marker, class First, class... Rest>
[[nodiscard]] ::fixy::View<First> run_of(Marker alloc, ::fixy::BumpArena& arena, First first, Rest... rest) noexcept {
    ::fixy::View<First> const run = arena.array(alloc, 1 + sizeof...(Rest), first);
    if constexpr (sizeof...(Rest) > 0) {
        std::size_t position = 1;
        ((element_at(run, position++) = static_cast<First>(rest)), ...);
    }
    return run;
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
