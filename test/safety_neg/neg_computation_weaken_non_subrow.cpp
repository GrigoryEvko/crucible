// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling Computation::weaken<R2>() with a target row R2
// that is NOT a superset of the current row R.  The METX-1 (#473)
// `weaken<R2>` method carries `requires Subrow<R, R2>` — the
// substitution principle for capability propagation flows from
// narrower-effect to wider-effect, NEVER the reverse.
//
// Concrete violation: starting at Row<{Bg, IO}> (a function that may
// perform background work AND I/O), trying to weaken to Row<{Bg}>
// (claiming "I might do Bg but never IO").  This DROPS the I/O
// capability obligation — a downstream caller relying on the
// declared row would now see a function that can secretly perform
// I/O it never claimed.  Subrow<{Bg, IO}, {Bg}> is FALSE because
// {Bg, IO} ⊄ {Bg} — the constraint correctly rejects.
//
// This is the single most important METX-1 contract: capability
// REMOVAL must be impossible at the type level, because a runtime
// bypass would invalidate the foreground-vs-background hot-path
// discipline.  The diagnostic mentions "weaken" and "Subrow" so
// the harness pattern-matches reliably.
//
// Companion to neg_computation_extract_on_nonempty_row.cpp (which
// exercises the empty-row-required side).
//
// Task #146 (A8-P2 Neg-compile coverage); see
// include/crucible/effects/Computation.h METX-1 #473 weaken<R2>.

#include <crucible/effects/Computation.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/Capabilities.h>

using namespace crucible::effects;

int main() {
    // Construct a Computation at row {Bg, IO}.
    auto wide = Computation<Row<Effect::Bg, Effect::IO>, int>{};

    // Should FAIL: weaken<{Bg}>() — Subrow<{Bg, IO}, {Bg}> is false
    // because {Bg, IO} is NOT a subset of {Bg}.  Capability removal
    // is forbidden by the substitution principle.
    auto narrowed = wide.template weaken<Row<Effect::Bg>>();
    (void)narrowed;
    return 0;
}
