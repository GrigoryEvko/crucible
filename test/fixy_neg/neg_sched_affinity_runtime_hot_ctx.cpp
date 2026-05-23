// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-192 apply_affinity_to_cpu, mismatch class #1 of 2:
// HOT FG CONTEXT REJECTED.
//
// concurrent/Pipeline.h's pin_current_pipeline_thread_ routes a stage
// worker's affinity pin through ::crucible::fixy::sched::apply_affinity_to_cpu,
// whose CtxFitsRuntimeAffinity gate requires the ctx to OWN at least one of
// Effect::Bg or Effect::Init.  A HotFgCtx owns neither (it is the hot-path
// foreground context); presenting it at the pin would silently re-pin the
// foreground thread mid-flight — the Scenario-3 invariant.
//
// Distinct from neg_sched_affinity_runtime_non_exec_ctx.cpp (a ctx-type
// constraint); here the failure is the row-content constraint.
//
// Expected diagnostic: constraints not satisfied / CtxFitsRuntimeAffinity /
// no matching function / apply_affinity_to_cpu.

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Sched.h>

int main() {
    ::crucible::effects::HotFgCtx fg{};

    // Should FAIL: HotFgCtx owns neither Bg nor Init.
    auto r = ::crucible::fixy::sched::apply_affinity_to_cpu(fg, 0);
    return r.has_value() ? 0 : 1;
}
