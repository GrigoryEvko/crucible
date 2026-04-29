#pragma once

// ── test/test_assert.h — NDEBUG-robust assertion for test TUs ──────
//
// Replaces <cassert> in test files.  The standard's assert macro is
// stripped to nothing under -DNDEBUG, which causes two test-quality
// failures:
//
//   1. Tests silently print "PASSED" while not actually checking
//      anything when -DNDEBUG leaks through (e.g. through stale
//      CMakeCache.txt retaining release flags across a preset
//      switch).  Catastrophic for CI integrity.
//
//   2. Variables consumed only by stripped asserts become "set but
//      not used" under -Werror=unused-but-set-variable, breaking
//      the build entirely under release-flag pollution.
//
// CMakeLists.txt §432 guards against (1) by skipping test/ in
// release builds.  But the guard is fragile: any developer who
// configures release in build/ then switches back to default
// without `rm -rf build` will retain the release CACHE values,
// reproducing both failures.  This header makes test files robust
// to that scenario.
//
// The macro deliberately shadows the name `assert` for drop-in
// compatibility with existing test code that uses the standard
// idiom.  Tests including this header MUST NOT also include
// <cassert>; doing so would let the standard's redefinition win
// under high-NDEBUG settings.

#include <cstdlib>

#ifdef assert
#  undef assert
#endif
#define assert(cond) do { if (!(cond)) std::abort(); } while (0)
