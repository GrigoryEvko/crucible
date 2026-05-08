// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #3 of 7 for safety/Pre.h + safety/Post.h.
//
// Premise: CRUCIBLE_PRE on a function taking a SCALAR by-value must
// fire at consteval when the predicate is violated.  Probe Shape #1:
// `pre(x > 0)` on `int x` — silently bypassed by P2900 pre() on the
// un-patched distro GCC 16.1.1 May-1 build.  Patched build (PR
// c++/124241 cherry-pick) closes the bypass for native pre(); this
// fixture pins the behavior of CRUCIBLE_PRE across both builds.
//
// Distinct mismatch class: scalar argument (no struct member access),
// no pointer indirection.  The simplest possible parameter shape —
// often the first thing a developer writes when adding a new check.
// If this shape regresses, the entire macro foundation is broken.
//
// Expected diagnostic: "non-constant condition for static assertion"
// — GCC's consteval evaluator hitting `if consteval { __builtin_trap(); }`
// in the CRUCIBLE_PRE expansion.

#include <crucible/safety/Pre.h>

namespace {

[[nodiscard]] constexpr int positive_only_double(int x) noexcept {
    CRUCIBLE_PRE(x > 0);
    return x * 2;
}

// x = 0 → predicate (x > 0) is false → CRUCIBLE_PRE must fire at consteval.
static_assert(positive_only_double(0) == 0,
    "CRUCIBLE_PRE on a scalar by-value parameter MUST fire at consteval "
    "when the predicate is violated.  If this static_assert ever evaluates "
    "successfully, Pre.h's consteval enforcement is broken for the "
    "simplest parameter shape (scalar by-value), and every fixture that "
    "depends on this shape is silently green.");

}  // namespace

int main() { return 0; }
