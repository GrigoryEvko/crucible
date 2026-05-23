// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-191 mint_scheduler_policy, mismatch class #2 of 2:
// SCHED_DEADLINE CBS BUDGET VIOLATION.
//
// A SCHED_DEADLINE class is admissible only when runtime < deadline <=
// period (the CBS inequality).  mint_scheduler_policy<Deadline, R, D, P>
// returns SchedClass<Deadline, int, R, D, P>, whose own static_assert
// fires on a violating budget — so a bad deadline is a compile error
// INSIDE the mint, with no separate concept.  Here runtime (100) exceeds
// deadline (50).
//
// Distinct from neg_sched_policy_non_exec_ctx.cpp (a ctx constraint); here
// the failure is the SchedClass CBS static_assert via the return type.
//
// Expected diagnostic: static assertion / CBS admission / RuntimeNs /
// DeadlineNs.

#include <crucible/fixy/Sched.h>
#include <crucible/effects/ExecCtx.h>

int main() {
    ::crucible::effects::BgDrainCtx bg{};

    // Should FAIL: runtime 100 >= deadline 50 violates CBS admission.
    auto policy = ::crucible::fixy::sched::mint_scheduler_policy<
        ::crucible::fixy::sched::SchedulerPolicy_v::Deadline,
        /*RuntimeNs=*/100, /*DeadlineNs=*/50, /*PeriodNs=*/200>(bg);
    return policy.has_value() ? 0 : 1;
}
