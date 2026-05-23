// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule D002 (FIXY-V-243):
//
//     marks_recurses_unbounded<F>::value == true  ⇒  ill-formed
//
// Plain English: a recursion grant (the future grant::dispatch::recurses<>)
// declared without an NTTP MaxDepth reds.  An unbounded recursion bound
// is the implicit-stack-growth anti-pattern — the function may exhaust
// the stack with no static witness of the worst-case depth.
//
// Mismatch class: recursion grant without a bounded MaxDepth (marker-
// driven; the V-245 grant header specializes the trait when the recurses
// grant carries no depth NTTP).
//
// Concrete bug-class this catches: dropping the D002 term would let an
// unbounded-recursion declaration compile, defeating the StackUse-axis
// discipline (StackUse::Unbounded has no proven frame bound).
//
// Expected diagnostic substring: "D002:"

#include <crucible/safety/Fn.h>

namespace fn = crucible::safety::fn;

namespace neg_collision_d002 {
struct RecurseMarker {};
using Bad = fn::Fn<RecurseMarker>;
}  // namespace neg_collision_d002

namespace crucible::safety::fn::collision {
    template <> struct marks_recurses_unbounded<::neg_collision_d002::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_d002::Bad the_fixture{};

int main() { return 0; }
