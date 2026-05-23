// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-197 HS14 floor #2 of 2 for fixy::sched::mint_priority:
// the `Nice in [-20, 19]` half of `CtxFitsPriorityMint` fails when
// the caller spells a value outside the kernel-accepted PRIO_PROCESS
// nice range.  Linux setpriority(2) silently clamps out-of-range
// values, so the type-system gate is the ONLY place an off-by-one
// gets caught.  This fixture pins Nice = -21 (one below the floor).
//
// Distinct from neg_fixy_sched_mint_priority_not_exec_ctx.cpp (the
// IsExecCtx half of the same concept).  Both fixtures are required
// to demonstrate that the §XXI requires-clause fires on EACH of the
// two CtxFitsPriorityMint conjuncts independently (HS14).
//
// Expected diagnostic: "constraints not satisfied" /
// "CtxFitsPriorityMint" / "Nice >= -20" / "mint_priority" /
// "-21".

#include <crucible/fixy/Sched.h>

int main() {
    // Should FAIL: -21 < -20; CtxFitsPriorityMint's
    // `(Nice >= -20 && Nice <= 19)` conjunct rejects.
    auto p = ::crucible::fixy::sched::mint_priority<-21>(
        ::crucible::effects::ColdInitCtx{});
    (void)p;
    return 0;
}
