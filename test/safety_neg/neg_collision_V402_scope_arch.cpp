// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule V402 (FIXY-V-268):
//
//     scope_tier_of<F::type_t> pins an accel (GPU device) scope
//   ∧ arch_pin_v<F::source_t> == ArchTag::Arm   (a CPU-host arch pin)
//   ⇒ ill-formed
//
// Plain English: a PTX `.gpu`-scope fence pinned to an ARM-DMB host
// (source::ArchPinned<Arm>) is cross-trunk incoherent — the ARM fence
// dialect (DMB ISH/OSH/SY) cannot realize a GPU device scope, and the
// MemoryScopeLattice marks the accel and ARM trunks mutually incomparable.
// The MemoryScope-axis mirror of V002's cross-arch intrinsic mixing.
//
// Mismatch class: scope-TRUNK × host-ARCH contradiction, read from
// F::type_t (the ScopedFence trunk) AGAINST F::source_t (arch_pin_v).
// Distinct from V401, which rejects an under-fenced device-scope
// publication on the SAME type_t.  The device scope here is correctly
// AcqRel-fenced (BarrierGuarded<AcqRel, ...>) precisely so V401 stays
// satisfied and V402 is the FIRST and only failure — proving the rule is
// independently load-bearing.
//
// PRODUCTION-PATH: this instantiates a real `fn::Fn<...>` (Source pinned to
// source::ArmPinned), so Fn's own `static_assert(ValidComposition<Fn>)`
// runs the validate() leg — the concept layer ALONE does not gate Fn<>
// instantiation (feedback_collision_catalog_dual_wiring).
//
// Expected diagnostic substring: "V402:".

#include <crucible/safety/BarrierGuarded.h>
#include <crucible/safety/Fn.h>
#include <crucible/safety/ScopedFence.h>
#include <crucible/safety/source/Arch.h>

namespace fn  = crucible::safety::fn;
namespace sf  = crucible::safety;
namespace src = crucible::safety::source;
using MS = crucible::algebra::lattices::MemoryScope;
using BS = crucible::algebra::lattices::BarrierStrength;

namespace neg_collision_v402 {
// A correctly-AcqRel-fenced Gpu-device-scope value, but the binding is
// pinned to an ARM CPU host — V401 is satisfied, V402 fires on the
// cross-trunk scope×arch contradiction.  All Fn params but Source take the
// Fn<Type> defaults (Refinement..Lifetime); only Source = ArmPinned.
using Bad = fn::Fn<
    sf::BarrierGuarded<BS::AcqRel, sf::ScopedFence<MS::Gpu, int>>,
    fn::pred::True,
    fn::UsageMode::Linear,
    crucible::effects::Row<>,
    fn::SecLevel::Classified,
    fn::proto::None,
    fn::lifetime::Static,
    src::ArmPinned>;
}  // namespace neg_collision_v402

[[maybe_unused]] neg_collision_v402::Bad the_fixture{};

int main() { return 0; }
