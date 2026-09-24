// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-192 apply_affinity_to_cpu, mismatch class #2 of 2:
// NON-EXEC-CONTEXT FIRST ARGUMENT.
//
// apply_affinity_to_cpu takes an effects::IsExecCtx first; presenting a
// bare `int` (the cpu number bound by mistake to the wrong parameter) is
// rejected by the constrained template parameter.  This catches the call-
// site shape error of "I forgot to thread the ctx" — the gate sees no
// ExecCtx and rejects before any row-content check runs.
//
// Distinct from neg_sched_affinity_runtime_hot_ctx.cpp (a row-content
// constraint); here the failure is the ctx-type constraint.
//
// Expected diagnostic: constraints not satisfied / IsExecCtx /
// no matching function / apply_affinity_to_cpu.

#include <crucible/fixy/Sched.h>

int main() {
    // Should FAIL: 7 (int) is not an effects::IsExecCtx.
    auto r = ::crucible::fixy::sched::apply_affinity_to_cpu(7, 0);
    return r.has_value() ? 0 : 1;
}
