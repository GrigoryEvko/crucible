// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule V301 (FIXY-V-260):
//
//     marks_hot_path<F>::value == true
//   ∧ F::type_t carries a BarrierStrength tier >= SeqCst
//   ⇒ ill-formed
//
// Plain English: a full fence (mfence / lock-prefixed) drains the store
// buffer (≈ 20-40+ cycles) — CLAUDE.md §IX mandates acquire/release
// only on the hot path (free on x86 TSO).
//
// Mismatch class: hot-path marker × BarrierStrength tier >= SeqCst.
// Uses the WRAPPER-TIER trigger path (the shipped V-255 BarrierGuarded
// carrier pins FullFence) — distinct from the Hw-axis and SimdIsa-axis
// fixtures.
//
// Concrete bug this catches: dropping the barrier_at_or_above_v term
// from V301_OK would let a hot-path function emit a full memory fence,
// silently re-introducing the store-buffer-drain stall.
//
// Expected diagnostic substring: "V301:".

#include <crucible/safety/BarrierGuarded.h>
#include <crucible/safety/Fn.h>

namespace fn = crucible::safety::fn;
namespace sf = crucible::safety;
using BS = crucible::algebra::lattices::BarrierStrength;

namespace neg_collision_v301 {
using Bad = fn::Fn<sf::BarrierGuarded<BS::FullFence, int>>;  // tier >= SeqCst
}  // namespace neg_collision_v301

// Mark Bad as hot-path — required to fire V301.
namespace crucible::safety::fn::collision {
    template <> struct marks_hot_path<::neg_collision_v301::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_v301::Bad the_fixture{};

int main() { return 0; }
