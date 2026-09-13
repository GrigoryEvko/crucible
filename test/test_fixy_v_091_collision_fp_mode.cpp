// Five cross-axis collision rules, each guarding a property that a pinned
// floating-point mode would defeat:
//
//   F101  replay-required with unrestricted reassociation. Algebraic rewrite
//         reorders additions, so the bit pattern differs between vendors and
//         bit-exact replay is lost.
//   F102  replay-required with fast contraction. Cross-statement fused
//         multiply-add picks different boundaries per vendor, so one source
//         yields different bits.
//   F103  constant-time with unrestricted reassociation. Reassociation makes
//         the reduction-tree topology data-dependent.
//   F104  constant-time with honoured denormal inputs. A denormal input then
//         costs materially more than a normal one, which is a timing side
//         channel.
//   F105  constant-time with preserved subnormal outputs. The output-side
//         dual of F104, where result magnitude leaks through cycle count.

#include <crucible/safety/Fn.h>
#include <crucible/safety/FpMode.h>

#include <string_view>
#include <type_traits>

namespace cs = ::crucible::safety;
namespace csfn = ::crucible::safety::fn;
namespace csc = ::crucible::safety::fn::collision;

namespace {

// A floor rather than an equality, so appending a catalog entry does not
// redden this file. Removing one still does, and the per-rule bijection
// assertions below pin each entry individually.
static_assert(csc::catalog_size >= 28, "the catalog must still carry F101 through F105 and M001");
static_assert(std::tuple_size_v<csc::Catalog> >= 28);
static_assert(csc::rule_bijection_v<csc::RuleCode::F101>);
static_assert(csc::rule_bijection_v<csc::RuleCode::F102>);
static_assert(csc::rule_bijection_v<csc::RuleCode::F103>);
static_assert(csc::rule_bijection_v<csc::RuleCode::F104>);
static_assert(csc::rule_bijection_v<csc::RuleCode::F105>);
static_assert(csc::rule_bijection_v<csc::RuleCode::M001>);

using DefaultFn = csfn::Fn<int>;
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::F101>::rule_code() == std::string_view{"F101"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::F102>::rule_code() == std::string_view{"F102"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::F103>::rule_code() == std::string_view{"F103"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::F104>::rule_code() == std::string_view{"F104"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::F105>::rule_code() == std::string_view{"F105"});

// One detector serves all eleven sub-axes. It matches on FpModePinned<Mode, U>
// with the probed NTTP constrained to Mode's own type, so probing a rounding
// wrapper for a reassociation mode falls back to false rather than matching on
// the enumerator value alone.
using ReassocPermittedF = cs::FpReassociatePinned<cs::FpReassociate::UnrestrictedRewrite, float>;
using ReassocPermittedD = cs::FpReassociatePinned<cs::FpReassociate::UnrestrictedRewrite, double>;
using ReassocForbidden = cs::FpReassociatePinned<cs::FpReassociate::Forbidden, float>;
using ContractFast = cs::FpContractPinned<cs::FpContract::Fast, float>;
using ContractOff = cs::FpContractPinned<cs::FpContract::Off, float>;
using FtzPreserved = cs::FpFtzPinned<cs::FpFtz::PreserveSubnormals, float>;
using FtzFlushed = cs::FpFtzPinned<cs::FpFtz::FlushToZero, float>;
using DenormalHonored = cs::FpDenormalInputPinned<cs::FpDenormalInput::HonorDenormals, float>;
using DenormalDaz = cs::FpDenormalInputPinned<cs::FpDenormalInput::DenormalsAreZero, float>;
using RoundingRTNE = cs::FpRoundingPinned<cs::FpRounding::RoundToNearestEven, float>;

static_assert(csc::wraps_fp_axis_mode_v<cs::FpReassociate::UnrestrictedRewrite, ReassocPermittedF>);
static_assert(csc::wraps_fp_axis_mode_v<cs::FpReassociate::UnrestrictedRewrite, ReassocPermittedD>);
static_assert(csc::wraps_fp_axis_mode_v<cs::FpContract::Fast, ContractFast>);
static_assert(csc::wraps_fp_axis_mode_v<cs::FpFtz::PreserveSubnormals, FtzPreserved>);
static_assert(csc::wraps_fp_axis_mode_v<cs::FpDenormalInput::HonorDenormals, DenormalHonored>);

// A cv-qualified or reference spelling carries the same mode, so the detector
// must pierce them.
static_assert(csc::wraps_fp_axis_mode_v<cs::FpReassociate::UnrestrictedRewrite, ReassocPermittedF const&>);
static_assert(csc::wraps_fp_axis_mode_v<cs::FpReassociate::UnrestrictedRewrite, ReassocPermittedF&>);
static_assert(csc::wraps_fp_axis_mode_v<cs::FpReassociate::UnrestrictedRewrite, ReassocPermittedF const>);

// Right axis, wrong mode value.
static_assert(!csc::wraps_fp_axis_mode_v<cs::FpReassociate::UnrestrictedRewrite, ReassocForbidden>);
static_assert(!csc::wraps_fp_axis_mode_v<cs::FpContract::Fast, ContractOff>);
static_assert(!csc::wraps_fp_axis_mode_v<cs::FpFtz::PreserveSubnormals, FtzFlushed>);
static_assert(!csc::wraps_fp_axis_mode_v<cs::FpDenormalInput::HonorDenormals, DenormalDaz>);

// Wrong axis. These close the hole where a detector keys on the enumerator
// value and accepts a wrapper that pins a different sub-axis.
static_assert(!csc::wraps_fp_axis_mode_v<cs::FpReassociate::UnrestrictedRewrite, RoundingRTNE>);
static_assert(!csc::wraps_fp_axis_mode_v<cs::FpContract::Fast, RoundingRTNE>);
static_assert(!csc::wraps_fp_axis_mode_v<cs::FpFtz::PreserveSubnormals, RoundingRTNE>);
static_assert(!csc::wraps_fp_axis_mode_v<cs::FpDenormalInput::HonorDenormals, ReassocPermittedF>);

// No wrapper at all.
static_assert(!csc::wraps_fp_axis_mode_v<cs::FpReassociate::UnrestrictedRewrite, float>);
static_assert(!csc::wraps_fp_axis_mode_v<cs::FpReassociate::UnrestrictedRewrite, int>);
static_assert(!csc::wraps_fp_axis_mode_v<cs::FpContract::Fast, double>);

// Exactly five combinations are rejected. The passing cases below are what
// catches a rule drawn too wide.

// Each rule needs both the toxic mode and the axis marker. A carrier with the
// mode alone trips nothing.
using NeutralReassocPermitted = csfn::Fn<ReassocPermittedF>;
static_assert(csfn::ValidComposition<NeutralReassocPermitted>);
static_assert(csc::first_failure_v<NeutralReassocPermitted> == csc::RuleCode::None);

using NeutralContractFast = csfn::Fn<ContractFast>;
static_assert(csfn::ValidComposition<NeutralContractFast>);

using NeutralDenormalHonored = csfn::Fn<DenormalHonored>;
static_assert(csfn::ValidComposition<NeutralDenormalHonored>);

using NeutralFtzPreserved = csfn::Fn<FtzPreserved>;
static_assert(csfn::ValidComposition<NeutralFtzPreserved>);

// The safe mode of each axis, which stays admissible even where the marker is
// present. These run the whole conjunction inside validate() without tripping
// any term of it.
using FnReassocForbidden = csfn::Fn<ReassocForbidden>;
using FnContractOff = csfn::Fn<ContractOff>;
using FnDenormalDaz = csfn::Fn<DenormalDaz>;
using FnFtzFlushed = csfn::Fn<FtzFlushed>;
static_assert(csfn::ValidComposition<FnReassocForbidden>);
static_assert(csfn::ValidComposition<FnContractOff>);
static_assert(csfn::ValidComposition<FnDenormalDaz>);
static_assert(csfn::ValidComposition<FnFtzFlushed>);

// A carrier that genuinely trips one of these rules cannot be named in this
// file at all. Instantiating it fires the composition assertion inside Fn
// itself, before first_failure_v could be queried. The rejections are
// therefore witnessed by compile-failure fixtures, and the diagnostic strings
// above are pinned through a carrier that trips nothing.

}  // namespace

// Everything above is compile-time. main() exists because the target is
// linked and run as an executable.
int main() { return 0; }
