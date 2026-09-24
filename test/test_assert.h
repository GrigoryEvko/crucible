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

#include <cstdlib>

#ifdef assert
#undef assert
#endif
#define assert(cond)               \
    do {                           \
        if (!(cond)) std::abort(); \
    } while (0)
