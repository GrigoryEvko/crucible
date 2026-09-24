// FpMode is a separate axis rather than part of Precision. Precision tracks
// the element type, and FpMode tracks the evaluation policy: rounding,
// flush-to-zero, contraction, NaN handling and the rest. Two operations on the
// same element type produce different bits under different evaluation
// policies, so folding the two axes together would leave no way to canonicalize
// floating point for a content hash.

#include <crucible/algebra/lattices/FpModeLattice.h>
#include <crucible/safety/DimensionTraits.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace cs = ::crucible::safety;
namespace cal = ::crucible::algebra::lattices;

namespace {

static_assert(std::to_underlying(cs::DimensionAxis::FpMode) == 22,
              "DimensionAxis::FpMode must be ordinal 22. The axis enumeration is "
              "append-only and earlier ordinals are never reused.");

static_assert(cs::tier_of_axis(cs::DimensionAxis::FpMode) == cs::TierKind::Semiring,
              "FpMode belongs on the semiring tier, where parallel composition is "
              "the join and the strictest mode wins. Recipe gating downstream "
              "depends on that classification.");
static_assert(cs::tier_of_axis_v<cs::DimensionAxis::FpMode> == cs::TierKind::Semiring,
              "the variable-template form of tier_of_axis must agree with the "
              "function form.");

static_assert(cs::dimension_axis_name(cs::DimensionAxis::FpMode) == std::string_view{"FpMode"},
              "dimension_axis_name must return the axis name. A sentinel coming "
              "back means the switch is missing an arm.");

// A floor, not the exact count. The exact pin sits beside the enum it counts,
// where anyone appending an axis has to see it. This one catches the other
// direction: an axis removed without review.
static_assert(cs::DIMENSION_AXIS_COUNT >= 23, "the DimensionAxis cardinality regressed below 23, so an axis was "
                                              "removed without the paired exact pin being updated.");

// One axis from each tier, re-witnessed. A switch arm that returned the wrong
// tier for FpMode would leave the cardinality check above satisfied, so only a
// per-axis check rules that out.
static_assert(cs::tier_of_axis(cs::DimensionAxis::Type) == cs::TierKind::Foundational);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Refinement) == cs::TierKind::Foundational);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Protocol) == cs::TierKind::Typestate);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Representation) == cs::TierKind::Lattice);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Version) == cs::TierKind::Versioned);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Synchronization) == cs::TierKind::Semiring);
static_assert(cs::tier_of_axis(cs::DimensionAxis::Regime) == cs::TierKind::Semiring);

static_assert(cal::detail::fp_mode_lattice_self_test::rounding_count == 5);
static_assert(cal::detail::fp_mode_lattice_self_test::ftz_count == 2);
static_assert(cal::detail::fp_mode_lattice_self_test::contract_count == 3);
static_assert(cal::detail::fp_mode_lattice_self_test::trap_mask_count == 6);
static_assert(cal::detail::fp_mode_lattice_self_test::denormal_input_count == 2);
static_assert(cal::detail::fp_mode_lattice_self_test::nan_policy_count == 3);
static_assert(cal::detail::fp_mode_lattice_self_test::inf_policy_count == 2);
static_assert(cal::detail::fp_mode_lattice_self_test::complex_layout_count == 3);
static_assert(cal::detail::fp_mode_lattice_self_test::libm_policy_count == 7);  // the seventh is the polynomial policy
static_assert(cal::detail::fp_mode_lattice_self_test::reassociate_count == 3);
static_assert(cal::detail::fp_mode_lattice_self_test::fp_constant_count == 3);

// Distinct types, so mixing a value of one sub-axis into another is a compile
// error rather than a silent wrong answer.
static_assert(!std::is_same_v<cal::FpRounding, cal::FpFtz>);
static_assert(!std::is_same_v<cal::FpContract, cal::FpReassociate>);
static_assert(!std::is_same_v<cal::FpTrapMask, cal::FpNanPolicy>);
static_assert(!std::is_same_v<cal::FpComplexLayout, cal::FpLibmPolicy>);
static_assert(!std::is_same_v<cal::FpDenormalInput, cal::FpInfPolicy>);
static_assert(!std::is_same_v<cal::FpRounding, cal::FpConstantRounding>);

// The chain ordering on each sub-axis is derived from the underlying value,
// so every sub-axis has to carry the same width for that derivation to behave
// uniformly.
static_assert(std::is_same_v<std::underlying_type_t<cal::FpRounding>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<cal::FpFtz>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<cal::FpContract>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<cal::FpTrapMask>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<cal::FpDenormalInput>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<cal::FpNanPolicy>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<cal::FpInfPolicy>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<cal::FpComplexLayout>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<cal::FpLibmPolicy>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<cal::FpReassociate>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<cal::FpConstantRounding>, std::uint8_t>);

// Ordinal zero is the weakest, least constraining element of every sub-axis.
// The lattice bottom is derived mechanically from that convention, so the
// convention has to hold on every one of them.
static_assert(std::to_underlying(cal::FpRounding::RoundToZero) == 0);
static_assert(std::to_underlying(cal::FpFtz::PreserveSubnormals) == 0);
static_assert(std::to_underlying(cal::FpContract::Off) == 0);
static_assert(std::to_underlying(cal::FpTrapMask::AllMasked) == 0);
static_assert(std::to_underlying(cal::FpDenormalInput::HonorDenormals) == 0);
static_assert(std::to_underlying(cal::FpNanPolicy::PropagateQuiet) == 0);
static_assert(std::to_underlying(cal::FpInfPolicy::PropagateInfinity) == 0);
static_assert(std::to_underlying(cal::FpComplexLayout::Interleaved) == 0);
static_assert(std::to_underlying(cal::FpLibmPolicy::ScalarLibm) == 0);
static_assert(std::to_underlying(cal::FpReassociate::Forbidden) == 0);
static_assert(std::to_underlying(cal::FpConstantRounding::SameAsRuntime) == 0);

}  // namespace

int main() { return 0; }
