// FpMode carries no aggregator template-parameter slot.  The per-axis
// pinning happens at the value site instead.  These grant tags are the
// federation and reflection channel that declares which sub-axis mode a
// binding's value sites pin.  The engagement walk reads which_dim_v
// uniformly across every axis, which is how an FpMode grant becomes
// visible to it.

#include <crucible/fixy/Fp.h>
#include <crucible/fixy/Reject.h>

#include <type_traits>
#include <utility>

namespace fxg = ::crucible::fixy::grant;
namespace fxd = ::crucible::fixy::dim;
namespace sf = ::crucible::safety;

using D = fxd::DimensionAxis;

namespace {

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

// Every enumerator of the four most NTTP-sensitive sub-axes is bound, so
// a copy-paste error in one per-axis which_dim specialization surfaces
// here.  The remaining sub-axes are covered by the sampling above.

static_assert(fxg::which_dim_v<fxg::with_fp_rounding<sf::FpRounding::RoundToZero>> == D::FpMode);
static_assert(fxg::which_dim_v<fxg::with_fp_rounding<sf::FpRounding::RoundToNegativeInf>> == D::FpMode);
static_assert(fxg::which_dim_v<fxg::with_fp_rounding<sf::FpRounding::RoundToPositiveInf>> == D::FpMode);
static_assert(fxg::which_dim_v<fxg::with_fp_rounding<sf::FpRounding::RoundToNearestEven>> == D::FpMode);
static_assert(fxg::which_dim_v<fxg::with_fp_rounding<sf::FpRounding::RoundToNearestAwayZero>> == D::FpMode);

static_assert(fxg::which_dim_v<fxg::with_fp_ftz<sf::FpFtz::PreserveSubnormals>> == D::FpMode);
static_assert(fxg::which_dim_v<fxg::with_fp_ftz<sf::FpFtz::FlushToZero>> == D::FpMode);

static_assert(fxg::which_dim_v<fxg::with_fp_contract<sf::FpContract::Off>> == D::FpMode);
static_assert(fxg::which_dim_v<fxg::with_fp_contract<sf::FpContract::OnInExpr>> == D::FpMode);
static_assert(fxg::which_dim_v<fxg::with_fp_contract<sf::FpContract::Fast>> == D::FpMode);

static_assert(fxg::which_dim_v<fxg::with_fp_reassociate<sf::FpReassociate::Forbidden>> == D::FpMode);
static_assert(fxg::which_dim_v<fxg::with_fp_reassociate<sf::FpReassociate::BoundedTreeDepth>> == D::FpMode);
static_assert(fxg::which_dim_v<fxg::with_fp_reassociate<sf::FpReassociate::UnrestrictedRewrite>> == D::FpMode);

static_assert(fxg::which_dim_v<fxg::fp_strict_ieee> == D::FpMode);

// The full pairwise matrix over the twelve grants.  Two grants that
// engage the same axis through different sub-axis types must stay
// distinct phantom types.  The duplicate-engagement detector collapses
// identical types, which would silently reduce the engagement signal.

template <typename A, typename B>
constexpr bool distinct = !std::is_same_v<A, B>;

using R = fxg::with_fp_rounding<sf::FpRounding::RoundToNearestEven>;
using Fz = fxg::with_fp_ftz<sf::FpFtz::PreserveSubnormals>;
using Ct = fxg::with_fp_contract<sf::FpContract::Off>;
using Tm = fxg::with_fp_trap_mask<sf::FpTrapMask::AllMasked>;
using Di = fxg::with_fp_denormal_input<sf::FpDenormalInput::HonorDenormals>;
using Np = fxg::with_fp_nan_policy<sf::FpNanPolicy::PropagateQuiet>;
using Ip = fxg::with_fp_inf_policy<sf::FpInfPolicy::PropagateInfinity>;
using Cl = fxg::with_fp_complex_layout<sf::FpComplexLayout::Interleaved>;
using Lp = fxg::with_fp_libm_policy<sf::FpLibmPolicy::ScalarLibm>;
using Re = fxg::with_fp_reassociate<sf::FpReassociate::Forbidden>;
using Cr = fxg::with_fp_constant_rounding<sf::FpConstantRounding::SameAsRuntime>;
using Sie = fxg::fp_strict_ieee;

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

static_assert(distinct<Ct, Tm>);
static_assert(distinct<Ct, Di>);
static_assert(distinct<Ct, Np>);
static_assert(distinct<Ct, Ip>);
static_assert(distinct<Ct, Cl>);
static_assert(distinct<Ct, Lp>);
static_assert(distinct<Ct, Re>);
static_assert(distinct<Ct, Cr>);
static_assert(distinct<Ct, Sie>);

static_assert(distinct<Tm, Di>);
static_assert(distinct<Tm, Np>);
static_assert(distinct<Tm, Ip>);
static_assert(distinct<Tm, Cl>);
static_assert(distinct<Tm, Lp>);
static_assert(distinct<Tm, Re>);
static_assert(distinct<Tm, Cr>);
static_assert(distinct<Tm, Sie>);

static_assert(distinct<Di, Np>);
static_assert(distinct<Di, Ip>);
static_assert(distinct<Di, Cl>);
static_assert(distinct<Di, Lp>);
static_assert(distinct<Di, Re>);
static_assert(distinct<Di, Cr>);
static_assert(distinct<Di, Sie>);

static_assert(distinct<Np, Ip>);
static_assert(distinct<Np, Cl>);
static_assert(distinct<Np, Lp>);
static_assert(distinct<Np, Re>);
static_assert(distinct<Np, Cr>);
static_assert(distinct<Np, Sie>);

static_assert(distinct<Ip, Cl>);
static_assert(distinct<Ip, Lp>);
static_assert(distinct<Ip, Re>);
static_assert(distinct<Ip, Cr>);
static_assert(distinct<Ip, Sie>);

static_assert(distinct<Cl, Lp>);
static_assert(distinct<Cl, Re>);
static_assert(distinct<Cl, Cr>);
static_assert(distinct<Cl, Sie>);

static_assert(distinct<Lp, Re>);
static_assert(distinct<Lp, Cr>);
static_assert(distinct<Lp, Sie>);

static_assert(distinct<Re, Cr>);
static_assert(distinct<Re, Sie>);

static_assert(distinct<Cr, Sie>);

// Two instantiations of one grant that differ only in the NTTP value must
// stay distinct types.  That witnesses the grant routing the NTTP into the
// type system rather than erasing it.

static_assert(distinct<fxg::with_fp_rounding<sf::FpRounding::RoundToNearestEven>,
                       fxg::with_fp_rounding<sf::FpRounding::RoundToZero>>);
static_assert(distinct<fxg::with_fp_ftz<sf::FpFtz::PreserveSubnormals>, fxg::with_fp_ftz<sf::FpFtz::FlushToZero>>);
static_assert(distinct<fxg::with_fp_contract<sf::FpContract::Off>, fxg::with_fp_contract<sf::FpContract::Fast>>);
static_assert(distinct<fxg::with_fp_trap_mask<sf::FpTrapMask::AllMasked>,
                       fxg::with_fp_trap_mask<sf::FpTrapMask::UnmaskedDivZero>>);
static_assert(distinct<fxg::with_fp_denormal_input<sf::FpDenormalInput::HonorDenormals>,
                       fxg::with_fp_denormal_input<sf::FpDenormalInput::DenormalsAreZero>>);
static_assert(distinct<fxg::with_fp_nan_policy<sf::FpNanPolicy::PropagateQuiet>,
                       fxg::with_fp_nan_policy<sf::FpNanPolicy::PropagateSignalling>>);
static_assert(distinct<fxg::with_fp_inf_policy<sf::FpInfPolicy::PropagateInfinity>,
                       fxg::with_fp_inf_policy<sf::FpInfPolicy::FlushInfToFinite>>);
static_assert(distinct<fxg::with_fp_complex_layout<sf::FpComplexLayout::Interleaved>,
                       fxg::with_fp_complex_layout<sf::FpComplexLayout::SplitRealImag>>);
static_assert(distinct<fxg::with_fp_libm_policy<sf::FpLibmPolicy::ScalarLibm>,
                       fxg::with_fp_libm_policy<sf::FpLibmPolicy::VectorLibmSleef>>);
static_assert(distinct<fxg::with_fp_reassociate<sf::FpReassociate::Forbidden>,
                       fxg::with_fp_reassociate<sf::FpReassociate::UnrestrictedRewrite>>);
static_assert(distinct<fxg::with_fp_constant_rounding<sf::FpConstantRounding::SameAsRuntime>,
                       fxg::with_fp_constant_rounding<sf::FpConstantRounding::AlwaysRTE>>);

// The cv-ref rejection is structural rather than written per grant, so
// one parametric grant plus the aggregate covers all twelve.

static_assert(!fxg::IsGrantTag_v<const fxg::with_fp_rounding<sf::FpRounding::RoundToNearestEven>>);
static_assert(!fxg::IsGrantTag_v<volatile fxg::with_fp_rounding<sf::FpRounding::RoundToNearestEven>>);
static_assert(!fxg::IsGrantTag_v<fxg::with_fp_rounding<sf::FpRounding::RoundToNearestEven>&>);
static_assert(!fxg::IsGrantTag_v<fxg::with_fp_rounding<sf::FpRounding::RoundToNearestEven>&&>);
static_assert(!fxg::IsGrantTag_v<const fxg::fp_strict_ieee>);
static_assert(!fxg::IsGrantTag_v<fxg::fp_strict_ieee&>);
static_assert(!fxg::IsGrantTag_v<fxg::fp_strict_ieee&&>);

}  // namespace

int main() {
    // A pure static_assert can mask a consteval or SFINAE bug that only
    // surfaces once the tag is instantiated at a runtime-eligible site.
    [[maybe_unused]] fxg::with_fp_rounding<sf::FpRounding::RoundToNearestEven> g_r{};
    [[maybe_unused]] fxg::with_fp_ftz<sf::FpFtz::PreserveSubnormals> g_f{};
    [[maybe_unused]] fxg::with_fp_contract<sf::FpContract::Off> g_c{};
    [[maybe_unused]] fxg::with_fp_trap_mask<sf::FpTrapMask::AllMasked> g_t{};
    [[maybe_unused]] fxg::with_fp_denormal_input<sf::FpDenormalInput::HonorDenormals> g_d{};
    [[maybe_unused]] fxg::with_fp_nan_policy<sf::FpNanPolicy::PropagateQuiet> g_n{};
    [[maybe_unused]] fxg::with_fp_inf_policy<sf::FpInfPolicy::PropagateInfinity> g_i{};
    [[maybe_unused]] fxg::with_fp_complex_layout<sf::FpComplexLayout::Interleaved> g_x{};
    [[maybe_unused]] fxg::with_fp_libm_policy<sf::FpLibmPolicy::ScalarLibm> g_l{};
    [[maybe_unused]] fxg::with_fp_reassociate<sf::FpReassociate::Forbidden> g_e{};
    [[maybe_unused]] fxg::with_fp_constant_rounding<sf::FpConstantRounding::SameAsRuntime> g_k{};
    [[maybe_unused]] fxg::fp_strict_ieee g_s{};
    return 0;
}
