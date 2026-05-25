// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule V402 (FIXY-FOUND-073 nested-cross-trunk leg):
//
//     scope_tier_of<F::type_t>     pins MemoryScope S_outer
//   ∧ inner_scope_tier_of<F::type_t> pins MemoryScope S_inner (NESTED)
//   ∧ scopes_cross_trunk(S_outer, S_inner)  (one accel + one ARM, no shared)
//   ⇒ ill-formed
//
// Plain English: a binding whose type composes TWO `ScopedFence` layers
// from contradicting MemoryScope trunks (one accel = `.warp/.cta/.gpu`,
// one ARM = `ish/osh`) has no coherent realization on any host.  PTX
// device scopes do not appear in ARM-DMB code; ARM shareability domains
// do not appear in PTX.  The MemoryScopeLattice cross-trunk leq is
// false in both directions, so the type itself contains the
// contradiction — independent of host arch.
//
// Gap closed by FOUND-073: V402 previously read only the OUTER scope
// against host arch (`scope_arch_cross_trunk_v`) OR the grant-driven
// marker.  Neither detected `ScopedFence<Gpu, ScopedFence<Inner, T>>` —
// the binding could pin host to ANYTHING (or leave it Portable) and
// V402 would not fire because the outer trunk vs host need not be
// contradictory by itself.  The new `nested_scope_cross_trunk_v` leg
// catches the within-type contradiction directly.
//
// Mismatch class: scope-TRUNK × scope-TRUNK contradiction WITHIN
// F::type_t (no host arch in play — Source defaults to FromInternal,
// which arch_pin_v maps to Portable; outer trunk vs Portable host is
// trivially coherent).  Distinct from the host-vs-outer leg above
// (neg_collision_V402_scope_arch.cpp).
//
// PRODUCTION-PATH: this instantiates a real `fn::Fn<...>` (Source
// defaulted), so Fn's own `static_assert(ValidComposition<Fn>)` runs
// the validate() leg — the concept layer ALONE does not gate Fn<>
// instantiation (feedback_collision_catalog_dual_wiring).
//
// Expected diagnostic substring: "V402:".

#include <crucible/safety/BarrierGuarded.h>
#include <crucible/safety/Fn.h>
#include <crucible/safety/ScopedFence.h>

namespace fn  = crucible::safety::fn;
namespace sf  = crucible::safety;
using MS = crucible::algebra::lattices::MemoryScope;
using BS = crucible::algebra::lattices::BarrierStrength;

namespace neg_collision_v402_nested {
// Outer fence: Gpu device scope (accel trunk).
// Inner fence: Inner ARM-shareability scope (ARM trunk).
// Both AcqRel-fenced so V401 (under-fenced cross-CTA-or-wider) stays
// satisfied; V402's new nested-cross-trunk leg is the FIRST and only
// failure — proving the rule is independently load-bearing.
//
// All Fn params take defaults except Type — Source defaults to
// FromInternal (arch_pin = Portable), so the OLD host-vs-outer leg
// is SILENT here.  V402 fires ONLY because of the new
// nested_scope_cross_trunk_v leg added in FOUND-073.
using Bad = fn::Fn<
    sf::BarrierGuarded<BS::AcqRel,
        sf::ScopedFence<MS::Gpu,                       // ← outer accel
            sf::ScopedFence<MS::Inner, int>>>>;        // ← inner ARM
}  // namespace neg_collision_v402_nested

[[maybe_unused]] neg_collision_v402_nested::Bad the_fixture{};

int main() { return 0; }
