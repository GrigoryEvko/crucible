#pragma once

// Floating-point evaluation policy is a meta-axis: eleven sub-axes together
// decide it.  The eleven parametric grants below each engage one sub-axis,
// and the aggregate grant engages all of them at the strict default.  Keeping
// them here rather than alongside the safety-lattice grants gives the family
// one grep target and leaves that header to the lattice axes.
//
// A which_dim specialization must appear syntactically inside namespace
// crucible::fixy::grant.  A nested namespace does not satisfy that rule, so
// this header reopens it.
//
// The axis is wrapper-only: no aggregator slot carries it.  A grant here is
// the engagement and federation marker, and the mode itself is pinned by a
// wrapper at the value site.  So a grant accepts any value in its enum's
// domain.  It declares which sub-axis the binding speaks to, not that the
// binding is strict.

#include <crucible/fixy/Grant.h>
#include <crucible/safety/FpMode.h>
#include <crucible/safety/DimensionTraits.h>

#include <type_traits>

namespace crucible::fixy::grant {

template <::crucible::safety::FpRounding Mode>
struct with_fp_rounding final : grant_base {};

template <::crucible::safety::FpRounding Mode>
struct which_dim<with_fp_rounding<Mode>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::FpMode> {};

template <::crucible::safety::FpFtz Mode>
struct with_fp_ftz final : grant_base {};

template <::crucible::safety::FpFtz Mode>
struct which_dim<with_fp_ftz<Mode>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::FpMode> {};

template <::crucible::safety::FpContract Mode>
struct with_fp_contract final : grant_base {};

template <::crucible::safety::FpContract Mode>
struct which_dim<with_fp_contract<Mode>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::FpMode> {};

template <::crucible::safety::FpTrapMask Mode>
struct with_fp_trap_mask final : grant_base {};

template <::crucible::safety::FpTrapMask Mode>
struct which_dim<with_fp_trap_mask<Mode>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::FpMode> {};

template <::crucible::safety::FpDenormalInput Mode>
struct with_fp_denormal_input final : grant_base {};

template <::crucible::safety::FpDenormalInput Mode>
struct which_dim<with_fp_denormal_input<Mode>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::FpMode> {};

template <::crucible::safety::FpNanPolicy Mode>
struct with_fp_nan_policy final : grant_base {};

template <::crucible::safety::FpNanPolicy Mode>
struct which_dim<with_fp_nan_policy<Mode>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::FpMode> {};

template <::crucible::safety::FpInfPolicy Mode>
struct with_fp_inf_policy final : grant_base {};

template <::crucible::safety::FpInfPolicy Mode>
struct which_dim<with_fp_inf_policy<Mode>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::FpMode> {};

template <::crucible::safety::FpComplexLayout Mode>
struct with_fp_complex_layout final : grant_base {};

template <::crucible::safety::FpComplexLayout Mode>
struct which_dim<with_fp_complex_layout<Mode>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::FpMode> {};

template <::crucible::safety::FpLibmPolicy Mode>
struct with_fp_libm_policy final : grant_base {};

template <::crucible::safety::FpLibmPolicy Mode>
struct which_dim<with_fp_libm_policy<Mode>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::FpMode> {};

template <::crucible::safety::FpReassociate Mode>
struct with_fp_reassociate final : grant_base {};

template <::crucible::safety::FpReassociate Mode>
struct which_dim<with_fp_reassociate<Mode>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::FpMode> {};

template <::crucible::safety::FpConstantRounding Mode>
struct with_fp_constant_rounding final : grant_base {};

template <::crucible::safety::FpConstantRounding Mode>
struct which_dim<with_fp_constant_rounding<Mode>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::FpMode> {};

// This is the one grant that opts into bit-exact reproducibility on every
// sub-axis at once.  A caller writes it instead of eleven parametric grants,
// and the engagement walk still sees a single engagement of the axis.

struct fp_strict_ieee final : grant_base {};

template <>
struct which_dim<fp_strict_ieee> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::FpMode> {};

namespace detail::fp_grant_self_test {

namespace sf = ::crucible::safety;
using D = dim::DimensionAxis;

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

static_assert(sizeof(with_fp_rounding<sf::FpRounding::RoundToNearestEven>) == 1);
static_assert(sizeof(with_fp_ftz<sf::FpFtz::PreserveSubnormals>) == 1);
static_assert(sizeof(with_fp_contract<sf::FpContract::Off>) == 1);
static_assert(sizeof(with_fp_trap_mask<sf::FpTrapMask::AllMasked>) == 1);
static_assert(sizeof(with_fp_denormal_input<sf::FpDenormalInput::HonorDenormals>) == 1);
static_assert(sizeof(with_fp_nan_policy<sf::FpNanPolicy::PropagateQuiet>) == 1);
static_assert(sizeof(with_fp_inf_policy<sf::FpInfPolicy::PropagateInfinity>) == 1);
static_assert(sizeof(with_fp_complex_layout<sf::FpComplexLayout::Interleaved>) == 1);
static_assert(sizeof(with_fp_libm_policy<sf::FpLibmPolicy::ScalarLibm>) == 1);
static_assert(sizeof(with_fp_reassociate<sf::FpReassociate::Forbidden>) == 1);
static_assert(sizeof(with_fp_constant_rounding<sf::FpConstantRounding::SameAsRuntime>) == 1);
static_assert(sizeof(fp_strict_ieee) == 1);

static_assert(which_dim_v<with_fp_rounding<sf::FpRounding::RoundToNearestEven>> == D::FpMode);
static_assert(which_dim_v<with_fp_ftz<sf::FpFtz::PreserveSubnormals>> == D::FpMode);
static_assert(which_dim_v<with_fp_contract<sf::FpContract::Off>> == D::FpMode);
static_assert(which_dim_v<with_fp_trap_mask<sf::FpTrapMask::AllMasked>> == D::FpMode);
static_assert(which_dim_v<with_fp_denormal_input<sf::FpDenormalInput::HonorDenormals>> == D::FpMode);
static_assert(which_dim_v<with_fp_nan_policy<sf::FpNanPolicy::PropagateQuiet>> == D::FpMode);
static_assert(which_dim_v<with_fp_inf_policy<sf::FpInfPolicy::PropagateInfinity>> == D::FpMode);
static_assert(which_dim_v<with_fp_complex_layout<sf::FpComplexLayout::Interleaved>> == D::FpMode);
static_assert(which_dim_v<with_fp_libm_policy<sf::FpLibmPolicy::ScalarLibm>> == D::FpMode);
static_assert(which_dim_v<with_fp_reassociate<sf::FpReassociate::Forbidden>> == D::FpMode);
static_assert(which_dim_v<with_fp_constant_rounding<sf::FpConstantRounding::SameAsRuntime>> == D::FpMode);
static_assert(which_dim_v<fp_strict_ieee> == D::FpMode);

static_assert(
    !std::is_same_v<with_fp_rounding<sf::FpRounding::RoundToNearestEven>, with_fp_ftz<sf::FpFtz::PreserveSubnormals>>);
static_assert(!std::is_same_v<with_fp_ftz<sf::FpFtz::PreserveSubnormals>, with_fp_contract<sf::FpContract::Off>>);
static_assert(!std::is_same_v<with_fp_contract<sf::FpContract::Off>, with_fp_trap_mask<sf::FpTrapMask::AllMasked>>);
static_assert(!std::is_same_v<with_fp_trap_mask<sf::FpTrapMask::AllMasked>,
                              with_fp_denormal_input<sf::FpDenormalInput::HonorDenormals>>);
static_assert(!std::is_same_v<with_fp_denormal_input<sf::FpDenormalInput::HonorDenormals>,
                              with_fp_nan_policy<sf::FpNanPolicy::PropagateQuiet>>);
static_assert(!std::is_same_v<with_fp_nan_policy<sf::FpNanPolicy::PropagateQuiet>,
                              with_fp_inf_policy<sf::FpInfPolicy::PropagateInfinity>>);
static_assert(!std::is_same_v<with_fp_inf_policy<sf::FpInfPolicy::PropagateInfinity>,
                              with_fp_complex_layout<sf::FpComplexLayout::Interleaved>>);
static_assert(!std::is_same_v<with_fp_complex_layout<sf::FpComplexLayout::Interleaved>,
                              with_fp_libm_policy<sf::FpLibmPolicy::ScalarLibm>>);
static_assert(!std::is_same_v<with_fp_libm_policy<sf::FpLibmPolicy::ScalarLibm>,
                              with_fp_reassociate<sf::FpReassociate::Forbidden>>);
static_assert(!std::is_same_v<with_fp_reassociate<sf::FpReassociate::Forbidden>,
                              with_fp_constant_rounding<sf::FpConstantRounding::SameAsRuntime>>);
static_assert(!std::is_same_v<with_fp_constant_rounding<sf::FpConstantRounding::SameAsRuntime>, fp_strict_ieee>);

static_assert(!std::is_same_v<with_fp_rounding<sf::FpRounding::RoundToNearestEven>,
                              with_fp_rounding<sf::FpRounding::RoundToZero>>);
static_assert(!std::is_same_v<with_fp_ftz<sf::FpFtz::PreserveSubnormals>, with_fp_ftz<sf::FpFtz::FlushToZero>>);
static_assert(!std::is_same_v<with_fp_contract<sf::FpContract::Off>, with_fp_contract<sf::FpContract::Fast>>);
static_assert(!std::is_same_v<with_fp_reassociate<sf::FpReassociate::Forbidden>,
                              with_fp_reassociate<sf::FpReassociate::UnrestrictedRewrite>>);

static_assert(!IsGrantTag_v<const with_fp_rounding<sf::FpRounding::RoundToNearestEven>>);
static_assert(!IsGrantTag_v<with_fp_rounding<sf::FpRounding::RoundToNearestEven>&>);
static_assert(!IsGrantTag_v<const fp_strict_ieee>);
static_assert(!IsGrantTag_v<fp_strict_ieee&>);

}  // namespace detail::fp_grant_self_test

}  // namespace crucible::fixy::grant

// The composite carrying all eleven pinned sub-axes, and its mint, live in
// crucible::safety.  Both names come across as using-declarations rather
// than re-declarations, so size, row hash and nesting order are unchanged.

namespace crucible::fixy::wrap {

using ::crucible::safety::FpModeComposite;
using ::crucible::safety::mint_fp_mode_composite;

namespace fp_composite_self_test {

namespace sf = ::crucible::safety;

// The same strict pin the fp_strict_ieee grant above denotes.
using strict_ieee_composite =
    FpModeComposite<sf::FpRounding::RoundToNearestEven, sf::FpFtz::PreserveSubnormals, sf::FpContract::Off,
                    sf::FpTrapMask::AllMasked, sf::FpDenormalInput::HonorDenormals, sf::FpNanPolicy::PropagateQuiet,
                    sf::FpInfPolicy::PropagateInfinity, sf::FpComplexLayout::Interleaved, sf::FpLibmPolicy::ScalarLibm,
                    sf::FpReassociate::Forbidden, sf::FpConstantRounding::SameAsRuntime, double>;

static_assert(std::is_same_v<strict_ieee_composite,
                             sf::FpModeComposite<sf::FpRounding::RoundToNearestEven, sf::FpFtz::PreserveSubnormals,
                                                 sf::FpContract::Off, sf::FpTrapMask::AllMasked,
                                                 sf::FpDenormalInput::HonorDenormals, sf::FpNanPolicy::PropagateQuiet,
                                                 sf::FpInfPolicy::PropagateInfinity, sf::FpComplexLayout::Interleaved,
                                                 sf::FpLibmPolicy::ScalarLibm, sf::FpReassociate::Forbidden,
                                                 sf::FpConstantRounding::SameAsRuntime, double>>,
              "fixy::wrap::FpModeComposite must alias the safety substrate type");

static_assert(
    std::is_same_v<
        decltype(&mint_fp_mode_composite<
                 sf::FpRounding::RoundToNearestEven, sf::FpFtz::PreserveSubnormals, sf::FpContract::Off,
                 sf::FpTrapMask::AllMasked, sf::FpDenormalInput::HonorDenormals, sf::FpNanPolicy::PropagateQuiet,
                 sf::FpInfPolicy::PropagateInfinity, sf::FpComplexLayout::Interleaved, sf::FpLibmPolicy::ScalarLibm,
                 sf::FpReassociate::Forbidden, sf::FpConstantRounding::SameAsRuntime, double, double>),
        decltype(&sf::mint_fp_mode_composite<
                 sf::FpRounding::RoundToNearestEven, sf::FpFtz::PreserveSubnormals, sf::FpContract::Off,
                 sf::FpTrapMask::AllMasked, sf::FpDenormalInput::HonorDenormals, sf::FpNanPolicy::PropagateQuiet,
                 sf::FpInfPolicy::PropagateInfinity, sf::FpComplexLayout::Interleaved, sf::FpLibmPolicy::ScalarLibm,
                 sf::FpReassociate::Forbidden, sf::FpConstantRounding::SameAsRuntime, double, double>)>,
    "fixy::wrap::mint_fp_mode_composite must have the substrate signature");

}  // namespace fp_composite_self_test

}  // namespace crucible::fixy::wrap
