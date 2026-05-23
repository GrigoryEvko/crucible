// FIXY-V-091 sentinel TU: CollisionCatalog F-family — FP-mode cross-axis rules.
//
// V-091 ships 5 NEW collision-catalog entries (F101..F105) atop the
// V-090 FpModePinned wrapper family.  Each rule guards a cross-axis
// composition where pinning a load-bearing toxic FP-mode value on a
// Replay-required or CT-typed Fn defeats the property that axis owns:
//
//   F101: Replay-required × FpReassociate<UnrestrictedRewrite>
//         Algebraic rewrite (-fassociative-math) reorders FP additions;
//         bit pattern diverges across vendors → bit-exact replay broken.
//
//   F102: Replay-required × FpContract<Fast>
//         Cross-statement FMA folding picks different boundaries per
//         vendor; same source → different bits in the CI matrix.
//
//   F103: CT × FpReassociate<UnrestrictedRewrite>
//         Reassociation introduces data-dependent reduction-tree
//         topology, violating CT timing independence.
//
//   F104: CT × FpDenormalInput<HonorDenormals>
//         DAZ=0 introduces 30-100× cycle delta when the input IS
//         denormal — textbook FP timing side-channel.
//
//   F105: CT × FpFtz<PreserveSubnormals>
//         Output-side dual of F104 — FTZ=0 introduces the same 30-100×
//         slowdown PRODUCING denormal outputs.  Result-magnitude leaks
//         through cycle count.
//
// Sentinel witnesses:
//   (a) catalog cardinality bumped 22 → 27 + 5 rule_bijection cells
//   (b) CollisionDiagnosticByRule<F, F10x>::rule_code() string identity
//   (c) wraps_fp_axis_mode<AxisMode, T> detector — true on matching
//       FpModePinned spelling, false on every off-axis / wrong-mode /
//       non-FP spelling (one detector covers all 11 sub-axes)
//   (d) POSITIVE compositions PASS — toxic FP wrapper WITHOUT the
//       marker, Forbidden / OnInExpr / DAZ / FlushToZero modes generally
//   (e) NEGATIVE compositions trip the expected RuleCode in
//       first_failure_v<F> — without instantiating ValidComposition
//       (which would hard-fail compilation via the static_assert chain
//       in validate()).
//
// HS14 floor met by 2 distinct neg-compile fixtures covering 2 distinct
// mismatch classes:
//   - neg_collision_f101_replay_fp_reassoc.cpp (Replay axis × Reassoc)
//   - neg_collision_f104_ct_fp_denormal_honored.cpp (CT axis × Denormal)

#include <crucible/safety/Fn.h>             // pulls CollisionCatalog body
#include <crucible/safety/FpMode.h>

#include <string_view>
#include <type_traits>

namespace cs   = ::crucible::safety;
namespace csfn = ::crucible::safety::fn;
namespace csc  = ::crucible::safety::fn::collision;

namespace {

// ── (a) Catalog cardinality + bijection witnesses ──────────────────
//
// V-091 extends the 22-entry catalog to 27 entries; V-234 appends M001
// for 28 total.  Use a floor (`>=`) on cardinality so future appends
// don't silently red this sentinel — the catalog_cardinality_test_drift
// pattern (feedback memory) prevents the silent-skip class of
// regression that strict equality assertions cause.  Per-rule
// rule_bijection assertions still pin each entry individually.
static_assert(csc::catalog_size >= 28,
              "FIXY-V-091 / FIXY-V-234 floor: catalog must include F101..F105 + M001");
static_assert(std::tuple_size_v<csc::Catalog> >= 28);
static_assert(csc::rule_bijection_v<csc::RuleCode::F101>);
static_assert(csc::rule_bijection_v<csc::RuleCode::F102>);
static_assert(csc::rule_bijection_v<csc::RuleCode::F103>);
static_assert(csc::rule_bijection_v<csc::RuleCode::F104>);
static_assert(csc::rule_bijection_v<csc::RuleCode::F105>);
static_assert(csc::rule_bijection_v<csc::RuleCode::M001>);

// ── (b) Diagnostic-string identity ─────────────────────────────────
//
// rule_code() returns the canonical "F10N" string for each new rule.
// Pins the diagnostic surface against accidental rename.
using DefaultFn = csfn::Fn<int>;
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::F101>::rule_code()
              == std::string_view{"F101"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::F102>::rule_code()
              == std::string_view{"F102"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::F103>::rule_code()
              == std::string_view{"F103"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::F104>::rule_code()
              == std::string_view{"F104"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::F105>::rule_code()
              == std::string_view{"F105"});

// ── (c) wraps_fp_axis_mode<> detector cells ────────────────────────
//
// The generic detector is partial-specialized on FpModePinned<Mode, U>
// with `auto AxisMode` constrained to bind to Mode's type.  Probing
// `<FpReassociate::UnrestrictedRewrite, FpRoundingPinned<...>>` must
// fall back to false_type because the inner FpRoundingPinned pins
// `Mode` of type FpRounding, not FpReassociate.  One detector → 11
// sub-axes.
using ReassocPermittedF  = cs::FpReassociatePinned<cs::FpReassociate::UnrestrictedRewrite, float>;
using ReassocPermittedD  = cs::FpReassociatePinned<cs::FpReassociate::UnrestrictedRewrite, double>;
using ReassocForbidden   = cs::FpReassociatePinned<cs::FpReassociate::Forbidden, float>;
using ContractFast       = cs::FpContractPinned<cs::FpContract::Fast, float>;
using ContractOff        = cs::FpContractPinned<cs::FpContract::Off, float>;
using FtzPreserved       = cs::FpFtzPinned<cs::FpFtz::PreserveSubnormals, float>;
using FtzFlushed         = cs::FpFtzPinned<cs::FpFtz::FlushToZero, float>;
using DenormalHonored    = cs::FpDenormalInputPinned<cs::FpDenormalInput::HonorDenormals, float>;
using DenormalDaz        = cs::FpDenormalInputPinned<cs::FpDenormalInput::DenormalsAreZero, float>;
using RoundingRTNE       = cs::FpRoundingPinned<cs::FpRounding::RoundToNearestEven, float>;

// Positive cells — detector fires.
static_assert(csc::wraps_fp_axis_mode_v<
                  cs::FpReassociate::UnrestrictedRewrite, ReassocPermittedF>);
static_assert(csc::wraps_fp_axis_mode_v<
                  cs::FpReassociate::UnrestrictedRewrite, ReassocPermittedD>);
static_assert(csc::wraps_fp_axis_mode_v<
                  cs::FpContract::Fast, ContractFast>);
static_assert(csc::wraps_fp_axis_mode_v<
                  cs::FpFtz::PreserveSubnormals, FtzPreserved>);
static_assert(csc::wraps_fp_axis_mode_v<
                  cs::FpDenormalInput::HonorDenormals, DenormalHonored>);

// CV / reference piercing — `ReassocPermittedF const&` is just as toxic
// as the bare wrapper.
static_assert(csc::wraps_fp_axis_mode_v<
                  cs::FpReassociate::UnrestrictedRewrite,
                  ReassocPermittedF const&>);
static_assert(csc::wraps_fp_axis_mode_v<
                  cs::FpReassociate::UnrestrictedRewrite,
                  ReassocPermittedF&>);
static_assert(csc::wraps_fp_axis_mode_v<
                  cs::FpReassociate::UnrestrictedRewrite,
                  ReassocPermittedF const>);

// Negative cells — wrong mode value on right axis.
static_assert(!csc::wraps_fp_axis_mode_v<
                   cs::FpReassociate::UnrestrictedRewrite, ReassocForbidden>);
static_assert(!csc::wraps_fp_axis_mode_v<
                   cs::FpContract::Fast, ContractOff>);
static_assert(!csc::wraps_fp_axis_mode_v<
                   cs::FpFtz::PreserveSubnormals, FtzFlushed>);
static_assert(!csc::wraps_fp_axis_mode_v<
                   cs::FpDenormalInput::HonorDenormals, DenormalDaz>);

// Cross-axis cells — wrapper pins a DIFFERENT sub-axis.  Probing for
// FpReassociate against an FpRoundingPinned, etc., must fall back to
// false_type because the NTTP type doesn't match.  Closes the
// "generic detector accidentally accepts wrong axis" hole.
static_assert(!csc::wraps_fp_axis_mode_v<
                   cs::FpReassociate::UnrestrictedRewrite, RoundingRTNE>);
static_assert(!csc::wraps_fp_axis_mode_v<
                   cs::FpContract::Fast, RoundingRTNE>);
static_assert(!csc::wraps_fp_axis_mode_v<
                   cs::FpFtz::PreserveSubnormals, RoundingRTNE>);
static_assert(!csc::wraps_fp_axis_mode_v<
                   cs::FpDenormalInput::HonorDenormals, ReassocPermittedF>);

// Non-FP type — bare float, double, int — detector reports false.
static_assert(!csc::wraps_fp_axis_mode_v<
                   cs::FpReassociate::UnrestrictedRewrite, float>);
static_assert(!csc::wraps_fp_axis_mode_v<
                   cs::FpReassociate::UnrestrictedRewrite, int>);
static_assert(!csc::wraps_fp_axis_mode_v<
                   cs::FpContract::Fast, double>);

// ── (d) POSITIVE compositions — these MUST NOT trip any rule ───────
//
// V-091's contribution is to REJECT exactly 5 combinations; everything
// else must continue to pass.  Witnessing these positives prevents a
// false positive from sneaking through validate().

// (d1) Bare Fn over a toxic FP wrapper, WITHOUT marks_replay_required
// or marks_ct.  No rule fires.
using NeutralReassocPermitted = csfn::Fn<ReassocPermittedF>;
static_assert(csfn::ValidComposition<NeutralReassocPermitted>);
static_assert(csc::first_failure_v<NeutralReassocPermitted> == csc::RuleCode::None);

using NeutralContractFast = csfn::Fn<ContractFast>;
static_assert(csfn::ValidComposition<NeutralContractFast>);

using NeutralDenormalHonored = csfn::Fn<DenormalHonored>;
static_assert(csfn::ValidComposition<NeutralDenormalHonored>);

using NeutralFtzPreserved = csfn::Fn<FtzPreserved>;
static_assert(csfn::ValidComposition<NeutralFtzPreserved>);

// (d2) Forbidden / Off / DAZ / FlushToZero modes — safe even on
// replay-required and CT paths.  Aliases for the safe modes — no rule
// fires; passing variants exercise the validate() return conjunction
// without tripping any term.
using FnReassocForbidden = csfn::Fn<ReassocForbidden>;
using FnContractOff      = csfn::Fn<ContractOff>;
using FnDenormalDaz      = csfn::Fn<DenormalDaz>;
using FnFtzFlushed       = csfn::Fn<FtzFlushed>;
static_assert(csfn::ValidComposition<FnReassocForbidden>);
static_assert(csfn::ValidComposition<FnContractOff>);
static_assert(csfn::ValidComposition<FnDenormalDaz>);
static_assert(csfn::ValidComposition<FnFtzFlushed>);

// ── (e) NEGATIVE compositions covered by HS14 neg-compile fixtures ─
//
// A sentinel TU CANNOT positively assert `first_failure_v<F> == F10x`
// when F is a Fn carrier that genuinely trips F10x: instantiating F
// runs Fn<>'s own `static_assert(ValidComposition<Fn>, ...)` which
// fires the validate() static_assert chain — i.e., the Fn type itself
// fails to instantiate before first_failure_v can be queried.
//
// HS14 covers this ground correctly: two neg-compile fixtures
// (neg_collision_f101_replay_fp_reassoc.cpp, F101; and
//  neg_collision_f104_ct_fp_denormal_honored.cpp, F104) declare a
// matching Fn carrier, specialize marks_replay_required / marks_ct,
// and let Fn's own static_assert fire with the expected diagnostic
// substring.  Each fixture's CMake test stanza greps the per-rule
// diagnostic ("F101: ...", "F104: ...") — a refactor that
//   (a) drops the static_assert from validate(),
//   (b) relaxes F10x_OK,
//   (c) weakens wraps_fp_axis_mode,
//   (d) drops marks_replay_required / marks_ct from the precondition,
// turns the fixture from RED to GREEN and the test reports failure.
//
// CollisionDiagnosticByRule<DefaultFn, RuleCode::F10x> at section (b)
// pins the diagnostic-string identity without instantiating a carrier
// that trips the rule.

}  // namespace

int main() {
    // No runtime smoke needed — V-091 is pure compile-time discipline.
    // The static_asserts above already exercise every load-bearing
    // surface; main() just satisfies the executable-link requirement
    // ctest expects from a non-fixture test target.
    return 0;
}
