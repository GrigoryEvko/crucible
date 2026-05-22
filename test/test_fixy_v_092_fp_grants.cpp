// FIXY-V-092 sentinel TU: fixy/Fp.h — 12 FP-mode grant tags + per-tag
// which_dim metafunction routing to DimensionAxis::FpMode.
//
// V-092 ships 12 grant tags:
//   - 11 per-sub-axis parametric grants: with_fp_rounding<Mode>,
//     with_fp_ftz<Mode>, with_fp_contract<Mode>, with_fp_trap_mask<Mode>,
//     with_fp_denormal_input<Mode>, with_fp_nan_policy<Mode>,
//     with_fp_inf_policy<Mode>, with_fp_complex_layout<Mode>,
//     with_fp_libm_policy<Mode>, with_fp_reassociate<Mode>,
//     with_fp_constant_rounding<Mode>.
//   - 1 aggregate grant: fp_strict_ieee (zero-NTTP, declares IEEE 754
//     strict defaults on every sub-axis simultaneously).
//
// Per the V-088 wrapper-only design (Default.h:329-345), FpMode has NO
// Fn<...> aggregator template-parameter slot; the per-axis pinning
// happens at the VALUE site via FpModePinned<Mode, T> (V-090).  These
// grants are the FEDERATION/REFLECTION channel that declares "this
// binding's value sites pin sub-axis X to mode Y".  Reject.h's
// engagement walk reads which_dim_v<G> uniformly across all 23 axes;
// the FpMode coverage check for each grant fires this way.
//
// Sentinel surface (four claim families):
//   (a) Catalog identity — Fp.h ships exactly 12 grant tags; the umbrella
//       in detail::fp_grant_self_test exercises every one at the
//       static_assert level.  This TU re-binds at runtime-eligible call
//       sites so consteval/SFINAE bugs surface at link time.
//   (b) which_dim routing — full enumeration over the 5-element FpRounding
//       enum, the 2-element FpFtz enum, the 3-element FpContract /
//       FpReassociate enums (exhaustive coverage of the four sub-axes
//       most likely to have NTTP-projection bugs).
//   (c) Cross-sub-axis type distinctness — full 66-cell pairwise
//       enumeration over all 12 tags.  Bug-class catch: a copy-paste
//       error where two `with_fp_*` templates resolve to the same
//       phantom-type would silently reduce the engagement signal.
//   (d) Within-sub-axis NTTP-distinctness — sample on every sub-axis
//       (every enum's first ≥2 enumerators).
//
// Layered with the embedded static_asserts in Fp.h's
// `detail::fp_grant_self_test`, the surface witnesses every load-
// bearing property of the grant tags.

#include <crucible/fixy/Fp.h>
#include <crucible/fixy/Reject.h>

#include <type_traits>
#include <utility>

namespace fxg   = ::crucible::fixy::grant;
namespace fxd   = ::crucible::fixy::dim;
namespace sf    = ::crucible::safety;

using D = fxd::DimensionAxis;

namespace {

// ═══════════════════════════════════════════════════════════════════════
// (a) ── Catalog identity — every grant tag is IsGrantTag + sizeof 1 ──
// ═══════════════════════════════════════════════════════════════════════

// Per-axis parametric — sampled at every sub-axis's bottom enumerator.
static_assert(fxg::IsGrantTag<fxg::with_fp_rounding<sf::FpRounding::RoundToZero>>);
static_assert(fxg::IsGrantTag<fxg::with_fp_ftz<sf::FpFtz::PreserveSubnormals>>);
static_assert(fxg::IsGrantTag<fxg::with_fp_contract<sf::FpContract::Off>>);
static_assert(fxg::IsGrantTag<fxg::with_fp_trap_mask<sf::FpTrapMask::AllMasked>>);
static_assert(fxg::IsGrantTag<fxg::with_fp_denormal_input<sf::FpDenormalInput::HonorDenormals>>);
static_assert(fxg::IsGrantTag<fxg::with_fp_nan_policy<sf::FpNanPolicy::PropagateQuiet>>);
static_assert(fxg::IsGrantTag<fxg::with_fp_inf_policy<sf::FpInfPolicy::PropagateInfinity>>);
static_assert(fxg::IsGrantTag<fxg::with_fp_complex_layout<sf::FpComplexLayout::Interleaved>>);
static_assert(fxg::IsGrantTag<fxg::with_fp_libm_policy<sf::FpLibmPolicy::ScalarLibm>>);
static_assert(fxg::IsGrantTag<fxg::with_fp_reassociate<sf::FpReassociate::Forbidden>>);
static_assert(fxg::IsGrantTag<fxg::with_fp_constant_rounding<sf::FpConstantRounding::SameAsRuntime>>);
static_assert(fxg::IsGrantTag<fxg::fp_strict_ieee>);

// EBO collapse — every tag is sizeof 1 standalone.
static_assert(sizeof(fxg::with_fp_rounding<sf::FpRounding::RoundToZero>) == 1);
static_assert(sizeof(fxg::with_fp_ftz<sf::FpFtz::PreserveSubnormals>) == 1);
static_assert(sizeof(fxg::with_fp_contract<sf::FpContract::Off>) == 1);
static_assert(sizeof(fxg::with_fp_trap_mask<sf::FpTrapMask::AllMasked>) == 1);
static_assert(sizeof(fxg::with_fp_denormal_input<sf::FpDenormalInput::HonorDenormals>) == 1);
static_assert(sizeof(fxg::with_fp_nan_policy<sf::FpNanPolicy::PropagateQuiet>) == 1);
static_assert(sizeof(fxg::with_fp_inf_policy<sf::FpInfPolicy::PropagateInfinity>) == 1);
static_assert(sizeof(fxg::with_fp_complex_layout<sf::FpComplexLayout::Interleaved>) == 1);
static_assert(sizeof(fxg::with_fp_libm_policy<sf::FpLibmPolicy::ScalarLibm>) == 1);
static_assert(sizeof(fxg::with_fp_reassociate<sf::FpReassociate::Forbidden>) == 1);
static_assert(sizeof(fxg::with_fp_constant_rounding<sf::FpConstantRounding::SameAsRuntime>) == 1);
static_assert(sizeof(fxg::fp_strict_ieee) == 1);

// ═══════════════════════════════════════════════════════════════════════
// (b) ── which_dim routing — full enumeration on key sub-axes ──────────
// ═══════════════════════════════════════════════════════════════════════
//
// Exhaustively bind every enumerator on the four most NTTP-sensitive
// sub-axes (Rounding: 5 enumerators, Ftz: 2, Contract: 3, Reassociate:
// 3) so a copy-paste bug in the per-axis `which_dim` specialization
// surfaces in this TU.  Other sub-axes are covered by sampling above;
// the substrate ChainLattice enumerator count is verified by V-089's
// sentinel.

// FpRounding — 5 enumerators
static_assert(fxg::which_dim_v<fxg::with_fp_rounding<sf::FpRounding::RoundToZero>>            == D::FpMode);
static_assert(fxg::which_dim_v<fxg::with_fp_rounding<sf::FpRounding::RoundToNegativeInf>>     == D::FpMode);
static_assert(fxg::which_dim_v<fxg::with_fp_rounding<sf::FpRounding::RoundToPositiveInf>>     == D::FpMode);
static_assert(fxg::which_dim_v<fxg::with_fp_rounding<sf::FpRounding::RoundToNearestEven>>     == D::FpMode);
static_assert(fxg::which_dim_v<fxg::with_fp_rounding<sf::FpRounding::RoundToNearestAwayZero>> == D::FpMode);

// FpFtz — 2 enumerators
static_assert(fxg::which_dim_v<fxg::with_fp_ftz<sf::FpFtz::PreserveSubnormals>> == D::FpMode);
static_assert(fxg::which_dim_v<fxg::with_fp_ftz<sf::FpFtz::FlushToZero>>        == D::FpMode);

// FpContract — 3 enumerators
static_assert(fxg::which_dim_v<fxg::with_fp_contract<sf::FpContract::Off>>          == D::FpMode);
static_assert(fxg::which_dim_v<fxg::with_fp_contract<sf::FpContract::OnInExpr>>     == D::FpMode);
static_assert(fxg::which_dim_v<fxg::with_fp_contract<sf::FpContract::Fast>>         == D::FpMode);

// FpReassociate — 3 enumerators
static_assert(fxg::which_dim_v<fxg::with_fp_reassociate<sf::FpReassociate::Forbidden>>           == D::FpMode);
static_assert(fxg::which_dim_v<fxg::with_fp_reassociate<sf::FpReassociate::BoundedTreeDepth>>    == D::FpMode);
static_assert(fxg::which_dim_v<fxg::with_fp_reassociate<sf::FpReassociate::UnrestrictedRewrite>> == D::FpMode);

// Aggregate
static_assert(fxg::which_dim_v<fxg::fp_strict_ieee> == D::FpMode);

// ═══════════════════════════════════════════════════════════════════════
// (c) ── Cross-sub-axis type distinctness — full 66-cell matrix ─────────
// ═══════════════════════════════════════════════════════════════════════
//
// Pairwise enumeration over the 12 grants.  Each cell witnesses that
// two grants engaging the SAME axis (FpMode) via DIFFERENT sub-axis
// types produce DISTINCT phantom types — Reject.h's duplicate-
// engagement detector must NOT collapse these.

template <typename A, typename B>
constexpr bool distinct = !std::is_same_v<A, B>;

// Concise aliases for readability below.
using R   = fxg::with_fp_rounding<sf::FpRounding::RoundToNearestEven>;
using Fz  = fxg::with_fp_ftz<sf::FpFtz::PreserveSubnormals>;
using Ct  = fxg::with_fp_contract<sf::FpContract::Off>;
using Tm  = fxg::with_fp_trap_mask<sf::FpTrapMask::AllMasked>;
using Di  = fxg::with_fp_denormal_input<sf::FpDenormalInput::HonorDenormals>;
using Np  = fxg::with_fp_nan_policy<sf::FpNanPolicy::PropagateQuiet>;
using Ip  = fxg::with_fp_inf_policy<sf::FpInfPolicy::PropagateInfinity>;
using Cl  = fxg::with_fp_complex_layout<sf::FpComplexLayout::Interleaved>;
using Lp  = fxg::with_fp_libm_policy<sf::FpLibmPolicy::ScalarLibm>;
using Re  = fxg::with_fp_reassociate<sf::FpReassociate::Forbidden>;
using Cr  = fxg::with_fp_constant_rounding<sf::FpConstantRounding::SameAsRuntime>;
using Sie = fxg::fp_strict_ieee;

// Row 1: R vs the other 11.
static_assert(distinct<R, Fz>);
static_assert(distinct<R, Ct>);
static_assert(distinct<R, Tm>);
static_assert(distinct<R, Di>);
static_assert(distinct<R, Np>);
static_assert(distinct<R, Ip>);
static_assert(distinct<R, Cl>);
static_assert(distinct<R, Lp>);
static_assert(distinct<R, Re>);
static_assert(distinct<R, Cr>);
static_assert(distinct<R, Sie>);

// Row 2: Fz vs the remaining 10.
static_assert(distinct<Fz, Ct>);
static_assert(distinct<Fz, Tm>);
static_assert(distinct<Fz, Di>);
static_assert(distinct<Fz, Np>);
static_assert(distinct<Fz, Ip>);
static_assert(distinct<Fz, Cl>);
static_assert(distinct<Fz, Lp>);
static_assert(distinct<Fz, Re>);
static_assert(distinct<Fz, Cr>);
static_assert(distinct<Fz, Sie>);

// Row 3: Ct vs the remaining 9.
static_assert(distinct<Ct, Tm>);
static_assert(distinct<Ct, Di>);
static_assert(distinct<Ct, Np>);
static_assert(distinct<Ct, Ip>);
static_assert(distinct<Ct, Cl>);
static_assert(distinct<Ct, Lp>);
static_assert(distinct<Ct, Re>);
static_assert(distinct<Ct, Cr>);
static_assert(distinct<Ct, Sie>);

// Row 4: Tm vs the remaining 8.
static_assert(distinct<Tm, Di>);
static_assert(distinct<Tm, Np>);
static_assert(distinct<Tm, Ip>);
static_assert(distinct<Tm, Cl>);
static_assert(distinct<Tm, Lp>);
static_assert(distinct<Tm, Re>);
static_assert(distinct<Tm, Cr>);
static_assert(distinct<Tm, Sie>);

// Row 5: Di vs the remaining 7.
static_assert(distinct<Di, Np>);
static_assert(distinct<Di, Ip>);
static_assert(distinct<Di, Cl>);
static_assert(distinct<Di, Lp>);
static_assert(distinct<Di, Re>);
static_assert(distinct<Di, Cr>);
static_assert(distinct<Di, Sie>);

// Row 6: Np vs the remaining 6.
static_assert(distinct<Np, Ip>);
static_assert(distinct<Np, Cl>);
static_assert(distinct<Np, Lp>);
static_assert(distinct<Np, Re>);
static_assert(distinct<Np, Cr>);
static_assert(distinct<Np, Sie>);

// Row 7: Ip vs the remaining 5.
static_assert(distinct<Ip, Cl>);
static_assert(distinct<Ip, Lp>);
static_assert(distinct<Ip, Re>);
static_assert(distinct<Ip, Cr>);
static_assert(distinct<Ip, Sie>);

// Row 8: Cl vs the remaining 4.
static_assert(distinct<Cl, Lp>);
static_assert(distinct<Cl, Re>);
static_assert(distinct<Cl, Cr>);
static_assert(distinct<Cl, Sie>);

// Row 9: Lp vs the remaining 3.
static_assert(distinct<Lp, Re>);
static_assert(distinct<Lp, Cr>);
static_assert(distinct<Lp, Sie>);

// Row 10: Re vs the remaining 2.
static_assert(distinct<Re, Cr>);
static_assert(distinct<Re, Sie>);

// Row 11: Cr vs the aggregate.
static_assert(distinct<Cr, Sie>);

// Total: 11 + 10 + 9 + 8 + 7 + 6 + 5 + 4 + 3 + 2 + 1 = 66 cells.

// ═══════════════════════════════════════════════════════════════════════
// (d) ── Within-sub-axis NTTP-distinctness — sample on every sub-axis ──
// ═══════════════════════════════════════════════════════════════════════
//
// For each of the 11 parametric grants, show that two instantiations
// differing only in the NTTP value produce distinct types.  This is
// the C++ template-instantiation rule baseline; sampling here
// witnesses that the per-axis grant DOES route the NTTP through to
// the type system (no accidental erasure).

static_assert(distinct<
    fxg::with_fp_rounding<sf::FpRounding::RoundToNearestEven>,
    fxg::with_fp_rounding<sf::FpRounding::RoundToZero>>);
static_assert(distinct<
    fxg::with_fp_ftz<sf::FpFtz::PreserveSubnormals>,
    fxg::with_fp_ftz<sf::FpFtz::FlushToZero>>);
static_assert(distinct<
    fxg::with_fp_contract<sf::FpContract::Off>,
    fxg::with_fp_contract<sf::FpContract::Fast>>);
static_assert(distinct<
    fxg::with_fp_trap_mask<sf::FpTrapMask::AllMasked>,
    fxg::with_fp_trap_mask<sf::FpTrapMask::UnmaskedDivZero>>);
static_assert(distinct<
    fxg::with_fp_denormal_input<sf::FpDenormalInput::HonorDenormals>,
    fxg::with_fp_denormal_input<sf::FpDenormalInput::DenormalsAreZero>>);
static_assert(distinct<
    fxg::with_fp_nan_policy<sf::FpNanPolicy::PropagateQuiet>,
    fxg::with_fp_nan_policy<sf::FpNanPolicy::PropagateSignalling>>);
static_assert(distinct<
    fxg::with_fp_inf_policy<sf::FpInfPolicy::PropagateInfinity>,
    fxg::with_fp_inf_policy<sf::FpInfPolicy::FlushInfToFinite>>);
static_assert(distinct<
    fxg::with_fp_complex_layout<sf::FpComplexLayout::Interleaved>,
    fxg::with_fp_complex_layout<sf::FpComplexLayout::SplitRealImag>>);
static_assert(distinct<
    fxg::with_fp_libm_policy<sf::FpLibmPolicy::ScalarLibm>,
    fxg::with_fp_libm_policy<sf::FpLibmPolicy::VectorLibmSleef>>);
static_assert(distinct<
    fxg::with_fp_reassociate<sf::FpReassociate::Forbidden>,
    fxg::with_fp_reassociate<sf::FpReassociate::UnrestrictedRewrite>>);
static_assert(distinct<
    fxg::with_fp_constant_rounding<sf::FpConstantRounding::SameAsRuntime>,
    fxg::with_fp_constant_rounding<sf::FpConstantRounding::AlwaysRTE>>);

// ═══════════════════════════════════════════════════════════════════════
// (e) ── cv-ref rejection — fixy-A4-033 discipline ─────────────────────
// ═══════════════════════════════════════════════════════════════════════
//
// IsGrantTag_v rejects cv-qualified and reference-qualified forms; the
// rejection flows through `is_same_v<G, remove_cvref_t<G>>` in
// Grant.h:165.  Every Fp grant inherits the discipline structurally.

static_assert(!fxg::IsGrantTag_v<const fxg::with_fp_rounding<sf::FpRounding::RoundToNearestEven>>);
static_assert(!fxg::IsGrantTag_v<volatile fxg::with_fp_rounding<sf::FpRounding::RoundToNearestEven>>);
static_assert(!fxg::IsGrantTag_v<fxg::with_fp_rounding<sf::FpRounding::RoundToNearestEven>&>);
static_assert(!fxg::IsGrantTag_v<fxg::with_fp_rounding<sf::FpRounding::RoundToNearestEven>&&>);
static_assert(!fxg::IsGrantTag_v<const fxg::fp_strict_ieee>);
static_assert(!fxg::IsGrantTag_v<fxg::fp_strict_ieee&>);
static_assert(!fxg::IsGrantTag_v<fxg::fp_strict_ieee&&>);

}  // namespace

int main() {
    // Runtime smoke — every grant tag is default-constructible (empty
    // class), passes by value at sizeof 1.  This keeps consteval/SFINAE
    // bugs from masking under pure static_assert per
    // feedback_algebra_runtime_smoke_test_discipline.
    [[maybe_unused]] fxg::with_fp_rounding<sf::FpRounding::RoundToNearestEven> g_r{};
    [[maybe_unused]] fxg::with_fp_ftz<sf::FpFtz::PreserveSubnormals>           g_f{};
    [[maybe_unused]] fxg::with_fp_contract<sf::FpContract::Off>                g_c{};
    [[maybe_unused]] fxg::with_fp_trap_mask<sf::FpTrapMask::AllMasked>         g_t{};
    [[maybe_unused]] fxg::with_fp_denormal_input<sf::FpDenormalInput::HonorDenormals> g_d{};
    [[maybe_unused]] fxg::with_fp_nan_policy<sf::FpNanPolicy::PropagateQuiet>  g_n{};
    [[maybe_unused]] fxg::with_fp_inf_policy<sf::FpInfPolicy::PropagateInfinity> g_i{};
    [[maybe_unused]] fxg::with_fp_complex_layout<sf::FpComplexLayout::Interleaved> g_x{};
    [[maybe_unused]] fxg::with_fp_libm_policy<sf::FpLibmPolicy::ScalarLibm>    g_l{};
    [[maybe_unused]] fxg::with_fp_reassociate<sf::FpReassociate::Forbidden>    g_e{};
    [[maybe_unused]] fxg::with_fp_constant_rounding<sf::FpConstantRounding::SameAsRuntime> g_k{};
    [[maybe_unused]] fxg::fp_strict_ieee                                       g_s{};
    return 0;
}
