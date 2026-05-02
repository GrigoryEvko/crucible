// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for safety::FixedArray<T, N> (#1081).
//
// Premise: FixedArray<T, 0> MUST be a compile error.  The wrapper
// requires `(N > 0)` at the concept gate.  An empty FixedArray would:
//   - have no valid front()/back() (would be UB on dereference)
//   - make the index_type = Refined<bounded_above<N-1>, size_t>
//     ill-defined: N-1 wraps to SIZE_MAX, meaning "any index up to
//     SIZE_MAX is valid", but there ARE no valid indices when N=0
//   - clash with std::array<T, 0>'s special-case empty type
//
// By REJECTING the zero-sized instantiation, the wrapper guarantees
// every FixedArray<T, N> has at least one element — front() and
// back() are always valid, the typed-bound index is always
// meaningful (N-1 ≥ 0), and there's no degenerate-empty case to
// special-case.  Empty cases handled by Optional<FixedArray<T, M>>.
//
// Expected diagnostic: "associated constraints are not satisfied" /
// "constraints not satisfied" / "no matching template" pointing at
// the FixedArray<int, 0> instantiation.  The (N > 0) requires-clause
// on the primary template fires before the body is substituted.

#include <crucible/safety/FixedArray.h>

namespace saf = crucible::safety;

int main() {
    // Bridge fires: FixedArray<int, 0> attempts to instantiate the
    // primary template, which carries `requires (N > 0)` — N=0 fails
    // the constraint, no other partial spec exists for N=0, so the
    // instantiation is ill-formed.
    saf::FixedArray<int, 0> bad{};
    (void)bad;
    return 0;
}
