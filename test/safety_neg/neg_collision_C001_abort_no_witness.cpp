// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule C001 (FIXY-V-243):
//
//     marks_aborts<F>::value == true
//   ∧ F::type_t carries NO ControlFlowPinned tier >= AbortOnly
//   ⇒ ill-formed
//
// Plain English: a function declaring it may abort (the future
// grant::ctrl::abort<Rationale>) MUST carry a ControlFlow tier that
// witnesses the escape (>= AbortOnly).  Claiming abort while the result
// is typed Pure (here a bare marker type, no ControlFlowPinned wrapper)
// is the ControlFlow↔escape inconsistency Agent 10 §4 names: the
// federation cache key and every reviewer would see a "pure" function
// that in fact terminates the process.
//
// Mismatch class: abort-marker × unwitnessed ControlFlow tier.  This is
// the marker-driven trigger path (C001 also fires via a too-low wrapper
// tier).  Pairs with the other 7 V-243 fixtures, each a distinct rule.
//
// Concrete bug-class this catches: a refactor that drops the
// `!cf_at_or_above_v<AbortOnly, type_t>` term from C001_OK would let an
// abort-declaring Fn pass with a Pure-typed result, silently breaking
// the escape-witness contract.
//
// Expected diagnostic substring: "C001:"

#include <crucible/safety/Fn.h>

namespace fn = crucible::safety::fn;

namespace neg_collision_c001 {
struct AbortMarker {};
using Bad = fn::Fn<AbortMarker>;  // type_t carries no ControlFlow witness
}  // namespace neg_collision_c001

// Mark Bad as abort-declaring — fires C001 because the type_t does not
// witness the escape (no ControlFlowPinned tier >= AbortOnly).
namespace crucible::safety::fn::collision {
    template <> struct marks_aborts<::neg_collision_c001::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_c001::Bad the_fixture{};

int main() { return 0; }
