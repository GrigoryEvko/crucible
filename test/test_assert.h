#pragma once

// Replaces <cassert> in test TUs.  The standard assert macro expands to
// nothing under NDEBUG, so release flags leaking into a test build produce two
// failures: the test prints PASSED while checking nothing, and a variable read
// only by the stripped assert trips -Werror=unused-but-set-variable.  This
// definition always checks.
//
// The macro deliberately shadows the name assert so existing test code needs no
// edit.  A test that includes this header must not also include <cassert>.  A
// later include of <cassert> redefines assert and restores the stripped form.
//
// A failed assertion prints its file, its line and its condition to stderr,
// and then it aborts.  So a failure names its place without a debugger, and a
// death test still sees SIGABRT.

#include <cstdio>
#include <cstdlib>

namespace crucible::test {

// Print the place and the text of a failed assertion, then abort.  The helper
// is cold and out of line, so a passing assertion costs one branch.
[[noreturn, gnu::cold, gnu::noinline]] inline void assertion_failed(const char* file, int line,
                                                                    const char* condition) noexcept {
    std::fprintf(stderr, "%s:%d: assertion failed: %s\n", file, line, condition);
    std::fflush(stderr);
    std::abort();
}

}  // namespace crucible::test

#ifdef assert
#undef assert
#endif
#define assert(cond)                                                                \
    do {                                                                            \
        if (!(cond)) ::crucible::test::assertion_failed(__FILE__, __LINE__, #cond); \
    } while (0)
