// FIXY-V-243 sentinel TU: CollisionCatalog hazard-axis cross-axis rules.
//
// V-243 ships 8 NEW collision-catalog entries (C001/D001/D002/G001/L006/
// P003/S001/S004) atop the V-242 hazard-axis Graded carriers
// (ControlFlowPinned / StdioPinned).  Each rule gates a cross-axis
// composition where a hazard declaration is inconsistent with another Fn
// axis.  Four read a SHIPPED V-242 wrapper tier off F::type_t (C001 /
// L006 / P003 / S001 — triggerable today), four read an opt-in marker
// trait the V-244/245/246 grant headers will specialize (D001 / D002 /
// G001 / S004 — default-SAFE until a grant opts in).
//
// Sentinel witnesses:
//   (a) catalog cardinality floor (>= 36) + 8 rule_bijection cells.
//   (b) CollisionDiagnosticByRule<F, X>::rule_code() string identity.
//   (c) control_flow_tier_of / stdio_tier_of detectors + cf_at_or_above_v
//       / stdio_at_or_above_v ceiling predicates — positive, negative,
//       CV-piercing, non-wrapper-falls-back cells.
//   (d) pack::singleton_init_acyclic over acyclic / cyclic / self-loop /
//       empty edge lists (S004's reusable detector, exercised here so a
//       refactor that breaks Kahn's algorithm reds without waiting for V-248).
//   (e) POSITIVE compositions PASS — a hazard wrapper WITHOUT the marker,
//       or a below-floor tier, trips no rule.
//   (f) NEGATIVE compositions are covered by the 8 HS14 neg-compile
//       fixtures (one per rule); see the closing note.

#include <crucible/safety/ControlFlow.h>
#include <crucible/safety/Fn.h>             // pulls the CollisionCatalog body
#include <crucible/safety/Stdio.h>

#include <array>
#include <string_view>
#include <type_traits>
#include <utility>

namespace cs   = ::crucible::safety;
namespace csfn = ::crucible::safety::fn;
namespace csc  = ::crucible::safety::fn::collision;
using CF  = ::crucible::algebra::lattices::ControlFlow;
using SIO = ::crucible::algebra::lattices::Stdio;

namespace {

// ── (a) Catalog cardinality + bijection witnesses ──────────────────
static_assert(csc::catalog_size >= 36,
              "FIXY-V-243 floor: catalog must include C001..S004 (8 rules)");
static_assert(std::tuple_size_v<csc::Catalog> >= 36);
static_assert(csc::rule_bijection_v<csc::RuleCode::C001>);
static_assert(csc::rule_bijection_v<csc::RuleCode::D001>);
static_assert(csc::rule_bijection_v<csc::RuleCode::D002>);
static_assert(csc::rule_bijection_v<csc::RuleCode::G001>);
static_assert(csc::rule_bijection_v<csc::RuleCode::L006>);
static_assert(csc::rule_bijection_v<csc::RuleCode::P003>);
static_assert(csc::rule_bijection_v<csc::RuleCode::S001>);
static_assert(csc::rule_bijection_v<csc::RuleCode::S004>);

// ── (b) Diagnostic-string identity ─────────────────────────────────
using DefaultFn = csfn::Fn<int>;
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::C001>::rule_code()
              == std::string_view{"C001"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::D001>::rule_code()
              == std::string_view{"D001"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::D002>::rule_code()
              == std::string_view{"D002"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::G001>::rule_code()
              == std::string_view{"G001"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::L006>::rule_code()
              == std::string_view{"L006"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::P003>::rule_code()
              == std::string_view{"P003"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::S001>::rule_code()
              == std::string_view{"S001"});
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::S004>::rule_code()
              == std::string_view{"S004"});

// ── (c) ControlFlow / Stdio tier detectors ─────────────────────────
using CfPure       = cs::ControlFlowPinned<CF::Pure, int>;
using CfAbort      = cs::ControlFlowPinned<CF::AbortOnly, int>;
using CfThrow      = cs::ControlFlowPinned<CF::ThrowOnly, int>;
using CfLongjmp    = cs::ControlFlowPinned<CF::MayLongjmp, int>;
using CfSignal     = cs::ControlFlowPinned<CF::MaySignal, int>;
using StdioNone    = cs::StdioPinned<SIO::NoStdio, int>;
using StdioBuf     = cs::StdioPinned<SIO::BufferedWrite, int>;
using StdioRead    = cs::StdioPinned<SIO::InteractiveRead, int>;

// has_cf / has_stdio discrimination.
static_assert(csc::control_flow_tier_of<CfAbort>::has_cf);
static_assert(csc::control_flow_tier_of<CfAbort>::value == CF::AbortOnly);
static_assert(!csc::control_flow_tier_of<int>::has_cf);
static_assert(!csc::control_flow_tier_of<StdioBuf>::has_cf);  // wrong wrapper family
static_assert(csc::stdio_tier_of<StdioBuf>::has_stdio);
static_assert(csc::stdio_tier_of<StdioBuf>::value == SIO::BufferedWrite);
static_assert(!csc::stdio_tier_of<int>::has_stdio);
static_assert(!csc::stdio_tier_of<CfAbort>::has_stdio);

// CV / reference piercing — a wrapped return of `CfAbort const&` is just
// as load-bearing as the bare wrapper.
static_assert(csc::control_flow_tier_of<CfAbort const&>::has_cf);
static_assert(csc::control_flow_tier_of<CfAbort&>::value == CF::AbortOnly);
static_assert(csc::stdio_tier_of<StdioBuf const>::has_stdio);

// cf_at_or_above_v ceiling predicate — AbortOnly floor.
static_assert(!csc::cf_at_or_above_v<CF::AbortOnly, CfPure>);    // Pure < AbortOnly
static_assert(csc::cf_at_or_above_v<CF::AbortOnly, CfAbort>);    // == floor
static_assert(csc::cf_at_or_above_v<CF::AbortOnly, CfThrow>);    // above
static_assert(csc::cf_at_or_above_v<CF::AbortOnly, CfSignal>);   // top
static_assert(!csc::cf_at_or_above_v<CF::AbortOnly, int>);       // no wrapper → false

// MayLongjmp floor (L006) — only MayLongjmp + MaySignal qualify.
static_assert(!csc::cf_at_or_above_v<CF::MayLongjmp, CfThrow>);
static_assert(csc::cf_at_or_above_v<CF::MayLongjmp, CfLongjmp>);
static_assert(csc::cf_at_or_above_v<CF::MayLongjmp, CfSignal>);

// ThrowOnly floor (P003).
static_assert(!csc::cf_at_or_above_v<CF::ThrowOnly, CfAbort>);
static_assert(csc::cf_at_or_above_v<CF::ThrowOnly, CfThrow>);

// stdio_at_or_above_v ceiling predicate — BufferedWrite floor (S001).
static_assert(!csc::stdio_at_or_above_v<SIO::BufferedWrite, StdioNone>);  // NoStdio < BufferedWrite
static_assert(csc::stdio_at_or_above_v<SIO::BufferedWrite, StdioBuf>);
static_assert(csc::stdio_at_or_above_v<SIO::BufferedWrite, StdioRead>);   // above
static_assert(!csc::stdio_at_or_above_v<SIO::BufferedWrite, int>);        // no wrapper → false

// ── (d) S004 init-cycle detector ───────────────────────────────────
using Edge = std::pair<std::size_t, std::size_t>;
// Acyclic chain 0 → 1 → 2.
static_assert(csc::pack::singleton_init_acyclic<3>(
    std::array<Edge, 2>{{{0, 1}, {1, 2}}}));
// Cyclic 0 → 1 → 2 → 0.
static_assert(!csc::pack::singleton_init_acyclic<3>(
    std::array<Edge, 3>{{{0, 1}, {1, 2}, {2, 0}}}));
static_assert(csc::pack::singleton_init_has_cycle<3>(
    std::array<Edge, 3>{{{0, 1}, {1, 2}, {2, 0}}}));
// Self-loop 0 → 0.
static_assert(!csc::pack::singleton_init_acyclic<1>(
    std::array<Edge, 1>{{{0, 0}}}));
// Empty graph — vacuously acyclic.
static_assert(csc::pack::singleton_init_acyclic<3>(std::array<Edge, 0>{}));
// Diamond 0→1, 0→2, 1→3, 2→3 — acyclic (no back edge).
static_assert(csc::pack::singleton_init_acyclic<4>(
    std::array<Edge, 4>{{{0, 1}, {0, 2}, {1, 3}, {2, 3}}}));

// ── (e) POSITIVE compositions — these MUST NOT trip any rule ───────
//
// V-243 REJECTS exactly 8 combinations; everything else continues to
// pass.  Witnessing these positives prevents a false positive sneaking
// through validate().

// (e1) A ControlFlowPinned carrier WITHOUT the marks_aborts marker and a
// below-MayLongjmp tier: no rule fires.  ThrowOnly < MayLongjmp so L006
// never fires regardless of Usage; no fork_worker so P003 never fires.
using NeutralCfThrow = csfn::Fn<CfThrow>;
static_assert(csfn::ValidComposition<NeutralCfThrow>);
static_assert(csc::first_failure_v<NeutralCfThrow> == csc::RuleCode::None);

using NeutralCfAbort = csfn::Fn<CfAbort>;
static_assert(csfn::ValidComposition<NeutralCfAbort>);

// (e2) A StdioPinned<BufferedWrite> carrier WITHOUT marks_hot_path: S001
// does not fire (the rule needs the hot-path marker).
using NeutralStdioBuf = csfn::Fn<StdioBuf>;
static_assert(csfn::ValidComposition<NeutralStdioBuf>);
static_assert(csc::first_failure_v<NeutralStdioBuf> == csc::RuleCode::None);

// (e3) A StdioPinned<NoStdio> carrier is safe even on a hot path (tier 0
// is below the BufferedWrite floor); the bare carrier passes here.
using NeutralStdioNone = csfn::Fn<StdioNone>;
static_assert(csfn::ValidComposition<NeutralStdioNone>);

// (e4) Plain int carrier — none of the hazard-axis rules touch a
// non-wrapper, non-marked Fn.
static_assert(csfn::ValidComposition<DefaultFn>);
static_assert(csc::first_failure_v<DefaultFn> == csc::RuleCode::None);

// ── (f) NEGATIVE compositions covered by HS14 neg-compile fixtures ─
//
// A sentinel TU cannot positively assert `first_failure_v<F> == X` when
// F is a Fn carrier that genuinely trips X: instantiating F runs Fn<>'s
// own `static_assert(ValidComposition<Fn>, ...)`, firing the validate()
// chain before first_failure_v can be queried.  HS14 covers this with 8
// neg-compile fixtures (one per rule, distinct mismatch classes):
//   neg_collision_C001_abort_no_witness.cpp   (marker × CF tier < AbortOnly)
//   neg_collision_D001_indirect_call_throws.cpp (marker)
//   neg_collision_D002_recursion_unbounded.cpp  (marker)
//   neg_collision_G001_thread_local_untagged.cpp (marker)
//   neg_collision_L006_linear_longjmp.cpp       (Linear × CF tier ≥ MayLongjmp)
//   neg_collision_P003_fork_body_throws.cpp     (fork-worker × CF tier ≥ ThrowOnly)
//   neg_collision_S001_hotpath_stdio.cpp        (hot-path × Stdio tier ≥ BufferedWrite)
//   neg_collision_S004_singleton_cycle.cpp      (marker)
// Each fixture's CMake stanza greps the per-rule diagnostic substring; a
// refactor that relaxes the rule turns the fixture RED→GREEN and ctest
// reports failure.

}  // namespace

int main() {
    // V-243 is pure compile-time discipline; the static_asserts above
    // exercise every load-bearing surface.  main() just satisfies the
    // executable-link requirement ctest expects from the test target.
    return 0;
}
