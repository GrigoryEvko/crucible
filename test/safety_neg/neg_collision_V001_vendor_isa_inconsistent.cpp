// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule V001 (FIXY-V-260) — CONCEPT-ISOLATION path.
//
//     marks_vendor_isa_inconsistent<F>::value == true ⇒ V001_OK<F> false
//
// Plain English: a binding pack that declares vendor::intrinsic<V, I>
// grants whose (V, I) disagree (one pins NV, another an x86 ISA family)
// is unsound — the per-grant vendor_isa_consistent_v<V, I> gate (V-258)
// only checks a SINGLE intrinsic; the pack can still mix vendors.  V001
// is the grant-pack rule the V-258 grant-pack analysis specializes.
//
// Mismatch class: marker-driven, asserted standalone on a non-Fn probe
// so the failure is the concept itself (a refactor dropping V001 from
// AllRulesOK is caught even if an earlier rule would have masked it in
// the production path).
//
// Expected diagnostic substring: V001.

#include <crucible/safety/CollisionCatalog.h>

#include <type_traits>

namespace csc = crucible::safety::fn::collision;

namespace neg_collision_v001 {
struct Probe {};
}  // namespace neg_collision_v001

namespace crucible::safety::fn::collision {
    template <> struct marks_vendor_isa_inconsistent<::neg_collision_v001::Probe>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

static_assert(csc::V001_OK<::neg_collision_v001::Probe>,
              "V001: vendor::intrinsic pack declares inconsistent (vendor, ISA)");

int main() { return 0; }
