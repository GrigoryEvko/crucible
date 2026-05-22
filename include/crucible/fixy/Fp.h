#pragma once

// ── crucible::fixy::grant — FP-mode grant tags (FIXY-V-092) ───────────
//
// Twelve grant tags that engage `DimensionAxis::FpMode` (substrate
// ordinal 22, added 2026-05-22 by V-088).  Eleven per-sub-axis parametric grants pin a
// specific mode on a single FP sub-axis; one aggregate grant
// (`fp_strict_ieee`) declares conformance to the IEEE 754 default on
// every sub-axis at once.  Every tag routes to `DimensionAxis::FpMode`
// via `which_dim` so Reject.h's engagement walk treats this axis
// uniformly.
//
// ── Why a separate header (rather than extending Grant.h) ─────────────
//
// Grant.h is approaching 750 lines and groups tags by SAFETY-LATTICE
// taxonomy (Security, Trust, Lifetime, etc.).  FpMode is a
// META-axis — it composes ELEVEN sub-axes (Rounding, Ftz, Contract,
// TrapMask, DenormalInput, NanPolicy, InfPolicy, ComplexLayout,
// LibmPolicy, Reassociate, ConstantRounding) which collectively
// determine floating-point evaluation policy.  Keeping the 12 tags
// + their per-tag `which_dim` specializations + the per-axis enum
// `using`-declarations in their own header keeps Grant.h focused on
// the safety-lattice axes and gives the FP family a single grep
// target (`grant::with_fp_*`, `grant::fp_strict_ieee`).
//
// Per Grant.h's namespace-purity discipline (lines 121-158), all
// `which_dim` specializations MUST live syntactically inside
// `namespace crucible::fixy::grant`.  This header reopens that
// namespace — `scripts/check-fixy-grant-namespace-purity.sh` allowlists
// Fp.h alongside Grant.h itself.
//
// ── Substrate consumed ────────────────────────────────────────────────
//
//   crucible::fixy::dim::DimensionAxis   — enum cited by which_dim
//   crucible::fixy::grant::grant_base    — structural marker (Grant.h)
//   crucible::fixy::grant::which_dim     — primary template (Grant.h)
//   crucible::safety::Fp*                — 11 sub-axis enum aliases
//
// ── Substrate added by this header ────────────────────────────────────
//
// NONE.  Every tag is an empty + final struct inheriting `grant_base`;
// EBO-collapses to 1 byte; `which_dim` specializations carry no
// runtime state.  No new lattice, no new wrapper.
//
// ── Cost ──────────────────────────────────────────────────────────────
//
// Zero.  Every tag is sizeof == 1 standalone; in any aggregator that
// inherits from grant_base, EBO collapses to 0 bytes.  Compile-time
// `which_dim_v<G>` is a single integral_constant member access.
//
// ── How the grants resolve inside fixy::fn<...> ───────────────────────
//
// V-088 marked FpMode WRAPPER-ONLY: no Fn<...> aggregator template-
// parameter slot.  The per-axis pinning happens at the VALUE site
// (FpRoundingPinned<RoundToNearestEven, T> etc., V-090).  Each grant
// here is the FEDERATION/REFLECTION channel — it declares "this Fn
// binding's value sites carry FP-mode wrappers pinning sub-axis X to
// mode Y".  The binding's resolver does NOT inject a Fn<> slot;
// instead it:
//
//   (1) routes through `which_dim_v<G>` to compute axis engagement
//       for the FpMode dim (Reject.h coverage check),
//   (2) propagates the mode NTTP to the federation key via
//       `row_hash_contribution<FpModePinned<Mode, T>>` at the value
//       site (V-090's row_hash specializations).
//
// Production callers wanting "the IEEE strict default on every sub-
// axis" pass `fp_strict_ieee` (zero parameters, one tag).  Callers
// pinning a specific sub-axis pass the matching `with_fp_<axis><Mode>`
// — `with_fp_rounding<FpRounding::RoundToZero>` pins the rounding
// axis to RTZ while leaving the other 10 sub-axes at whatever the
// strict default specifies.
//
// Each parametric grant accepts ANY value of its enum's domain so
// non-strict modes flow through identically — the grant is the
// engagement marker, NOT a strict-default gate.  Tier-S Semiring
// composition (par=join, strictest-wins) at the CollisionCatalog F101-
// F105 cross-axis sites enforces the discipline.
//
// ── Self-test ─────────────────────────────────────────────────────────
//
// Five load-bearing assertions per grant tag (twelve grants total →
// 60 cells):
//
//   1. `IsGrantTag<G>` — final + grant_base + cv-ref-free.
//   2. `sizeof(G) == 1` — EBO-collapsible.
//   3. `which_dim_v<G> == DimensionAxis::FpMode` — routes to FpMode.
//   4. Distinct tag types — pairwise `!std::is_same_v<Gi, Gj>` for
//      the 12 tags.  (12 × 11 / 2 = 66 cells; sampled at key sub-axis
//      boundaries here, exhaustive enumeration lives in the sentinel
//      TU.)
//   5. Two parametric tags with the SAME sub-axis but DIFFERENT mode
//      NTTPs are distinct types (e.g.,
//      `with_fp_rounding<RTE>` ≠ `with_fp_rounding<RTZ>`).
//
// HS14 negative-compile fixtures witness (a) cv-qualified rejection
// reaches the AllGrantsWellFormed gate just like the bare-Grant.h
// fixtures and (b) a non-enum NTTP value in a parametric grant
// rejects at template-id formation.

#include <crucible/fixy/Grant.h>            // grant_base, which_dim primary
#include <crucible/safety/FpMode.h>         // 11 sub-axis enums + wrappers
#include <crucible/safety/DimensionTraits.h>// DimensionAxis::FpMode

#include <type_traits>

namespace crucible::fixy::grant {

// ═════════════════════════════════════════════════════════════════════
// ── 11 per-sub-axis parametric grants ─────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Each grant takes a non-type template parameter typed to the
// corresponding sub-axis enum.  The NTTP enum-type discriminates: two
// `with_fp_rounding<...>` instantiations with different FpRounding
// values are distinct types, just as `with_fp_rounding<RTE>` and
// `with_fp_ftz<...>` are distinct types regardless of mode value.
// All twelve tags fold into `DimensionAxis::FpMode` via `which_dim`
// — duplicate-engagement detection lives in Reject.h.

// ── Sub-axis 1: Rounding ──────────────────────────────────────────────
template <::crucible::safety::FpRounding Mode>
struct with_fp_rounding final : grant_base {};

template <::crucible::safety::FpRounding Mode>
struct which_dim<with_fp_rounding<Mode>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::FpMode> {};

// ── Sub-axis 2: Flush-to-zero ─────────────────────────────────────────
template <::crucible::safety::FpFtz Mode>
struct with_fp_ftz final : grant_base {};

template <::crucible::safety::FpFtz Mode>
struct which_dim<with_fp_ftz<Mode>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::FpMode> {};

// ── Sub-axis 3: Contract (cross-statement FMA folding) ────────────────
template <::crucible::safety::FpContract Mode>
struct with_fp_contract final : grant_base {};

template <::crucible::safety::FpContract Mode>
struct which_dim<with_fp_contract<Mode>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::FpMode> {};

// ── Sub-axis 4: Trap mask ─────────────────────────────────────────────
template <::crucible::safety::FpTrapMask Mode>
struct with_fp_trap_mask final : grant_base {};

template <::crucible::safety::FpTrapMask Mode>
struct which_dim<with_fp_trap_mask<Mode>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::FpMode> {};

// ── Sub-axis 5: Denormal-input handling (DAZ-axis dual) ───────────────
template <::crucible::safety::FpDenormalInput Mode>
struct with_fp_denormal_input final : grant_base {};

template <::crucible::safety::FpDenormalInput Mode>
struct which_dim<with_fp_denormal_input<Mode>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::FpMode> {};

// ── Sub-axis 6: NaN policy ────────────────────────────────────────────
template <::crucible::safety::FpNanPolicy Mode>
struct with_fp_nan_policy final : grant_base {};

template <::crucible::safety::FpNanPolicy Mode>
struct which_dim<with_fp_nan_policy<Mode>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::FpMode> {};

// ── Sub-axis 7: Infinity policy ───────────────────────────────────────
template <::crucible::safety::FpInfPolicy Mode>
struct with_fp_inf_policy final : grant_base {};

template <::crucible::safety::FpInfPolicy Mode>
struct which_dim<with_fp_inf_policy<Mode>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::FpMode> {};

// ── Sub-axis 8: Complex layout ────────────────────────────────────────
template <::crucible::safety::FpComplexLayout Mode>
struct with_fp_complex_layout final : grant_base {};

template <::crucible::safety::FpComplexLayout Mode>
struct which_dim<with_fp_complex_layout<Mode>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::FpMode> {};

// ── Sub-axis 9: libm policy (vector vs scalar transcendentals) ────────
template <::crucible::safety::FpLibmPolicy Mode>
struct with_fp_libm_policy final : grant_base {};

template <::crucible::safety::FpLibmPolicy Mode>
struct which_dim<with_fp_libm_policy<Mode>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::FpMode> {};

// ── Sub-axis 10: Reassociation (BITEXACT-critical) ────────────────────
template <::crucible::safety::FpReassociate Mode>
struct with_fp_reassociate final : grant_base {};

template <::crucible::safety::FpReassociate Mode>
struct which_dim<with_fp_reassociate<Mode>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::FpMode> {};

// ── Sub-axis 11: Constant rounding (FP-literal folding) ───────────────
template <::crucible::safety::FpConstantRounding Mode>
struct with_fp_constant_rounding final : grant_base {};

template <::crucible::safety::FpConstantRounding Mode>
struct which_dim<with_fp_constant_rounding<Mode>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::FpMode> {};

// ═════════════════════════════════════════════════════════════════════
// ── Aggregate grant: fp_strict_ieee ───────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// `fp_strict_ieee` declares "this binding pins every FP sub-axis to
// its IEEE 754 strict default on every sub-axis simultaneously":
//
//     FpRounding          = RoundToNearestEven  (RTE)
//     FpFtz               = PreserveSubnormals   (gradual underflow)
//     FpContract          = Off                  (no cross-statement FMA folding)
//     FpTrapMask          = AllMasked            (silent — DetSafe-safe)
//     FpDenormalInput     = HonorDenormals       (DAZ=0)
//     FpNanPolicy         = PropagateQuiet       (qNaN survives every op)
//     FpInfPolicy         = PropagateInfinity    (±Inf survives)
//     FpComplexLayout     = Interleaved          (std::complex layout)
//     FpLibmPolicy        = ScalarLibm           (scalar glibc/musl libm)
//     FpReassociate       = Forbidden            (no rewrites — BITEXACT)
//     FpConstantRounding  = SameAsRuntime        (literals follow runtime)
//
// This is the ONE grant that explicitly opts INTO BITEXACT-quality
// reproducibility on every sub-axis at once.  Common case for HotPath ×
// DetSafe binding sites — instead of writing 11 separate parametric
// grants, the caller writes `fp_strict_ieee` and Reject.h sees one
// engagement of the FpMode axis.  Tier-S Semiring composition reads
// this as the bottom of the lattice on every sub-axis (the
// strictest-wins join at F101-F105 sites collapses through this point).

struct fp_strict_ieee final : grant_base {};

template <>
struct which_dim<fp_strict_ieee>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::FpMode> {};

// ═════════════════════════════════════════════════════════════════════
// ── Self-test (compile-time) ─────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Five layers per the doc-block: IsGrantTag, sizeof, which_dim
// routing, type-distinctness across sub-axes, NTTP-distinctness within
// one sub-axis.  Sample at every sub-axis to cover the 12-tag surface.

namespace detail::fp_grant_self_test {

namespace sf = ::crucible::safety;
using D      = dim::DimensionAxis;

// ── Layer 1: IsGrantTag — sampled at every sub-axis + aggregate ─────
static_assert(IsGrantTag<with_fp_rounding<sf::FpRounding::RoundToNearestEven>>);
static_assert(IsGrantTag<with_fp_ftz<sf::FpFtz::PreserveSubnormals>>);
static_assert(IsGrantTag<with_fp_contract<sf::FpContract::Off>>);
static_assert(IsGrantTag<with_fp_trap_mask<sf::FpTrapMask::AllMasked>>);
static_assert(IsGrantTag<with_fp_denormal_input<sf::FpDenormalInput::HonorDenormals>>);
static_assert(IsGrantTag<with_fp_nan_policy<sf::FpNanPolicy::PropagateQuiet>>);
static_assert(IsGrantTag<with_fp_inf_policy<sf::FpInfPolicy::PropagateInfinity>>);
static_assert(IsGrantTag<with_fp_complex_layout<sf::FpComplexLayout::Interleaved>>);
static_assert(IsGrantTag<with_fp_libm_policy<sf::FpLibmPolicy::ScalarLibm>>);
static_assert(IsGrantTag<with_fp_reassociate<sf::FpReassociate::Forbidden>>);
static_assert(IsGrantTag<with_fp_constant_rounding<sf::FpConstantRounding::SameAsRuntime>>);
static_assert(IsGrantTag<fp_strict_ieee>);

// ── Layer 2: sizeof — EBO-collapsible standalone marker (1 byte) ────
static_assert(sizeof(with_fp_rounding<sf::FpRounding::RoundToNearestEven>)        == 1);
static_assert(sizeof(with_fp_ftz<sf::FpFtz::PreserveSubnormals>)                  == 1);
static_assert(sizeof(with_fp_contract<sf::FpContract::Off>)                       == 1);
static_assert(sizeof(with_fp_trap_mask<sf::FpTrapMask::AllMasked>)                == 1);
static_assert(sizeof(with_fp_denormal_input<sf::FpDenormalInput::HonorDenormals>) == 1);
static_assert(sizeof(with_fp_nan_policy<sf::FpNanPolicy::PropagateQuiet>)         == 1);
static_assert(sizeof(with_fp_inf_policy<sf::FpInfPolicy::PropagateInfinity>)      == 1);
static_assert(sizeof(with_fp_complex_layout<sf::FpComplexLayout::Interleaved>)    == 1);
static_assert(sizeof(with_fp_libm_policy<sf::FpLibmPolicy::ScalarLibm>)           == 1);
static_assert(sizeof(with_fp_reassociate<sf::FpReassociate::Forbidden>)           == 1);
static_assert(sizeof(with_fp_constant_rounding<sf::FpConstantRounding::SameAsRuntime>) == 1);
static_assert(sizeof(fp_strict_ieee) == 1);

// ── Layer 3: which_dim routing — every tag → FpMode ─────────────────
static_assert(which_dim_v<with_fp_rounding<sf::FpRounding::RoundToNearestEven>>        == D::FpMode);
static_assert(which_dim_v<with_fp_ftz<sf::FpFtz::PreserveSubnormals>>                  == D::FpMode);
static_assert(which_dim_v<with_fp_contract<sf::FpContract::Off>>                       == D::FpMode);
static_assert(which_dim_v<with_fp_trap_mask<sf::FpTrapMask::AllMasked>>                == D::FpMode);
static_assert(which_dim_v<with_fp_denormal_input<sf::FpDenormalInput::HonorDenormals>> == D::FpMode);
static_assert(which_dim_v<with_fp_nan_policy<sf::FpNanPolicy::PropagateQuiet>>         == D::FpMode);
static_assert(which_dim_v<with_fp_inf_policy<sf::FpInfPolicy::PropagateInfinity>>      == D::FpMode);
static_assert(which_dim_v<with_fp_complex_layout<sf::FpComplexLayout::Interleaved>>    == D::FpMode);
static_assert(which_dim_v<with_fp_libm_policy<sf::FpLibmPolicy::ScalarLibm>>           == D::FpMode);
static_assert(which_dim_v<with_fp_reassociate<sf::FpReassociate::Forbidden>>           == D::FpMode);
static_assert(which_dim_v<with_fp_constant_rounding<sf::FpConstantRounding::SameAsRuntime>> == D::FpMode);
static_assert(which_dim_v<fp_strict_ieee> == D::FpMode);

// ── Layer 4: cross-sub-axis distinctness — sampled boundary pairs ───
// Sampling 11 pairs spanning every sub-axis boundary; the sentinel TU
// runs the full 66-cell pairwise matrix.
static_assert(!std::is_same_v<
    with_fp_rounding<sf::FpRounding::RoundToNearestEven>,
    with_fp_ftz<sf::FpFtz::PreserveSubnormals>>);
static_assert(!std::is_same_v<
    with_fp_ftz<sf::FpFtz::PreserveSubnormals>,
    with_fp_contract<sf::FpContract::Off>>);
static_assert(!std::is_same_v<
    with_fp_contract<sf::FpContract::Off>,
    with_fp_trap_mask<sf::FpTrapMask::AllMasked>>);
static_assert(!std::is_same_v<
    with_fp_trap_mask<sf::FpTrapMask::AllMasked>,
    with_fp_denormal_input<sf::FpDenormalInput::HonorDenormals>>);
static_assert(!std::is_same_v<
    with_fp_denormal_input<sf::FpDenormalInput::HonorDenormals>,
    with_fp_nan_policy<sf::FpNanPolicy::PropagateQuiet>>);
static_assert(!std::is_same_v<
    with_fp_nan_policy<sf::FpNanPolicy::PropagateQuiet>,
    with_fp_inf_policy<sf::FpInfPolicy::PropagateInfinity>>);
static_assert(!std::is_same_v<
    with_fp_inf_policy<sf::FpInfPolicy::PropagateInfinity>,
    with_fp_complex_layout<sf::FpComplexLayout::Interleaved>>);
static_assert(!std::is_same_v<
    with_fp_complex_layout<sf::FpComplexLayout::Interleaved>,
    with_fp_libm_policy<sf::FpLibmPolicy::ScalarLibm>>);
static_assert(!std::is_same_v<
    with_fp_libm_policy<sf::FpLibmPolicy::ScalarLibm>,
    with_fp_reassociate<sf::FpReassociate::Forbidden>>);
static_assert(!std::is_same_v<
    with_fp_reassociate<sf::FpReassociate::Forbidden>,
    with_fp_constant_rounding<sf::FpConstantRounding::SameAsRuntime>>);
static_assert(!std::is_same_v<
    with_fp_constant_rounding<sf::FpConstantRounding::SameAsRuntime>,
    fp_strict_ieee>);

// ── Layer 5: within-sub-axis NTTP-distinctness ──────────────────────
// Two `with_fp_rounding<...>` instantiations with different FpRounding
// NTTP values produce distinct types — the type system discriminates
// rounding modes structurally.  Sampled at the Rounding axis (which
// has 5 enumerators), Ftz (2), Contract (3), Reassociate (3); the
// other 7 sub-axes follow the same C++ template-instantiation rule
// and the sentinel TU covers them.
static_assert(!std::is_same_v<
    with_fp_rounding<sf::FpRounding::RoundToNearestEven>,
    with_fp_rounding<sf::FpRounding::RoundToZero>>);
static_assert(!std::is_same_v<
    with_fp_ftz<sf::FpFtz::PreserveSubnormals>,
    with_fp_ftz<sf::FpFtz::FlushToZero>>);
static_assert(!std::is_same_v<
    with_fp_contract<sf::FpContract::Off>,
    with_fp_contract<sf::FpContract::Fast>>);
static_assert(!std::is_same_v<
    with_fp_reassociate<sf::FpReassociate::Forbidden>,
    with_fp_reassociate<sf::FpReassociate::UnrestrictedRewrite>>);

// ── cv-ref rejection — every Fp grant flows through IsGrantTag's
//    `is_same_v<G, remove_cvref_t<G>>` clause (fixy-A4-033). ──────────
static_assert(!IsGrantTag_v<const with_fp_rounding<sf::FpRounding::RoundToNearestEven>>);
static_assert(!IsGrantTag_v<with_fp_rounding<sf::FpRounding::RoundToNearestEven>&>);
static_assert(!IsGrantTag_v<const fp_strict_ieee>);
static_assert(!IsGrantTag_v<fp_strict_ieee&>);

}  // namespace detail::fp_grant_self_test

}  // namespace crucible::fixy::grant
