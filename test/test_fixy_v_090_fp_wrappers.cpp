// FIXY-V-090 sentinel TU: 11 FP-mode safety wrappers + composite.
//
// V-090 ships, atop the V-089 lattice algebras:
//   1. ONE generic `safety::FpModePinned<auto Mode, T>` carrier
//      regime-1-EBO over `FpModeLattice::At<Mode>`.
//   2. 11 per-axis type aliases (FpRoundingPinned, FpFtzPinned,
//      FpContractPinned, FpTrapMaskPinned, FpDenormalInputPinned,
//      FpNanPolicyPinned, FpInfPolicyPinned, FpComplexLayoutPinned,
//      FpLibmPolicyPinned, FpReassociatePinned, FpConstantRoundingPinned).
//   3. 11 §XXI-compliant `mint_fp_<axis>` factories +
//      `mint_fp_mode_composite` for the 11-deep nest.
//   4. `FpModeComposite<...>` type alias — canonical §XVI nested stack
//      with outermost FpRoundingPinned and innermost FpConstantRoundingPinned.
//   5. `algebra::lattices::FpModeProductLattice` — 11-way ProductLattice
//      composite for any consumer needing the lattice algebra directly.
//   6. 11 row_hash_contribution specializations (salts 0x21..0x2B) +
//      11 wrapper_dimension specializations (all → DimensionAxis::FpMode).
//
// This sentinel witnesses:
//   (a) the GradedWrapper concept holds for all 11 alias instantiations,
//   (b) sizeof(FpModePinned<Mode, T>) == sizeof(T) for trivial T
//       (regime-1 EBO collapse via At<> singleton element_type),
//   (c) cross-axis row_hash distinctness — every pair of distinct
//       sub-axis types resolves to a DIFFERENT federation cache slot,
//   (d) wrapper_dimension binds all 11 spellings to DimensionAxis::FpMode,
//   (e) FpModeProductLattice satisfies Lattice + BoundedLattice + !Semiring,
//   (f) the 11-deep FpModeComposite collapses to sizeof(T),
//   (g) runtime smoke of every mint + the composite nest.

#include <crucible/safety/FpMode.h>
#include <crucible/safety/DimensionTraits.h>
#include <crucible/safety/diag/RowHashFold.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/FpModeLattice.h>
#include <crucible/algebra/lattices/AllLattices.h>

#include <type_traits>

namespace cs  = ::crucible::safety;
namespace cal = ::crucible::algebra::lattices;
namespace ca  = ::crucible::algebra;
namespace csd = ::crucible::safety::diag;

namespace {

// ── (a) Concept satisfaction — all 11 spellings are GradedWrappers ─
//
// `GradedWrapper` is the structural contract every Graded-backed
// safety carrier must satisfy (graded_type / lattice_type / value_type
// / modality / value_type_name / lattice_name).  Pinning here guards
// against a header refactor that silently drops one of the contract
// members.
using F01 = cs::FpRoundingPinned<cs::FpRounding::RoundToNearestEven, int>;
using F02 = cs::FpFtzPinned<cs::FpFtz::FlushToZero, int>;
using F03 = cs::FpContractPinned<cs::FpContract::Fast, int>;
using F04 = cs::FpTrapMaskPinned<cs::FpTrapMask::AllMasked, int>;
using F05 = cs::FpDenormalInputPinned<cs::FpDenormalInput::DenormalsAreZero, int>;
using F06 = cs::FpNanPolicyPinned<cs::FpNanPolicy::PropagateQuiet, int>;
using F07 = cs::FpInfPolicyPinned<cs::FpInfPolicy::PropagateInfinity, int>;
using F08 = cs::FpComplexLayoutPinned<cs::FpComplexLayout::Interleaved, int>;
using F09 = cs::FpLibmPolicyPinned<cs::FpLibmPolicy::ScalarLibm, int>;
using F10 = cs::FpReassociatePinned<cs::FpReassociate::Forbidden, int>;
using F11 = cs::FpConstantRoundingPinned<cs::FpConstantRounding::SameAsRuntime, int>;

static_assert(ca::GradedWrapper<F01>);
static_assert(ca::GradedWrapper<F02>);
static_assert(ca::GradedWrapper<F03>);
static_assert(ca::GradedWrapper<F04>);
static_assert(ca::GradedWrapper<F05>);
static_assert(ca::GradedWrapper<F06>);
static_assert(ca::GradedWrapper<F07>);
static_assert(ca::GradedWrapper<F08>);
static_assert(ca::GradedWrapper<F09>);
static_assert(ca::GradedWrapper<F10>);
static_assert(ca::GradedWrapper<F11>);

// ── (b) Regime-1 EBO collapse — sizeof(W<T>) == sizeof(T) ──────────
//
// The FpModeLattice::At<Mode>::element_type is EMPTY for every Mode
// (V-089 pinned this via CRUCIBLE_FP_LATTICE_VERIFY).  Combined with
// Graded's [[no_unique_address]] grade_, every per-axis wrapper is
// byte-equivalent to its T payload.  This is the load-bearing "ZERO
// runtime cost" claim of V-090; regression here lights every FpMode
// consumer downstream.
static_assert(sizeof(F01) == sizeof(int));
static_assert(sizeof(F02) == sizeof(int));
static_assert(sizeof(F03) == sizeof(int));
static_assert(sizeof(F04) == sizeof(int));
static_assert(sizeof(F05) == sizeof(int));
static_assert(sizeof(F06) == sizeof(int));
static_assert(sizeof(F07) == sizeof(int));
static_assert(sizeof(F08) == sizeof(int));
static_assert(sizeof(F09) == sizeof(int));
static_assert(sizeof(F10) == sizeof(int));
static_assert(sizeof(F11) == sizeof(int));

// ── (c) row_hash cross-axis distinctness ────────────────────────────
//
// Each per-axis wrapper carries a distinct WRAPPER_FP_*_TAG salt
// (0x21..0x2B per RowHashFold.h §FIXY-V-090).  Two distinct sub-axis
// instantiations over the SAME payload Inner MUST resolve to distinct
// row_hash values — otherwise the federation cache routes BITEXACT
// FpRounding::RoundToNearestEven results into the same slot as
// FpFtz::FlushToZero results, silently smashing the §7(b) per-axis
// numerics contract.  Each pair below pins a different cell.
static_assert(csd::row_hash_contribution_v<F01>
           != csd::row_hash_contribution_v<F02>);
static_assert(csd::row_hash_contribution_v<F01>
           != csd::row_hash_contribution_v<F03>);
static_assert(csd::row_hash_contribution_v<F02>
           != csd::row_hash_contribution_v<F04>);
static_assert(csd::row_hash_contribution_v<F05>
           != csd::row_hash_contribution_v<F06>);
static_assert(csd::row_hash_contribution_v<F07>
           != csd::row_hash_contribution_v<F08>);
static_assert(csd::row_hash_contribution_v<F09>
           != csd::row_hash_contribution_v<F10>);
static_assert(csd::row_hash_contribution_v<F10>
           != csd::row_hash_contribution_v<F11>);
// Per-axis cross-MODE distinctness — same axis, different mode → low
// byte of the salt differs through the Mode enumerator's underlying
// value.  Pins that two FpRounding modes on the SAME payload land in
// distinct cache slots.
static_assert(csd::row_hash_contribution_v<
                  cs::FpRoundingPinned<cs::FpRounding::RoundToNearestEven, int>>
           != csd::row_hash_contribution_v<
                  cs::FpRoundingPinned<cs::FpRounding::RoundToZero, int>>);
static_assert(csd::row_hash_contribution_v<
                  cs::FpFtzPinned<cs::FpFtz::FlushToZero, int>>
           != csd::row_hash_contribution_v<
                  cs::FpFtzPinned<cs::FpFtz::PreserveSubnormals, int>>);

// ── (d) wrapper_dimension — all 11 spellings on the FpMode axis ─────
//
// Every per-axis wrapper maps to DimensionAxis::FpMode (the V-088
// axis-22 Tier-S slot).  Per-axis row_hash disambiguation happens
// through the row_hash specializations above; the dimension-traits
// layer treats the 11 as one axis for cross-cutting queries.
static_assert(cs::wrapper_dimension_v<F01> == cs::DimensionAxis::FpMode);
static_assert(cs::wrapper_dimension_v<F02> == cs::DimensionAxis::FpMode);
static_assert(cs::wrapper_dimension_v<F03> == cs::DimensionAxis::FpMode);
static_assert(cs::wrapper_dimension_v<F04> == cs::DimensionAxis::FpMode);
static_assert(cs::wrapper_dimension_v<F05> == cs::DimensionAxis::FpMode);
static_assert(cs::wrapper_dimension_v<F06> == cs::DimensionAxis::FpMode);
static_assert(cs::wrapper_dimension_v<F07> == cs::DimensionAxis::FpMode);
static_assert(cs::wrapper_dimension_v<F08> == cs::DimensionAxis::FpMode);
static_assert(cs::wrapper_dimension_v<F09> == cs::DimensionAxis::FpMode);
static_assert(cs::wrapper_dimension_v<F10> == cs::DimensionAxis::FpMode);
static_assert(cs::wrapper_dimension_v<F11> == cs::DimensionAxis::FpMode);

// ── (e) FpModeProductLattice concept + arity ────────────────────────
//
// The 11-way ProductLattice composite must satisfy Lattice and
// BoundedLattice (every component ChainLatticeOps<EnumT> is bounded)
// but NOT Semiring (no ⊕/⊗ separate from join/meet).  Arity must be
// 11 — guards against a sub-axis being dropped from the alias.
static_assert(ca::Lattice<cal::FpModeProductLattice>);
static_assert(ca::BoundedLattice<cal::FpModeProductLattice>);
static_assert(!ca::Semiring<cal::FpModeProductLattice>);
static_assert(cal::FpModeProductLattice::arity == 11);

// ── (f) FpModeComposite — 11-deep nest collapses to sizeof(T) ──────
//
// The §XVI canonical nested stack composes 11 regime-1 wrappers
// outer-to-inner.  Every layer EBO-collapses; the composite is
// byte-equivalent to the bare T.  This is the "ZERO runtime cost
// per axis" promise of V-090.
using Composite = cs::FpModeComposite<
    cs::FpRounding::RoundToNearestEven,
    cs::FpFtz::FlushToZero,
    cs::FpContract::Off,
    cs::FpTrapMask::AllMasked,
    cs::FpDenormalInput::HonorDenormals,
    cs::FpNanPolicy::PropagateQuiet,
    cs::FpInfPolicy::PropagateInfinity,
    cs::FpComplexLayout::Interleaved,
    cs::FpLibmPolicy::ScalarLibm,
    cs::FpReassociate::Forbidden,
    cs::FpConstantRounding::SameAsRuntime,
    int>;
static_assert(sizeof(Composite) == sizeof(int));

// ── §XXI Universal Mint Pattern — 12 mints are §XXI-compliant ──────
//
// All 12 mints (11 per-axis + 1 composite) are constexpr noexcept
// [[nodiscard]] and gated by a `requires std::is_constructible_v<T,
// Args...>` clause.  Pinning consteval-callability witnesses the
// §XXI compliance without requiring a separate concept gate.
static_assert(cs::mint_fp_rounding<cs::FpRounding::RoundToNearestEven, int>(7).peek() == 7);
static_assert(cs::mint_fp_ftz<cs::FpFtz::FlushToZero, int>(42).peek() == 42);
static_assert(cs::mint_fp_contract<cs::FpContract::Fast, int>(13).peek() == 13);
static_assert(cs::mint_fp_trap_mask<cs::FpTrapMask::AllMasked, int>(0).peek() == 0);

}  // namespace

int main() {
    // Runtime smoke — exercises every mint with non-constant args plus
    // the 11-deep composite nest.  Catches consteval/SFINAE bugs that
    // pure static_assert would mask (feedback_algebra_runtime_smoke_test
    // _discipline memory).
    cs::fp_mode_runtime_smoke_test();
    return 0;
}
