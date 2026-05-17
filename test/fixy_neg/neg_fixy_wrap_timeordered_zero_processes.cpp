// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-WRAP fixture #10: TimeOrdered<T, 0> is rejected by a named
// static_assert.
//
// Violation: TimeOrdered<T, N> requires N > 0 — a zero-participant
// vector clock has no algebraic content.  Routing through fixy::wrap
// must preserve the static_assert identically.
//
// Expected diagnostic: substring "TimeOrdered<T, 0>" / "N > 0" /
// "static assertion failed" / "forbidden".

#include <crucible/fixy/Wrap.h>

namespace fw = crucible::fixy::wrap;

struct TypeFixyWrapTimeOrderedZero {};

// Should FAIL: TimeOrdered<int, 0> fires the static_assert.
namespace {
using BadEvent = fw::TimeOrdered<int, 0>;
[[maybe_unused]] BadEvent obj{};
}  // namespace

int main() {
    return 0;
}
