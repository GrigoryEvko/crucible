// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule V201 (FIXY-V-260):
//
//     marks_hot_path<F>::value == true
//   ∧ F::type_t carries a Hw tier >= NonDeterministicTsc
//   ⇒ ill-formed
//
// FIXY-FOUND-069 sub-HS14 closure: V201's trigger is a tier-threshold
// (`hw_at_or_above_v<NonDeterministicTsc, type_t>`), and the
// HwInstruction lattice has TWO values at/above it — NonDeterministicTsc(3)
// and PrivilegedMsr(4).  The sibling fixture
// neg_collision_V201_hotpath_nondet_tsc.cpp witnesses the
// NonDeterministicTsc (threshold-boundary) arm; this companion witnesses
// the PRIVILEGEDMSR (top) arm.  A refactor narrowing the gate from
// `>= NonDeterministicTsc` to `== NonDeterministicTsc` would silently
// re-admit a hot-path rdmsr/wrmsr/IN/OUT — and only this arm catches it.
//
// Co-fire note (honest): a PrivilegedMsr tier with NO Init-context row
// ALSO satisfies V202 (`== PrivilegedMsr WITHOUT effect_row ⊇ Init`), so
// BOTH V201 and V202 fire on this pack.  That does NOT weaken the V201
// witness: GCC emits every failing static_assert, the neg driver greps
// the substring "V201:", and that substring appears ONLY because V201
// itself rejects this pack.  Were V201 deleted or narrowed to
// `== NonDeterministicTsc`, "V201:" would vanish from the output and this
// test would correctly red — V202 alone cannot satisfy the V201: grep.
// (Suppressing V202 via an Init row is not done here: it would require
// threading an effects::Init row through the Fn AND a hot-path function
// with an Init row is itself a contradiction; the co-fire is the cleaner
// witness.)
//
// Expected diagnostic substring: "V201:".

#include <crucible/safety/Fn.h>
#include <crucible/safety/Hw.h>

namespace fn = crucible::safety::fn;
namespace sf = crucible::safety;
using HW = crucible::algebra::lattices::HwInstruction;

namespace neg_collision_v201_msr {
// PrivilegedMsr (tier 4) — strictly above NonDeterministicTsc (tier 3);
// the `>= NonDeterministicTsc` threshold still fires, witnessing the top arm.
using Bad = fn::Fn<sf::Hw<HW::PrivilegedMsr, int>>;
}  // namespace neg_collision_v201_msr

// Mark Bad as hot-path — required to fire V201.
namespace crucible::safety::fn::collision {
    template <> struct marks_hot_path<::neg_collision_v201_msr::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_v201_msr::Bad the_fixture{};

int main() { return 0; }
