// FIXY-V-268 sentinel TU: CollisionCatalog memory-scope cross-axis rules.
//
// V-268 ships 2 NEW collision-catalog entries (V401 / V402) atop the V-265
// MemoryScopeLattice + the V-267 ScopedFence<Scope, T> carrier, dual-wired
// per feedback_collision_catalog_dual_wiring: BOTH the V40x_OK concept
// (folded into AllRulesOK + first_failure) AND the independent
// CollisionRules<F>::validate() static_assert leg carry each rule — the
// concept alone does NOT gate Fn<> instantiation.
//
//   V401: scope ⊒ Gpu (device / system visibility) × BarrierStrength
//         ⊏ AcqRel — a device-or-wider publication under-fenced; reads the
//         ScopedFence tier AND the BarrierGuarded tier from one composed
//         F::type_t (the two detectors pierce each other).
//   V402: scope-trunk × host-arch cross-trunk — an accel (GPU device) scope
//         on a CPU-host arch pin, or an ARM-shareability scope on a non-ARM
//         host.  Reads the ScopedFence trunk (V-265 mem_scope_is_accel /
//         mem_scope_is_arm) against arch_pin_v<F::source_t> (V-261); the
//         marks_scope_arch_cross_trunk marker is the grant-driven path.
//
// Sentinel witnesses (mirroring the V-260 sentinel):
//   (a) catalog cardinality floor (>= 47) + 2 rule_bijection cells.
//   (b) CollisionDiagnosticByRule<F, X>::rule_code() string identity.
//   (c) scope_tier_of detector + scope_at_or_above_v ceiling +
//       scope_contradicts_host_arch truth table + the MUTUAL piercing
//       (barrier_tier_of sees through ScopedFence; scope_tier_of sees
//       through BarrierGuarded / Hw / SimdWidthPinned).
//   (d) MOCK-F concept firing: each V40x_OK rejects a hand-built probe that
//       trips it (NOT a csfn::Fn, so Fn's own ValidComposition does not
//       pre-empt the concept) — proves each _OK concept is load-bearing.
//   (e) POSITIVE compositions PASS — a Cta-scope fence with None barrier, an
//       AcqRel-fenced device-scope value, or a coherent scope×arch pin trips
//       no rule.
//   (f) NEGATIVE production-path compositions covered by the 2 HS14
//       neg-compile fixtures (one per rule, distinct mismatch class).

#include <crucible/safety/BarrierGuarded.h>
#include <crucible/safety/Fn.h>             // pulls the CollisionCatalog body
#include <crucible/safety/Hw.h>
#include <crucible/safety/ScopedFence.h>
#include <crucible/safety/SimdWidthPinned.h>
#include <crucible/safety/source/Arch.h>

#include <string_view>
#include <type_traits>

namespace cs   = ::crucible::safety;
namespace csfn = ::crucible::safety::fn;
namespace csc  = ::crucible::safety::fn::collision;
namespace eff  = ::crucible::effects;
namespace src  = ::crucible::safety::source;
using MS  = ::crucible::algebra::lattices::MemoryScope;
using BS  = ::crucible::algebra::lattices::BarrierStrength;
using HW  = ::crucible::algebra::lattices::HwInstruction;
using SI  = ::crucible::algebra::lattices::SimdIsa;
using AT  = ::crucible::safety::source::ArchTag;

namespace {

// ── (a) Catalog cardinality + bijection witnesses ──────────────────
static_assert(csc::catalog_size >= 47,
              "FIXY-V-268 floor: catalog must include V401 + V402 (2 rules)");
static_assert(csc::rule_bijection_v<csc::RuleCode::V401>);
static_assert(csc::rule_bijection_v<csc::RuleCode::V402>);

// ── (b) Diagnostic-string identity ─────────────────────────────────
using DefaultFn = csfn::Fn<int>;
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::V401>::rule_code()
              == std::string_view{"V401"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::V402>::rule_code()
              == std::string_view{"V402"});

// ── (c) scope_tier_of detector + ceiling + arch-contradiction table ─
using FenceGpu     = cs::ScopedFence<MS::Gpu,    int>;
using FenceCta     = cs::ScopedFence<MS::Cta,    int>;
using FenceSystem  = cs::ScopedFence<MS::System, int>;
using FenceInner   = cs::ScopedFence<MS::Inner,  int>;
using FenceThread  = cs::ScopedFence<MS::Thread, int>;
using BarNone      = cs::BarrierGuarded<BS::None,   int>;
using BarAcqRel    = cs::BarrierGuarded<BS::AcqRel, int>;

// has_scope discrimination + value extraction.
static_assert(csc::scope_tier_of<FenceGpu>::has_scope);
static_assert(csc::scope_tier_of<FenceGpu>::value == MS::Gpu);
static_assert(csc::scope_tier_of<FenceInner>::value == MS::Inner);
static_assert(!csc::scope_tier_of<int>::has_scope);
static_assert(!csc::scope_tier_of<BarNone>::has_scope);   // bare barrier carries no scope

// CV / reference piercing.
static_assert(csc::scope_tier_of<FenceGpu const&>::has_scope);
static_assert(csc::scope_tier_of<FenceGpu const>::value == MS::Gpu);

// scope_tier_of pierces the sibling hardware-band wrappers so V401 finds the
// scope inside a barrier-outer (or hw-outer) nesting.
static_assert(csc::scope_tier_of<cs::BarrierGuarded<BS::None, FenceGpu>>::has_scope);
static_assert(csc::scope_tier_of<cs::BarrierGuarded<BS::None, FenceGpu>>::value == MS::Gpu);
static_assert(csc::scope_tier_of<cs::Hw<HW::Scalar, FenceCta>>::value == MS::Cta);
static_assert(csc::scope_tier_of<cs::SimdWidthPinned<SI::Avx2, FenceInner>>::value == MS::Inner);

// barrier_tier_of pierces a ScopedFence-outer nesting (the dual) — the V401
// detectors are mutually piercing, so the rule is wrapper-order-independent.
static_assert(csc::barrier_tier_of<cs::ScopedFence<MS::Gpu, BarAcqRel>>::has_barrier);
static_assert(csc::barrier_tier_of<cs::ScopedFence<MS::Gpu, BarAcqRel>>::value == BS::AcqRel);
// A bare BarrierGuarded still matches the BarrierGuarded spec (no regression).
static_assert(csc::barrier_tier_of<BarAcqRel>::value == BS::AcqRel);

// scope_at_or_above_v<Gpu, T> — only Gpu and System (device-or-wider) satisfy;
// Cta / Cluster / Warp and every ARM-trunk scope do NOT (cross-trunk leq is false).
static_assert(csc::scope_at_or_above_v<MS::Gpu, FenceGpu>);          // == floor
static_assert(csc::scope_at_or_above_v<MS::Gpu, FenceSystem>);       // ⊤ above floor
static_assert(!csc::scope_at_or_above_v<MS::Gpu, FenceCta>);         // below (block scope)
static_assert(!csc::scope_at_or_above_v<MS::Gpu, FenceInner>);       // cross-trunk (ARM)
static_assert(!csc::scope_at_or_above_v<MS::Gpu, FenceThread>);      // ⊥
static_assert(!csc::scope_at_or_above_v<MS::Gpu, int>);              // no wrapper
// Reads through a barrier-outer nesting too (V401's composed-type case).
static_assert(csc::scope_at_or_above_v<MS::Gpu, cs::BarrierGuarded<BS::None, FenceGpu>>);

// scope_contradicts_host_arch truth table.
static_assert(csc::scope_contradicts_host_arch(MS::Gpu,   AT::Arm));      // GPU scope on ARM host
static_assert(csc::scope_contradicts_host_arch(MS::Gpu,   AT::X86));      // GPU scope on x86 host
static_assert(!csc::scope_contradicts_host_arch(MS::Gpu,  AT::Portable)); // no concrete host pin
static_assert(csc::scope_contradicts_host_arch(MS::Cta,   AT::Arm));      // accel scope on CPU host
static_assert(csc::scope_contradicts_host_arch(MS::Inner, AT::X86));      // ARM scope on x86 host
static_assert(!csc::scope_contradicts_host_arch(MS::Inner, AT::Arm));     // ARM scope on ARM host (OK)
static_assert(!csc::scope_contradicts_host_arch(MS::Inner, AT::Portable));
static_assert(!csc::scope_contradicts_host_arch(MS::Thread, AT::X86));    // ⊥ sentinel: any host
static_assert(!csc::scope_contradicts_host_arch(MS::System, AT::X86));    // ⊤ sentinel: any host
static_assert(!csc::scope_contradicts_host_arch(MS::System, AT::Arm));

// ── (d) MOCK-F concept firing — each V40x_OK rejects a tripping probe ─
//
// Probes are NOT csfn::Fn, so Fn's instantiation-time
// static_assert(ValidComposition) does not pre-empt the concept; the
// concept's own logic is the witness.  Each carries type_t + source_t +
// effect_row_t to match the F-shape the detectors read.
struct MockV401Under {                                    // scope Gpu × barrier None
    using type_t       = cs::BarrierGuarded<BS::None, FenceGpu>;
    using source_t     = src::PortablePinned;
    using effect_row_t = eff::Row<>;
};
struct MockV402Accel {                                    // GPU scope on ARM host
    using type_t       = cs::BarrierGuarded<BS::AcqRel, FenceGpu>;  // AcqRel so V401 stays clear
    using source_t     = src::ArmPinned;
    using effect_row_t = eff::Row<>;
};
struct MockV402Arm {                                      // ARM scope on x86 host
    using type_t       = FenceInner;
    using source_t     = src::X86Pinned;
    using effect_row_t = eff::Row<>;
};
struct MockV402Marker {                                   // grant-driven nested-cross-trunk
    using type_t       = int;
    using source_t     = src::PortablePinned;
    using effect_row_t = eff::Row<>;
};
// Coherent positives — these MUST satisfy both concepts.
struct MockScopeOk {                                      // AcqRel-fenced Gpu scope, no arch pin
    using type_t       = cs::BarrierGuarded<BS::AcqRel, FenceGpu>;
    using source_t     = src::PortablePinned;
    using effect_row_t = eff::Row<>;
};
struct MockArmOk {                                        // ARM scope on ARM host
    using type_t       = FenceInner;
    using source_t     = src::ArmPinned;
    using effect_row_t = eff::Row<>;
};

}  // namespace

namespace crucible::safety::fn::collision {
    template <> struct marks_scope_arch_cross_trunk<::MockV402Marker> : std::true_type {};
}  // namespace crucible::safety::fn::collision

namespace {

// V401 fires on the under-fenced device-scope probe; passes on the AcqRel one.
static_assert(!csc::V401_OK<MockV401Under>);
static_assert( csc::V401_OK<MockScopeOk>);
static_assert( csc::V401_OK<MockV402Accel>);   // AcqRel barrier — V401 clear, only V402 fires
static_assert( csc::V401_OK<MockV402Arm>);      // Inner scope not ⊒ Gpu — V401 never applies

// V402 fires on the type-readable cross-trunk probes AND the marker probe;
// passes on the coherent ones.
static_assert(!csc::V402_OK<MockV402Accel>);    // GPU scope × ArchPinned<Arm>
static_assert(!csc::V402_OK<MockV402Arm>);      // ARM scope × ArchPinned<X86>
static_assert(!csc::V402_OK<MockV402Marker>);   // grant-driven marker
static_assert( csc::V402_OK<MockScopeOk>);      // Portable host — no contradiction
static_assert( csc::V402_OK<MockArmOk>);        // ARM scope on ARM host — coherent
static_assert( csc::V402_OK<MockV401Under>);    // Portable host — V402 does not apply

// scope_arch_cross_trunk_v (the type-readable V402 leg) reads type_t + source_t.
static_assert(csc::scope_arch_cross_trunk_v<MockV402Accel>);
static_assert(csc::scope_arch_cross_trunk_v<MockV402Arm>);
static_assert(!csc::scope_arch_cross_trunk_v<MockScopeOk>);
static_assert(!csc::scope_arch_cross_trunk_v<MockArmOk>);

// ── (e) POSITIVE compositions — these MUST NOT trip any rule ───────
//
// V-268 REJECTS exactly the two unsound shapes; everything else passes.

// (e1) A Cta (block) scope with a None barrier is fine — block-scope visibility
// does NOT require acquire-release; V401 fires only for device-or-wider scopes.
using NeutralCtaNone = csfn::Fn<cs::BarrierGuarded<BS::None, FenceCta>>;
static_assert(csfn::ValidComposition<NeutralCtaNone>);
static_assert(csc::first_failure_v<NeutralCtaNone> == csc::RuleCode::None);

// (e2) A device (Gpu) scope CORRECTLY fenced with AcqRel passes V401.
using NeutralGpuAcqRel = csfn::Fn<cs::BarrierGuarded<BS::AcqRel, FenceGpu>>;
static_assert(csfn::ValidComposition<NeutralGpuAcqRel>);
static_assert(csc::first_failure_v<NeutralGpuAcqRel> == csc::RuleCode::None);

// (e3) A bare device-scope fence with no host arch pin (default source →
// Portable) trips no V402 — the GPU scope is coherent with a portable host.
using NeutralGpuFenced = csfn::Fn<cs::BarrierGuarded<BS::AcqRel, FenceGpu>>;
static_assert(csfn::ValidComposition<NeutralGpuFenced>);

// (e4) An ARM-shareability scope with a default (Portable) host pin passes V402.
using NeutralInner = csfn::Fn<FenceInner>;
static_assert(csfn::ValidComposition<NeutralInner>);
static_assert(csc::first_failure_v<NeutralInner> == csc::RuleCode::None);

// (e5) A System (⊤) scope fence is realizable on any host — never a cross-trunk.
using NeutralSystem = csfn::Fn<cs::BarrierGuarded<BS::AcqRel, FenceSystem>>;
static_assert(csfn::ValidComposition<NeutralSystem>);

// (e6) Plain int carrier — neither memory-scope rule touches a non-ScopedFence Fn.
static_assert(csfn::ValidComposition<DefaultFn>);
static_assert(csc::first_failure_v<DefaultFn> == csc::RuleCode::None);

// ── (f) NEGATIVE production-path compositions — 2 HS14 neg fixtures ─
//
// A real csfn::Fn carrier that genuinely trips V401 / V402 cannot be asserted
// positively here: instantiating it runs Fn<>'s own
// static_assert(ValidComposition), firing validate() before first_failure_v
// can be read.  HS14 covers this with 2 neg-compile fixtures in
// test/safety_neg/, distinct mismatch classes (per feedback_collision_catalog
// _dual_wiring: a concept-layer neg ALONE does not gate Fn<> — both fixtures
// instantiate a real Fn so the validate() leg is exercised):
//   neg_collision_V401_scope_strength.cpp   (scope Gpu × barrier None, type-readable)
//   neg_collision_V402_scope_arch.cpp        (scope Gpu × ArchPinned<Arm>, type+source)

}  // namespace

int main() {
    // V-268 is pure compile-time discipline; the static_asserts above
    // exercise every load-bearing surface.  main() satisfies the
    // executable-link requirement ctest expects from the test target.
    return 0;
}
