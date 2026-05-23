// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule V201 (FIXY-V-260):
//
//     marks_hot_path<F>::value == true
//   ∧ F::type_t carries a Hw tier >= NonDeterministicTsc
//   ⇒ ill-formed
//
// Plain English: a hot-path function MUST NOT read rdtsc/rdtscp
// (serializing, ≈ 20-40 cycles) or rdmsr/wrmsr (ring-0 privileged
// traps).  Both blow the ≤ 40 ns intra-socket hot budget (CLAUDE.md §IX).
//
// Mismatch class: hot-path marker × Hw tier >= NonDeterministicTsc.  Uses
// the WRAPPER-TIER trigger path (the shipped V-254 Hw carrier pins
// NonDeterministicTsc) plus the reused marks_hot_path marker.
//
// Concrete bug this catches: dropping the hw_at_or_above_v term from
// V201_OK would let a hot-path function read rdtsc, silently
// re-introducing the serializing-instruction stall on the recording path.
//
// Expected diagnostic substring: "V201:".

#include <crucible/safety/Fn.h>
#include <crucible/safety/Hw.h>

namespace fn = crucible::safety::fn;
namespace sf = crucible::safety;
using HW = crucible::algebra::lattices::HwInstruction;

namespace neg_collision_v201 {
using Bad = fn::Fn<sf::Hw<HW::NonDeterministicTsc, int>>;  // Hw tier >= NonDetTsc
}  // namespace neg_collision_v201

// Mark Bad as hot-path — required to fire V201.
namespace crucible::safety::fn::collision {
    template <> struct marks_hot_path<::neg_collision_v201::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_v201::Bad the_fixture{};

int main() { return 0; }
