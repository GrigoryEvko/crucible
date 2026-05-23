// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule V203 (FIXY-V-260):
//
//     marks_replay_required<F>::value == true
//   ∧ F::type_t carries a Hw tier >= NonDeterministicTsc
//   ⇒ ill-formed
//
// Plain English: rdtsc is hardware-dependent (different cycle base /
// invariant-TSC behavior on H100 vs 3090 hosts), so a replay-required
// body reading it diverges across reincarnation hardware — the
// instruction-axis dual of the F101 FP-replay rule.
//
// Mismatch class: replay marker × Hw tier >= NonDeterministicTsc.
// Distinct from V201 (hot-path marker on the same tier): here the
// replay marker drives the rejection while V201_OK passes (no hot
// marker), so first_failure proceeds to V203.
//
// Concrete bug this catches: dropping V203 would let a deterministic-
// replay body read a non-invariant timestamp, breaking
// bit_exact_replay_invariant across reincarnation hardware.
//
// Expected diagnostic substring: "V203:".

#include <crucible/safety/Fn.h>
#include <crucible/safety/Hw.h>

namespace fn = crucible::safety::fn;
namespace sf = crucible::safety;
using HW = crucible::algebra::lattices::HwInstruction;

namespace neg_collision_v203 {
using Bad = fn::Fn<sf::Hw<HW::NonDeterministicTsc, int>>;  // Hw tier >= NonDetTsc
}  // namespace neg_collision_v203

// Mark Bad as replay-required (NOT hot-path) — fires V203, not V201.
namespace crucible::safety::fn::collision {
    template <> struct marks_replay_required<::neg_collision_v203::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_v203::Bad the_fixture{};

int main() { return 0; }
