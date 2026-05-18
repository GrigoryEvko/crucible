// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A3-020 audit fixture (1/2): pins the diagnostic-cascade
// elimination promise of the WellFormedExecCtxAxes hoist.
//
// BEFORE the hoist, `ExecCtx<int>` fired SEVEN class-body
// static_assert lines in sequence (Cap not a cap type, Numa not a
// numa policy, Alloc not an alloc class, Heat not a heat tier, Resid
// not a residency tier, Row not an effect row, Workload not a
// workload hint) — burying the FIRST violation (Cap = int) under 6
// downstream cascade lines.  The reader had to scroll past 7
// diagnostics to find the actual typo.
//
// AFTER the hoist, the SINGLE constraint
//     WellFormedExecCtxAxes<int, Any, Unbound, Cold, DRAM, Row<>, Unspecified>
// fails substitution at the class-template head BEFORE the body is
// instantiated.  Concept conjunction short-circuits: IsCapType<int>
// evaluates to false and the later atoms (including the unsafe
// `cap_permitted_row_t<int>` access) are NOT substituted.  The
// diagnostic is one line: "constraints not satisfied for class
// template ExecCtx ... required by the constraints of class template
// ExecCtx".
//
// The fixture's failure witnesses the refactor: a typo'd ExecCtx
// instantiation MUST be rejected, and the rejection MUST surface the
// single WellFormedExecCtxAxes failure rather than a body-cascade.
//
// Expected diagnostic: "constraints not satisfied" /
// "WellFormedExecCtxAxes" / "IsCapType" / "fixy-A3-020"

#include <crucible/effects/ExecCtx.h>

namespace eff = ::crucible::effects;

// fixy-A3-020: the canonical typo — `int` at the Cap position.  The
// requires-clause `WellFormedExecCtxAxes<int, ...>` decomposes via
// concept conjunction and fails at the FIRST atom `IsCapType<int>`;
// the later `Subrow<Row<>, cap_permitted_row_t<int>>` atom is NOT
// substituted, so the missing `cap_permitted_row<int>::type`
// specialization never fires a secondary diagnostic.
using BadCtx = eff::ExecCtx<int>;

// Force instantiation so substitution failure becomes a hard error.
// Without this line the requires-clause failure is deferred to the
// first concrete use; the fixture pins the eager substitution.
[[maybe_unused]] static constexpr BadCtx witness{};

int main() { return 0; }
