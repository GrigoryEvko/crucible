// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-191 mint_affinity, mismatch class #1 of 2:
// NON-EXEC-CONTEXT FIRST ARGUMENT.
//
// Every §XXI ctx-bound mint takes an effects::IsExecCtx first.  A bare
// `int` is not an ExecCtx, so the mint's constrained template parameter
// rejects it.
//
// Distinct from neg_sched_affinity_not_pinned.cpp (a posture constraint);
// here the failure is the ctx-type constraint.
//
// Expected diagnostic: constraints not satisfied / IsExecCtx /
// no matching function / mint_affinity.

#include <crucible/fixy/Sched.h>

int main() {
    // Should FAIL: 42 (int) is not an effects::IsExecCtx.
    auto pin = ::crucible::fixy::sched::mint_affinity<
        ::crucible::algebra::lattices::AffinityMask::single(0)>(42);
    return pin.has_value() ? 0 : 1;
}
