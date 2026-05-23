// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule V002 (FIXY-V-260) — CONCEPT-ISOLATION path.
//
//     marks_vendor_cross_arch<F>::value == true ⇒ V002_OK<F> false
//
// Plain English: a single binding that composes an x86-trunk intrinsic
// AND an ARM-trunk intrinsic is unsound — the emitted binary would #UD
// on whichever ISA it lands (SimdIsaLattice cross-trunk leq = false; an
// x86+ARM kernel runs on neither).  V002 is the catalog companion to
// the V-261 source::ArchPinned<Arch> cross-arch gate.
//
// Mismatch class: marker-driven, asserted standalone on a non-Fn probe.
// Distinct from V001 (inconsistent vendor PACK) — V002 is the single-
// binding cross-TRUNK case.
//
// Expected diagnostic substring: V002.

#include <crucible/safety/CollisionCatalog.h>

#include <type_traits>

namespace csc = crucible::safety::fn::collision;

namespace neg_collision_v002 {
struct Probe {};
}  // namespace neg_collision_v002

namespace crucible::safety::fn::collision {
    template <> struct marks_vendor_cross_arch<::neg_collision_v002::Probe>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

static_assert(csc::V002_OK<::neg_collision_v002::Probe>,
              "V002: single binding composes cross-arch intrinsics (x86 + ARM)");

int main() { return 0; }
