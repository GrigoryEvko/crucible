// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-191 mint_priority, mismatch class #2 of 2:
// NON-EXEC-CONTEXT FIRST ARGUMENT.
//
// mint_priority is a §XXI ctx-bound mint; a bare `int` is not an
// effects::IsExecCtx and is rejected by the constrained template parameter.
//
// Distinct from neg_sched_priority_out_of_range.cpp (a nice-range
// constraint); here the failure is the ctx-type constraint.
//
// Expected diagnostic: constraints not satisfied / IsExecCtx /
// no matching function / mint_priority.

#include <crucible/fixy/Sched.h>

int main() {
    // Should FAIL: 99 (int) is not an effects::IsExecCtx.
    auto prio = ::crucible::fixy::sched::mint_priority<5>(99);
    return prio.has_value() ? 0 : 1;
}
