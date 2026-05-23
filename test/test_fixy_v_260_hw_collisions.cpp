// FIXY-V-260 sentinel TU: CollisionCatalog hardware-axis cross-axis rules.
//
// V-260 ships 8 NEW collision-catalog entries (V001/V002/V101/V102/V201/
// V202/V203/V301) atop the V-254/255/256 hardware-band Graded carriers
// (Hw / BarrierGuarded / SimdWidthPinned) and the V-258/259 vendor /
// simd grants.  Each rule gates a cross-axis composition where a
// hardware declaration is unsound against another Fn axis.  Five read a
// SHIPPED V-254/255/256 wrapper tier off F::type_t (V101 / V201 / V202 /
// V203 / V301 — triggerable today), three read an opt-in marker trait
// the V-258/259/261 grant-pack analysis specializes (V001 / V002 / V102
// — default-SAFE until a grant opts in).
//
// The task's Agent-11 §3.6 labels (V001/V002/S001/S002/H001/H002/H003/
// B001) re-home onto the FREE V band because S001/H001/H002/H003/B001
// already name shipped V-243 / Phase-B / Bg rules.  Number ranges encode
// the sub-axis like the F-family: Vendor V0xx, SimdIsa V1xx,
// HwInstruction V2xx, BarrierStrength V3xx.
//
// Sentinel witnesses:
//   (a) catalog cardinality floor (>= 44) + 8 rule_bijection cells.
//   (b) CollisionDiagnosticByRule<F, X>::rule_code() string identity.
//   (c) hw_tier_of / barrier_tier_of / simd_isa_of detectors +
//       hw_at_or_above_v / barrier_at_or_above_v ceiling predicates +
//       simd_isa_pins_specific_vector_v — positive, negative,
//       CV-piercing, non-wrapper-falls-back cells.
//   (d) MOCK-F concept firing: each V*_OK rejects a hand-built probe that
//       trips it (NOT a csfn::Fn, so Fn's own ValidComposition does not
//       pre-empt the concept).  This proves each _OK concept is itself
//       load-bearing — a refactor dropping it from AllRulesOK reds here.
//   (e) POSITIVE compositions PASS — a hardware wrapper below the floor,
//       or a replay-safe SIMD pole, trips no rule.
//   (f) NEGATIVE production-path compositions covered by the 8 HS14
//       neg-compile fixtures (one per rule); see the closing note.

#include <crucible/safety/BarrierGuarded.h>
#include <crucible/safety/Fn.h>             // pulls the CollisionCatalog body
#include <crucible/safety/Hw.h>
#include <crucible/safety/SimdWidthPinned.h>

#include <string_view>
#include <type_traits>

namespace cs   = ::crucible::safety;
namespace csfn = ::crucible::safety::fn;
namespace csc  = ::crucible::safety::fn::collision;
namespace eff  = ::crucible::effects;
using HW  = ::crucible::algebra::lattices::HwInstruction;
using BS  = ::crucible::algebra::lattices::BarrierStrength;
using SI  = ::crucible::algebra::lattices::SimdIsa;

namespace {

// ── (a) Catalog cardinality + bijection witnesses ──────────────────
static_assert(csc::catalog_size >= 44,
              "FIXY-V-260 floor: catalog must include V001..V301 (8 rules)");
static_assert(csc::rule_bijection_v<csc::RuleCode::V001>);
static_assert(csc::rule_bijection_v<csc::RuleCode::V002>);
static_assert(csc::rule_bijection_v<csc::RuleCode::V101>);
static_assert(csc::rule_bijection_v<csc::RuleCode::V102>);
static_assert(csc::rule_bijection_v<csc::RuleCode::V201>);
static_assert(csc::rule_bijection_v<csc::RuleCode::V202>);
static_assert(csc::rule_bijection_v<csc::RuleCode::V203>);
static_assert(csc::rule_bijection_v<csc::RuleCode::V301>);

// ── (b) Diagnostic-string identity ─────────────────────────────────
using DefaultFn = csfn::Fn<int>;
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::V001>::rule_code()
              == std::string_view{"V001"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::V101>::rule_code()
              == std::string_view{"V101"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::V201>::rule_code()
              == std::string_view{"V201"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::V202>::rule_code()
              == std::string_view{"V202"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::V301>::rule_code()
              == std::string_view{"V301"});

// ── (c) Hw / BarrierGuarded / SimdWidthPinned tier detectors ───────
using HwScalar   = cs::Hw<HW::Scalar, int>;
using HwVec      = cs::Hw<HW::Vectorizable, int>;
using HwTsc      = cs::Hw<HW::NonDeterministicTsc, int>;
using HwMsr      = cs::Hw<HW::PrivilegedMsr, int>;
using BarNone    = cs::BarrierGuarded<BS::None, int>;
using BarAcqRel  = cs::BarrierGuarded<BS::AcqRel, int>;
using BarSeqCst  = cs::BarrierGuarded<BS::SeqCst, int>;
using BarFence   = cs::BarrierGuarded<BS::FullFence, int>;
using SimdScalar = cs::SimdWidthPinned<SI::Scalar, int>;
using SimdAvx2   = cs::SimdWidthPinned<SI::Avx2, int>;
using SimdPort   = cs::SimdWidthPinned<SI::Portable, int>;

// has_* discrimination + value extraction.
static_assert(csc::hw_tier_of<HwTsc>::has_hw);
static_assert(csc::hw_tier_of<HwTsc>::value == HW::NonDeterministicTsc);
static_assert(!csc::hw_tier_of<int>::has_hw);
static_assert(!csc::hw_tier_of<BarSeqCst>::has_hw);   // wrong wrapper family
static_assert(csc::barrier_tier_of<BarSeqCst>::has_barrier);
static_assert(csc::barrier_tier_of<BarSeqCst>::value == BS::SeqCst);
static_assert(!csc::barrier_tier_of<int>::has_barrier);
static_assert(csc::simd_isa_of<SimdAvx2>::has_simd);
static_assert(csc::simd_isa_of<SimdAvx2>::value == SI::Avx2);
static_assert(!csc::simd_isa_of<int>::has_simd);

// CV / reference piercing — a wrapped return is just as load-bearing.
static_assert(csc::hw_tier_of<HwTsc const&>::has_hw);
static_assert(csc::barrier_tier_of<BarSeqCst&>::value == BS::SeqCst);
static_assert(csc::simd_isa_of<SimdAvx2 const>::value == SI::Avx2);

// hw_at_or_above_v ceiling — NonDeterministicTsc floor (V201/V203).
static_assert(!csc::hw_at_or_above_v<HW::NonDeterministicTsc, HwScalar>);  // below
static_assert(!csc::hw_at_or_above_v<HW::NonDeterministicTsc, HwVec>);     // below
static_assert(csc::hw_at_or_above_v<HW::NonDeterministicTsc, HwTsc>);      // == floor
static_assert(csc::hw_at_or_above_v<HW::NonDeterministicTsc, HwMsr>);      // above
static_assert(!csc::hw_at_or_above_v<HW::NonDeterministicTsc, int>);       // no wrapper

// barrier_at_or_above_v ceiling — SeqCst floor (V301).
static_assert(!csc::barrier_at_or_above_v<BS::SeqCst, BarNone>);
static_assert(!csc::barrier_at_or_above_v<BS::SeqCst, BarAcqRel>);
static_assert(csc::barrier_at_or_above_v<BS::SeqCst, BarSeqCst>);          // == floor
static_assert(csc::barrier_at_or_above_v<BS::SeqCst, BarFence>);           // above
static_assert(!csc::barrier_at_or_above_v<BS::SeqCst, int>);               // no wrapper

// simd_isa_pins_specific_vector_v — Scalar/Portable are replay-safe poles.
static_assert(!csc::simd_isa_pins_specific_vector_v<SimdScalar>);          // ⊥ pole
static_assert(!csc::simd_isa_pins_specific_vector_v<SimdPort>);            // ⊤ pole
static_assert(csc::simd_isa_pins_specific_vector_v<SimdAvx2>);             // specific ISA
static_assert(!csc::simd_isa_pins_specific_vector_v<int>);                 // no wrapper

// ── (d) MOCK-F concept firing — each V*_OK rejects a tripping probe ─
//
// These probes are NOT csfn::Fn, so Fn's instantiation-time
// static_assert(ValidComposition) does not pre-empt the concept (the
// reason the V-243 sentinel could only assert positives — see (f)).
// Here the concept's own logic is the witness.
struct MockReplaySimd  { using type_t = SimdAvx2;  using effect_row_t = eff::Row<>; };
struct MockHotTsc      { using type_t = HwTsc;     using effect_row_t = eff::Row<>; };
struct MockMsrNoInit   { using type_t = HwMsr;     using effect_row_t = eff::Row<>; };
struct MockReplayTsc   { using type_t = HwTsc;     using effect_row_t = eff::Row<>; };
struct MockHotFence    { using type_t = BarFence;  using effect_row_t = eff::Row<>; };
struct MockVendorMix   { using type_t = int;       using effect_row_t = eff::Row<>; };
struct MockCrossArch   { using type_t = int;       using effect_row_t = eff::Row<>; };
struct MockWidthOver   { using type_t = int;       using effect_row_t = eff::Row<>; };

}  // namespace

// Specialize the firing markers / hot-path on the probes (csc namespace).
namespace crucible::safety::fn::collision {
    template <> struct marks_replay_required<::MockReplaySimd>      : std::true_type {};
    template <> struct marks_hot_path<::MockHotTsc>                 : std::true_type {};
    template <> struct marks_replay_required<::MockReplayTsc>       : std::true_type {};
    template <> struct marks_hot_path<::MockHotFence>               : std::true_type {};
    template <> struct marks_vendor_isa_inconsistent<::MockVendorMix> : std::true_type {};
    template <> struct marks_vendor_cross_arch<::MockCrossArch>     : std::true_type {};
    template <> struct marks_simd_width_exceeds_isa<::MockWidthOver>: std::true_type {};
}  // namespace crucible::safety::fn::collision

namespace {

// Type-readable rules fire on the probe.
static_assert(!csc::V101_OK<MockReplaySimd>);   // replay × specific vector ISA
static_assert(!csc::V201_OK<MockHotTsc>);       // hot-path × Hw ≥ NonDetTsc
static_assert(!csc::V202_OK<MockMsrNoInit>);    // PrivilegedMsr without Init row
static_assert(!csc::V203_OK<MockReplayTsc>);    // replay × Hw ≥ NonDetTsc
static_assert(!csc::V301_OK<MockHotFence>);     // hot-path × Barrier ≥ SeqCst
// Marker rules fire on the probe.
static_assert(!csc::V001_OK<MockVendorMix>);    // inconsistent vendor pack
static_assert(!csc::V002_OK<MockCrossArch>);    // x86 + ARM in one binding
static_assert(!csc::V102_OK<MockWidthOver>);    // width exceeds ISA family

// V202 PASSES when the row carries Init (the privileged-setup context).
struct MockMsrWithInit { using type_t = HwMsr; using effect_row_t = eff::Row<eff::Effect::Init>; };
static_assert(csc::V202_OK<MockMsrWithInit>);

// ── (e) POSITIVE compositions — these MUST NOT trip any rule ───────
//
// V-260 REJECTS exactly 8 combinations; everything else continues to
// pass.  Witnessing positives prevents a false positive in validate().

// (e1) Hw below the NonDetTsc floor: V201/V203 never fire (no marker,
// below floor anyway).
using NeutralHwVec = csfn::Fn<HwVec>;
static_assert(csfn::ValidComposition<NeutralHwVec>);
static_assert(csc::first_failure_v<NeutralHwVec> == csc::RuleCode::None);

// (e2) BarrierGuarded<AcqRel> is hot-path-safe (below SeqCst floor); the
// bare carrier passes.
using NeutralBarAcqRel = csfn::Fn<BarAcqRel>;
static_assert(csfn::ValidComposition<NeutralBarAcqRel>);
static_assert(csc::first_failure_v<NeutralBarAcqRel> == csc::RuleCode::None);

// (e3) SimdWidthPinned<Scalar> and <Portable> are the two replay-safe
// poles — V101 never fires (no specific vector ISA).
using NeutralSimdScalar = csfn::Fn<SimdScalar>;
using NeutralSimdPort    = csfn::Fn<SimdPort>;
static_assert(csfn::ValidComposition<NeutralSimdScalar>);
static_assert(csfn::ValidComposition<NeutralSimdPort>);

// (e4) A bare SimdWidthPinned<Avx2> WITHOUT the replay marker passes —
// V101 needs marks_replay_required, which a default Fn does not carry.
using NeutralSimdAvx2 = csfn::Fn<SimdAvx2>;
static_assert(csfn::ValidComposition<NeutralSimdAvx2>);
static_assert(csc::first_failure_v<NeutralSimdAvx2> == csc::RuleCode::None);

// (e5) A bare Hw<NonDeterministicTsc> WITHOUT the hot-path / replay
// marker passes — V201/V203 both need a marker.
using NeutralHwTsc = csfn::Fn<HwTsc>;
static_assert(csfn::ValidComposition<NeutralHwTsc>);

// (e6) Plain int carrier — none of the hardware-axis rules touch a
// non-wrapper, non-marked Fn.
static_assert(csfn::ValidComposition<DefaultFn>);
static_assert(csc::first_failure_v<DefaultFn> == csc::RuleCode::None);

// ── (f) NEGATIVE production-path compositions — 8 HS14 neg fixtures ─
//
// A real csfn::Fn carrier that genuinely trips rule X cannot be asserted
// positively here: instantiating it runs Fn<>'s own
// static_assert(ValidComposition), firing validate() before
// first_failure_v can be read.  HS14 covers this with 8 neg-compile
// fixtures in test/safety_neg/, one per rule, distinct mismatch classes:
//   neg_collision_V201_hotpath_nondet_tsc.cpp   (hot-path × Hw tier, type)
//   neg_collision_V202_privileged_msr_no_init.cpp (PrivilegedMsr × row, type+row)
//   neg_collision_V203_replay_nondet_tsc.cpp     (replay × Hw tier, type)
//   neg_collision_V301_hotpath_full_fence.cpp    (hot-path × Barrier tier, type)
//   neg_collision_V101_replay_simd_isa.cpp       (replay × SimdWidthPinned, type)
//   neg_collision_V001_vendor_isa_inconsistent.cpp (marker, concept-direct)
//   neg_collision_V002_vendor_cross_arch.cpp     (marker, concept-direct)
//   neg_collision_V102_simd_width_exceeds_isa.cpp (marker, concept-direct)
// Each fixture's CMake stanza greps the per-rule diagnostic substring; a
// refactor that relaxes the rule turns the fixture RED→GREEN and ctest
// reports failure.

}  // namespace

int main() {
    // V-260 is pure compile-time discipline; the static_asserts above
    // exercise every load-bearing surface.  main() satisfies the
    // executable-link requirement ctest expects from the test target.
    return 0;
}
