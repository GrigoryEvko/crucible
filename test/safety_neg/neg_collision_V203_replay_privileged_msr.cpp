// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule V203 (FIXY-V-260):
//
//     marks_replay_required<F>::value == true
//   ∧ F::type_t carries a Hw tier >= NonDeterministicTsc
//   ⇒ ill-formed
//
// FIXY-FOUND-069 sub-HS14 closure: V203's trigger is a tier-threshold
// (`hw_at_or_above_v<NonDeterministicTsc, type_t>`), with two values
// at/above it — NonDeterministicTsc(3) and PrivilegedMsr(4).  The sibling
// fixture neg_collision_V203_replay_nondet_tsc.cpp witnesses the
// NonDeterministicTsc (boundary) arm; this companion witnesses the
// PRIVILEGEDMSR (top) arm.  A refactor narrowing `>= NonDeterministicTsc`
// to `== NonDeterministicTsc` would let a deterministic-replay body carry
// a PrivilegedMsr Hw tier — only this arm catches it.
//
// Co-fire note (honest): PrivilegedMsr with NO Init row also satisfies
// V202 (`== PrivilegedMsr WITHOUT Init`).  The neg driver greps the
// rule-specific substring "V203:", which appears only because V203
// itself rejects this pack; V202 alone cannot satisfy the V203: grep, so
// the test reds iff V203 stops rejecting PrivilegedMsr.  (V201 is NOT
// triggered: it needs marks_hot_path, and this fixture sets only
// marks_replay_required.)
//
// Expected diagnostic substring: "V203:".

#include <crucible/safety/Fn.h>
#include <crucible/safety/Hw.h>

namespace fn = crucible::safety::fn;
namespace sf = crucible::safety;
using HW = crucible::algebra::lattices::HwInstruction;

namespace neg_collision_v203_msr {
// PrivilegedMsr (tier 4) — strictly above NonDeterministicTsc (tier 3);
// the `>= NonDeterministicTsc` threshold still fires, witnessing the top arm.
using Bad = fn::Fn<sf::Hw<HW::PrivilegedMsr, int>>;
}  // namespace neg_collision_v203_msr

// Mark Bad as replay-required (NOT hot-path) — fires V203, not V201.
namespace crucible::safety::fn::collision {
    template <> struct marks_replay_required<::neg_collision_v203_msr::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_v203_msr::Bad the_fixture{};

int main() { return 0; }
