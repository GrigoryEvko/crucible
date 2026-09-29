#pragma once

// The ctgrind method: memcheck tracks, for every bit, whether the program
// ever defined it.  A test marks each secret input as undefined, runs the
// constant-time code, and marks the result as defined again at the point
// where the design lets the result go public.  memcheck then reports every
// conditional jump, every conditional move it cannot prove safe, and every
// address that depends on a secret bit in between.  A report is a timing
// channel, so each report fails the test through --error-exitcode.
//
// Build these tests with release optimization.  The optimizer is what
// turns arithmetic into a branch, so a test built at -O0 proves nothing
// about the code that ships.
//
// A run outside valgrind proves nothing either: every client request is
// then a no-op.  require_valgrind() makes that case a failure.

#include <valgrind/memcheck.h>
#include <valgrind/valgrind.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>

namespace ct_taint {

// Stops the run when valgrind is not the host.  Called first in main.
inline void require_valgrind() noexcept {
    if (RUNNING_ON_VALGRIND == 0) {
        std::fputs("ct_taint: this test must run under valgrind --tool=memcheck; outside valgrind every "
                   "taint request is a no-op and the run proves nothing\n",
                   stderr);
        std::exit(2);
    }
}

// Marks the bytes of `value` as undefined, which is how this harness
// spells "secret".  The value itself is unchanged.
template <typename T>
void make_secret(T& value) noexcept {
    (void)VALGRIND_MAKE_MEM_UNDEFINED(&value, sizeof value);
}

// Marks the bytes of `value` as defined again.  This is the point where a
// secret-derived result is allowed to become public.  The copy keeps the
// original object secret.
template <typename T>
[[nodiscard]] T make_public(T value) noexcept {
    (void)VALGRIND_MAKE_MEM_DEFINED(&value, sizeof value);
    return value;
}

// Marks a byte range as undefined.
inline void make_secret_bytes(void* data, std::size_t size) noexcept { (void)VALGRIND_MAKE_MEM_UNDEFINED(data, size); }

// The number of memcheck errors so far in this process.
[[nodiscard]] inline unsigned long reported_errors() noexcept {
    return static_cast<unsigned long>(VALGRIND_COUNT_ERRORS);
}

// A failed functional check.  The taint result is separate: memcheck
// reports its own errors and --error-exitcode turns them into a failure.
struct Tally {
    int failures = 0;

    void expect(bool is_equal, char const* what) noexcept {
        if (!is_equal) {
            std::fprintf(stderr, "ct_taint: wrong result: %s\n", what);
            ++failures;
        }
    }
};

}  // namespace ct_taint
