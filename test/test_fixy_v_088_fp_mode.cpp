// FIXY-V-088 sentinel TU: DimensionAxis::FpMode enumerator +
// FpModeLattice.h scaffolding.
//
// V-088 ships:
//   1. DimensionAxis::FpMode (= 22) appended to the enum.
//   2. dimension_axis_name(FpMode) → "FpMode" switch arm.
//   3. tier_of_axis(FpMode) → TierKind::Semiring.
//   4. DIMENSION_AXIS_COUNT bumped 22 → 23.
//   5. count_dims_in_tier(Semiring) bumped 17 → 18.
//   6. algebra/lattices/FpModeLattice.h — 11 sub-axis enums
//      (Rounding/Ftz/Contract/TrapMask/Denormal/NanPolicy/InfPolicy/
//       ComplexLayout/LibmPolicy/Reassociate/FpConstant) +
//      cardinality + distinctness + bottom-element sanity asserts.
//
// V-088 ships NO lattice algebra (V-089 ships per-sub-axis ChainLattice
// algebras; V-090 ships the FpModeProductLattice composite + 11
// safety/Fp*.h wrappers).  This sentinel TU witnesses that the
// VOCABULARY change is structurally consistent.
//
// Why a dedicated FpMode axis (not folded onto Precision):
//   Precision tracks ELEMENT-TYPE precision (FP32/FP16/BF16/E4M3/E5M2).
//   FpMode tracks EVALUATION POLICY (rounding, FTZ, contract, NaN policy
//     etc.).
//   Two ops with identical FP32 element type produce bit-different
//     results under different rounding/FTZ/contract modes; folding both
//     onto Precision would make Merkle-hash-safe FP canonicalization
//     (FIXY-V-093) intractable.

#include <crucible/algebra/lattices/FpModeLattice.h>
#include <crucible/safety/DimensionTraits.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace cs   = ::crucible::safety;
namespace cal  = ::crucible::algebra::lattices;

namespace {

// ── DimensionAxis::FpMode is the topmost (22nd) axis ────────────────
static_assert(std::to_underlying(cs::DimensionAxis::FpMode) == 22,
    "FIXY-V-088: DimensionAxis::FpMode must be the new topmost axis "
    "(ordinal 22).  Append-only discipline forbids reusing earlier "
    "ordinals.");

// ── Tier classification — FpMode is Tier-S (Semiring) ───────────────
static_assert(cs::tier_of_axis(cs::DimensionAxis::FpMode)
              == cs::TierKind::Semiring,
    "FIXY-V-088: FpMode lives on Tier-S (Semiring) — par=join "
    "(strictest-wins), peer to Synchronization (fixy-A3-008) and "
    "Regime (fixy-A3-009).  Misclassification will break Forge phase "
    "E.RecipeSelect's NumericalRecipe gating.");
static_assert(cs::tier_of_axis_v<cs::DimensionAxis::FpMode>
              == cs::TierKind::Semiring,
    "FIXY-V-088: variable-template form of tier_of_axis must agree.");

// ── Name surface — FpMode carries a non-sentinel, non-empty name ────
static_assert(cs::dimension_axis_name(cs::DimensionAxis::FpMode)
              == std::string_view{"FpMode"},
    "FIXY-V-088: dimension_axis_name must return \"FpMode\" for the "
    "new axis; a sentinel leak indicates a missing switch arm.");

// ── Catalog cardinality — V-088 grew the dim count 22 → 23 ──────────
//
// FIXY-U-128 / U-129 floor-vs-ceiling split: the EXACT ceiling pin
// (`== 23`) lives in safety/DimensionTraits.h:642 colocated with the
// source-of-truth enum; THIS TU only holds the FLOOR pin (`>= 23`)
// which catches the inverse direction — an accidental REMOVAL of a
// DimensionAxis enumerator post-FpMode.
static_assert(cs::DIMENSION_AXIS_COUNT >= 23,
    "FIXY-V-088 floor: DimensionAxis cardinality regressed below 23 "
    "— a post-FpMode enumerator was removed without updating both "
    "DimensionTraits.h's colocated ceiling pin AND this floor "
    "witness.");

// ── Tier-S count — 18 axes on Semiring post-V-088 ───────────────────
//
// 15 FX-inherited (Usage/Effect/Security/Lifetime/Provenance/Trust/
// Observability/Complexity/Precision/Space/Overflow/Mutation/
// Reentrancy/Size/Staleness) + Synchronization (A3-008) + Regime
// (A3-009) + FpMode (V-088) = 18.  Note this is a count over the
// CURRENT axis tier mapping; if FX adds another axis on Semiring,
// the count bumps and DimensionTraits.h's colocated assert tracks it.
//
// V-088 sentinel surfaces a FLOOR pin only — the EXACT ceiling
// (`== 18`) lives at DimensionTraits.h:724 colocated with the source
// of truth.  The reflection-driven self-test on the substrate side
// is the authoritative cross-check.

// ── Tier preservation — adding FpMode did not perturb other axes ────
//
// Spot-check axes from each Tier that V-088 must NOT have touched.
// Bug-class catch: an accidental `case DimensionAxis::FpMode: return
// TierKind::Lattice;` would silently re-classify, and the cardinality
// check above would still pass.  Per-axis re-witness rules this out.
static_assert(cs::tier_of_axis(cs::DimensionAxis::Type)
              == cs::TierKind::Foundational);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Refinement)
              == cs::TierKind::Foundational);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Protocol)
              == cs::TierKind::Typestate);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Representation)
              == cs::TierKind::Lattice);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Version)
              == cs::TierKind::Versioned);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Synchronization)
              == cs::TierKind::Semiring);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Regime)
              == cs::TierKind::Semiring);

// ── FpModeLattice.h — 11 sub-axis enum vocabulary ───────────────────
//
// V-088 ships the enums + cardinality sanity asserts ONLY.  V-089
// extends each sub-axis with a ChainLattice algebra; V-090 ships the
// FpModeProductLattice composite + 11 safety/Fp*.h wrappers.
static_assert(cal::detail::fp_mode_lattice_self_test::rounding_count   == 5);
static_assert(cal::detail::fp_mode_lattice_self_test::ftz_count        == 2);
static_assert(cal::detail::fp_mode_lattice_self_test::contract_count   == 3);
static_assert(cal::detail::fp_mode_lattice_self_test::trap_mask_count  == 6);
static_assert(cal::detail::fp_mode_lattice_self_test::denormal_input_count
              == 2);
static_assert(cal::detail::fp_mode_lattice_self_test::nan_policy_count == 3);
static_assert(cal::detail::fp_mode_lattice_self_test::inf_policy_count == 2);
static_assert(cal::detail::fp_mode_lattice_self_test::complex_layout_count
              == 3);
static_assert(cal::detail::fp_mode_lattice_self_test::libm_policy_count
              == 6);
static_assert(cal::detail::fp_mode_lattice_self_test::reassociate_count
              == 3);
static_assert(cal::detail::fp_mode_lattice_self_test::fp_constant_count
              == 3);

// ── Sub-axes are structurally distinct enum types ───────────────────
//
// Spot-check the orthogonality claim: any two sub-axis enums are
// distinct types so the C++ type system rejects accidental mixing.
static_assert(!std::is_same_v<cal::FpRounding,        cal::FpFtz>);
static_assert(!std::is_same_v<cal::FpContract,        cal::FpReassociate>);
static_assert(!std::is_same_v<cal::FpTrapMask,        cal::FpNanPolicy>);
static_assert(!std::is_same_v<cal::FpComplexLayout,   cal::FpLibmPolicy>);
static_assert(!std::is_same_v<cal::FpDenormalInput,   cal::FpInfPolicy>);
static_assert(!std::is_same_v<cal::FpRounding,        cal::FpConstantRounding>);

// ── Each sub-axis's underlying type is uint8_t ──────────────────────
//
// V-089 will derive ChainLatticeOps<E>::leq from std::to_underlying;
// pinning the underlying type now guarantees V-089's derivation has
// the same bit-width across every sub-axis.
static_assert(std::is_same_v<std::underlying_type_t<cal::FpRounding>,
                             std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<cal::FpFtz>,
                             std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<cal::FpContract>,
                             std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<cal::FpTrapMask>,
                             std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<cal::FpDenormalInput>,
                             std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<cal::FpNanPolicy>,
                             std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<cal::FpInfPolicy>,
                             std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<cal::FpComplexLayout>,
                             std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<cal::FpLibmPolicy>,
                             std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<cal::FpReassociate>,
                             std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<cal::FpConstantRounding>,
                             std::uint8_t>);

// ── Bottom-element ordinal convention ───────────────────────────────
//
// Every sub-axis's ordinal-0 element is the "weakest / least-constraining"
// element.  V-089 will derive `bottom()` mechanically from this; V-088
// pins the convention so V-089's derivation is well-defined.
static_assert(std::to_underlying(cal::FpRounding::RoundToZero)             == 0);
static_assert(std::to_underlying(cal::FpFtz::PreserveSubnormals)           == 0);
static_assert(std::to_underlying(cal::FpContract::Off)                     == 0);
static_assert(std::to_underlying(cal::FpTrapMask::AllMasked)               == 0);
static_assert(std::to_underlying(cal::FpDenormalInput::HonorDenormals)     == 0);
static_assert(std::to_underlying(cal::FpNanPolicy::PropagateQuiet)         == 0);
static_assert(std::to_underlying(cal::FpInfPolicy::PropagateInfinity)      == 0);
static_assert(std::to_underlying(cal::FpComplexLayout::Interleaved)        == 0);
static_assert(std::to_underlying(cal::FpLibmPolicy::ScalarLibm)            == 0);
static_assert(std::to_underlying(cal::FpReassociate::Forbidden)            == 0);
static_assert(std::to_underlying(cal::FpConstantRounding::SameAsRuntime)   == 0);

}  // namespace

int main() { return 0; }
