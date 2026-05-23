// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-191 mint_scheduler_policy, mismatch class #1 of 2:
// NON-EXEC-CONTEXT FIRST ARGUMENT.
//
// mint_scheduler_policy is a §XXI ctx-bound mint; a bare `int` is not an
// effects::IsExecCtx and is rejected by the constrained template parameter.
//
// Distinct from neg_sched_policy_deadline_budget.cpp (a CBS budget
// violation); here the failure is the ctx-type constraint.
//
// Expected diagnostic: constraints not satisfied / IsExecCtx /
// no matching function / mint_scheduler_policy.

#include <crucible/fixy/Sched.h>

int main() {
    // Should FAIL: 7 (int) is not an effects::IsExecCtx.
    auto policy = ::crucible::fixy::sched::mint_scheduler_policy<
        ::crucible::fixy::sched::SchedulerPolicy_v::Other>(7);
    return policy.has_value() ? 0 : 1;
}
