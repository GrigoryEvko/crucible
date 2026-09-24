// Eight cross-axis collision rules over the hazard axes, each firing where a
// hazard declaration contradicts another axis of the same binding.
//
// They split into two kinds. Four read a wrapper tier straight off the
// carrier's type and can therefore fire today. The other four read an opt-in
// marker trait that no binding sets yet, so they stay dormant and safe until
// something opts in. Only the first kind can be exercised from a positive
// composition here.

#include <crucible/safety/ControlFlow.h>
#include <crucible/safety/Fn.h>
#include <crucible/safety/Stdio.h>

#include <array>
#include <string_view>
#include <type_traits>
#include <utility>

namespace cs = ::crucible::safety;
namespace csfn = ::crucible::safety::fn;
namespace csc = ::crucible::safety::fn::collision;
using CF = ::crucible::algebra::lattices::ControlFlow;
using SIO = ::crucible::algebra::lattices::Stdio;

namespace {

// A floor rather than an equality, so appending a catalog entry does not
// redden this file while removing one still does.
static_assert(csc::catalog_size >= 36, "the catalog must still carry all eight hazard-axis rules");
static_assert(std::tuple_size_v<csc::Catalog> >= 36);
static_assert(csc::rule_bijection_v<csc::RuleCode::C001>);
static_assert(csc::rule_bijection_v<csc::RuleCode::D001>);
static_assert(csc::rule_bijection_v<csc::RuleCode::D002>);
static_assert(csc::rule_bijection_v<csc::RuleCode::G001>);
static_assert(csc::rule_bijection_v<csc::RuleCode::L006>);
static_assert(csc::rule_bijection_v<csc::RuleCode::P003>);
static_assert(csc::rule_bijection_v<csc::RuleCode::S001>);
static_assert(csc::rule_bijection_v<csc::RuleCode::S004>);

using DefaultFn = csfn::Fn<int>;
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::C001>::rule_code() == std::string_view{"C001"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::D001>::rule_code() == std::string_view{"D001"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::D002>::rule_code() == std::string_view{"D002"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::G001>::rule_code() == std::string_view{"G001"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::L006>::rule_code() == std::string_view{"L006"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::P003>::rule_code() == std::string_view{"P003"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::S001>::rule_code() == std::string_view{"S001"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::S004>::rule_code() == std::string_view{"S004"});

using CfPure = cs::ControlFlowPinned<CF::Pure, int>;
using CfAbort = cs::ControlFlowPinned<CF::AbortOnly, int>;
using CfThrow = cs::ControlFlowPinned<CF::ThrowOnly, int>;
using CfLongjmp = cs::ControlFlowPinned<CF::MayLongjmp, int>;
using CfSignal = cs::ControlFlowPinned<CF::MaySignal, int>;
using StdioNone = cs::StdioPinned<SIO::NoStdio, int>;
using StdioBuf = cs::StdioPinned<SIO::BufferedWrite, int>;
using StdioRead = cs::StdioPinned<SIO::InteractiveRead, int>;

static_assert(csc::control_flow_tier_of<CfAbort>::has_cf);
static_assert(csc::control_flow_tier_of<CfAbort>::value == CF::AbortOnly);
static_assert(!csc::control_flow_tier_of<int>::has_cf);
static_assert(!csc::control_flow_tier_of<StdioBuf>::has_cf);  // wrong wrapper family
static_assert(csc::stdio_tier_of<StdioBuf>::has_stdio);
static_assert(csc::stdio_tier_of<StdioBuf>::value == SIO::BufferedWrite);
static_assert(!csc::stdio_tier_of<int>::has_stdio);
static_assert(!csc::stdio_tier_of<CfAbort>::has_stdio);

// A cv-qualified or reference spelling declares the same hazard, so the
// detector must pierce them.
static_assert(csc::control_flow_tier_of<CfAbort const&>::has_cf);
static_assert(csc::control_flow_tier_of<CfAbort&>::value == CF::AbortOnly);
static_assert(csc::stdio_tier_of<StdioBuf const>::has_stdio);

static_assert(!csc::cf_at_or_above_v<CF::AbortOnly, CfPure>);  // Pure < AbortOnly
static_assert(csc::cf_at_or_above_v<CF::AbortOnly, CfAbort>);  // == floor
static_assert(csc::cf_at_or_above_v<CF::AbortOnly, CfThrow>);  // above
static_assert(csc::cf_at_or_above_v<CF::AbortOnly, CfSignal>);  // top
static_assert(!csc::cf_at_or_above_v<CF::AbortOnly, int>);  // no wrapper → false

// The MayLongjmp floor admits only MayLongjmp and MaySignal.
static_assert(!csc::cf_at_or_above_v<CF::MayLongjmp, CfThrow>);
static_assert(csc::cf_at_or_above_v<CF::MayLongjmp, CfLongjmp>);
static_assert(csc::cf_at_or_above_v<CF::MayLongjmp, CfSignal>);

static_assert(!csc::cf_at_or_above_v<CF::ThrowOnly, CfAbort>);
static_assert(csc::cf_at_or_above_v<CF::ThrowOnly, CfThrow>);

static_assert(!csc::stdio_at_or_above_v<SIO::BufferedWrite, StdioNone>);  // NoStdio < BufferedWrite
static_assert(csc::stdio_at_or_above_v<SIO::BufferedWrite, StdioBuf>);
static_assert(csc::stdio_at_or_above_v<SIO::BufferedWrite, StdioRead>);  // above
static_assert(!csc::stdio_at_or_above_v<SIO::BufferedWrite, int>);  // no wrapper → false

using Edge = std::pair<std::size_t, std::size_t>;
// Acyclic chain 0 → 1 → 2.
static_assert(csc::pack::singleton_init_acyclic<3>(std::array<Edge, 2>{{{0, 1}, {1, 2}}}));
// Cyclic 0 → 1 → 2 → 0.
static_assert(!csc::pack::singleton_init_acyclic<3>(std::array<Edge, 3>{{{0, 1}, {1, 2}, {2, 0}}}));
static_assert(csc::pack::singleton_init_has_cycle<3>(std::array<Edge, 3>{{{0, 1}, {1, 2}, {2, 0}}}));
// Self-loop 0 → 0.
static_assert(!csc::pack::singleton_init_acyclic<1>(std::array<Edge, 1>{{{0, 0}}}));
// Empty graph — vacuously acyclic.
static_assert(csc::pack::singleton_init_acyclic<3>(std::array<Edge, 0>{}));
// Diamond 0→1, 0→2, 1→3, 2→3 — acyclic (no back edge).
static_assert(csc::pack::singleton_init_acyclic<4>(std::array<Edge, 4>{{{0, 1}, {0, 2}, {1, 3}, {2, 3}}}));

// Exactly eight combinations are rejected. The passing cases below are what
// catches a rule drawn too wide.

// No abort marker, and a tier below the longjmp floor, so the longjmp rule
// cannot fire whatever the usage axis says. No fork worker either, so the
// throw rule cannot fire.
using NeutralCfThrow = csfn::Fn<CfThrow>;
static_assert(csfn::ValidComposition<NeutralCfThrow>);
static_assert(csc::first_failure_v<NeutralCfThrow> == csc::RuleCode::None);

using NeutralCfAbort = csfn::Fn<CfAbort>;
static_assert(csfn::ValidComposition<NeutralCfAbort>);

// The stdio tier alone is not enough. Without the hot-path marker the rule
// has nothing to collide with.
using NeutralStdioBuf = csfn::Fn<StdioBuf>;
static_assert(csfn::ValidComposition<NeutralStdioBuf>);
static_assert(csc::first_failure_v<NeutralStdioBuf> == csc::RuleCode::None);

// The bottom stdio tier sits below the floor, so it stays admissible even
// where the hot-path marker is present.
using NeutralStdioNone = csfn::Fn<StdioNone>;
static_assert(csfn::ValidComposition<NeutralStdioNone>);

// A carrier with no hazard wrapper and no marker is outside every one of
// these rules.
static_assert(csfn::ValidComposition<DefaultFn>);
static_assert(csc::first_failure_v<DefaultFn> == csc::RuleCode::None);

// A carrier that genuinely trips one of these rules cannot be named here at
// all. Instantiating it fires the composition assertion inside Fn itself,
// before first_failure_v could be queried. Each rejection is therefore
// witnessed by its own compile-failure fixture, one per rule.

}  // namespace

// Everything above is compile-time. main() exists because the target is
// linked and run as an executable.
int main() { return 0; }
