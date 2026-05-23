// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-191 mint_affinity, mismatch class #2 of 2:
// NOT-PINNED POSTURE.
//
// mint_affinity produces a pin PROOF; minting one with PinningPosture::
// NotPinned is contradictory (a "proof that the thread is not pinned").
// CtxFitsAffinityMint requires Posture != NotPinned, so it is rejected.
//
// Distinct from neg_sched_affinity_non_exec_ctx.cpp (a ctx constraint);
// here the failure is the posture constraint.
//
// Expected diagnostic: constraints not satisfied / CtxFitsAffinityMint /
// NotPinned / no matching function / mint_affinity.

#include <crucible/fixy/Sched.h>
#include <crucible/effects/ExecCtx.h>

int main() {
    ::crucible::effects::BgDrainCtx bg{};

    // Should FAIL: a NotPinned posture is not a pin proof.
    auto pin = ::crucible::fixy::sched::mint_affinity<
        ::crucible::algebra::lattices::AffinityMask::single(0),
        ::crucible::safety::PinningPosture::NotPinned>(bg);
    return pin.has_value() ? 0 : 1;
}
