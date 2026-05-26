// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule V301 (FIXY-V-260):
//
//     marks_hot_path<F>::value == true
//   ∧ F::type_t carries a BarrierStrength tier >= SeqCst
//   ⇒ ill-formed
//
// FIXY-FOUND-069 sub-HS14 closure: V301's trigger is a tier-threshold
// (`barrier_at_or_above_v<SeqCst, type_t>`), and the BarrierStrength
// lattice has TWO values at/above the threshold — SeqCst(5) and
// FullFence(6).  The sibling fixture neg_collision_V301_hotpath_full_fence.cpp
// witnesses the FullFence (top) arm; this companion witnesses the
// SEQCST (threshold-boundary) arm.  The catalog diagnostic explicitly
// names both "BarrierGuarded<SeqCst, U> or BarrierGuarded<FullFence, U>"
// as triggers, so SeqCst is a first-class arm, not a near-duplicate.
//
// Why the boundary value matters specifically: a refactor narrowing the
// gate from `>= SeqCst` to `== FullFence` (the common off-by-one when an
// author reasons "only the standalone fence is expensive") would silently
// re-admit a hot-path SeqCst barrier — a full sequentially-consistent
// ordering that still drains the store buffer on x86.  Only this arm
// catches that narrowing; the FullFence arm passes it.
//
// Clean (no cross-fire): SeqCst(5) > AcqRel(4), so V401 (fires on
// BarrierStrength ⊏ AcqRel — the UNDER-fenced cross-CTA case) does not
// trigger; V301 is the sole rule on `barrier >= SeqCst`.  No scope /
// Hw / SimdIsa / Stdio tier is engaged here.
//
// Expected diagnostic substring: "V301:".

#include <crucible/safety/BarrierGuarded.h>
#include <crucible/safety/Fn.h>

namespace fn = crucible::safety::fn;
namespace sf = crucible::safety;
using BS = crucible::algebra::lattices::BarrierStrength;

namespace neg_collision_v301_seqcst {
// SeqCst (tier 5) — the threshold-boundary value; >= SeqCst still fires,
// witnessing the second (lower) arm distinct from the FullFence top.
using Bad = fn::Fn<sf::BarrierGuarded<BS::SeqCst, int>>;
}  // namespace neg_collision_v301_seqcst

// Mark Bad as hot-path — required to fire V301.
namespace crucible::safety::fn::collision {
    template <> struct marks_hot_path<::neg_collision_v301_seqcst::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_v301_seqcst::Bad the_fixture{};

int main() { return 0; }
