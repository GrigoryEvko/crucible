// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-191 mint_priority, mismatch class #1 of 2:
// NICE VALUE OUT OF RANGE.
//
// The POSIX nice value is bounded to [-20, 19].  mint_priority requires
// Nice in that range (CtxFitsPriorityMint), so a Nice of 50 is rejected
// before any setpriority call.
//
// Distinct from neg_sched_priority_non_exec_ctx.cpp (a ctx constraint);
// here the failure is the nice-range constraint.
//
// Expected diagnostic: constraints not satisfied / CtxFitsPriorityMint /
// no matching function / mint_priority.

#include <crucible/fixy/Sched.h>
#include <crucible/effects/ExecCtx.h>

int main() {
    ::crucible::effects::BgDrainCtx bg{};

    // Should FAIL: 50 is outside the POSIX nice range [-20, 19].
    auto prio = ::crucible::fixy::sched::mint_priority<50>(bg);
    return prio.has_value() ? 0 : 1;
}
