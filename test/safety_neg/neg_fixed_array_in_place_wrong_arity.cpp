// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for safety::FixedArray<T, N> (#1081).
//
// Premise: FixedArray<T, N>(std::in_place, args...) where
// sizeof...(args) != N MUST be a compile error.  The in_place ctor's
// requires-clause is `(sizeof...(Args) == N)`, which rejects:
//   - too few args (would leave tail uninitialized — defeats NSDMI
//     default-zero invariant — silently)
//   - too many args (would attempt to pack-expand beyond N — UB)
//
// Without this rejection, `FixedArray<int, 8>(std::in_place, 1, 2, 3)`
// would silently produce a partially-filled array with the trailing
// 5 slots holding default-initialized T{} values that the caller
// did NOT mean to declare.  The compile error forces the caller to
// either:
//   (a) provide exactly N args to in_place
//   (b) use the default ctor + later operator[]/at() writes
//   (c) use the static factory fill_with(v)
//
// This fixture uses N=8 (matching production sites #932 / #1019)
// with 3 args — clearly wrong arity — to hit the requires-clause
// rejection.
//
// Expected diagnostic: "no matching function for call to" /
// "constraints not satisfied" / "associated constraints are not
// satisfied" pointing at the FixedArray<int, 8>(in_place, ...) call
// site.  The (sizeof...(Args) == N) constraint fires; no other
// in_place ctor overload matches.

#include <crucible/safety/FixedArray.h>

#include <utility>

namespace saf = crucible::safety;

int main() {
    // Bridge fires: 3 args provided where N=8 expected.  The
    // in_place ctor's `requires (sizeof...(Args) == N)` rejects.
    // Default ctor isn't a candidate (the in_place_t tag forces
    // overload resolution to the variadic in_place ctor).
    saf::FixedArray<int, 8> bad{std::in_place, 1, 2, 3};
    (void)bad;
    return 0;
}
