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
// then a no-op.  is_under_valgrind() lets the harness refuse that case.

#include <valgrind/memcheck.h>
#include <valgrind/valgrind.h>

#include <fixy/os/Environment.h>
#include <foundation/Quarantine.h>

namespace ct_taint {

// The client requests of valgrind.  Each request is inline asm that hands
// the address of a C array of words to the tool, so the whole interface to
// the tool is in one region.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: a client request of valgrind is inline asm over a C array of words")

// True when valgrind is the host of the process.
[[nodiscard]] inline bool is_under_valgrind() noexcept { return RUNNING_ON_VALGRIND != 0; }

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

// The number of memcheck errors so far in this process.
[[nodiscard]] inline unsigned long reported_errors() noexcept {
    return static_cast<unsigned long>(VALGRIND_COUNT_ERRORS);
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// True when DEBUGINFOD_URLS names a server.  valgrind reads the variable at
// its start, and it asks each server for the debug information of each
// loaded object that has none on the host.  A test must not depend on the
// network: one lookup can take seconds.  test/ct/CMakeLists.txt removes the
// variable from the environment of each taint test.  The environment door
// reads the variable one time, before main.
[[nodiscard]] inline bool names_debuginfod_server() noexcept { return ::fixy::env::switches()->has_debuginfod_server; }

}  // namespace ct_taint
