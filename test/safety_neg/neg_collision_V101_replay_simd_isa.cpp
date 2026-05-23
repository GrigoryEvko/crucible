// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule V101 (FIXY-V-260):
//
//     marks_replay_required<F>::value == true
//   ∧ F::type_t is SimdWidthPinned<W> for a W ∉ {Scalar, Portable}
//   ⇒ ill-formed
//
// Plain English: AVX-512 and NEON have different lane counts, so the
// same IR produces a different FP-reduction tree and the bit pattern
// diverges across the cross-vendor CI matrix (CLAUDE.md DetSafe: FP
// reductions reorder under chunked fold).  Scalar (no SIMD) and Portable
// (⊤, identical on every ISA) are the only replay-safe poles.
//
// Mismatch class: replay marker × SimdWidthPinned pinned to a specific
// vector ISA (Avx2).  Uses the WRAPPER-TIER trigger path (the shipped
// V-256 SimdWidthPinned carrier) — distinct from the Hw / Barrier
// fixtures.
//
// Concrete bug this catches: dropping the
// simd_isa_pins_specific_vector_v term from V101_OK would let a
// replay-required body pin a vector ISA, breaking bit-exact replay
// across reincarnation hardware with a different lane width.
//
// Expected diagnostic substring: "V101:".

#include <crucible/safety/Fn.h>
#include <crucible/safety/SimdWidthPinned.h>

namespace fn = crucible::safety::fn;
namespace sf = crucible::safety;
using SI = crucible::algebra::lattices::SimdIsa;

namespace neg_collision_v101 {
using Bad = fn::Fn<sf::SimdWidthPinned<SI::Avx2, int>>;  // specific vector ISA
}  // namespace neg_collision_v101

// Mark Bad as replay-required — required to fire V101.
namespace crucible::safety::fn::collision {
    template <> struct marks_replay_required<::neg_collision_v101::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_v101::Bad the_fixture{};

int main() { return 0; }
