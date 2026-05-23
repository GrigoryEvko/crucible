// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-190 mint_bounded_sleep, mismatch class #1 of 2:
// CONTEXT WITHOUT THE Block CAPABILITY.
//
// Sleeping is a Block-effect operation; mint_bounded_sleep admits only a
// ctx whose row engages Effect::Block (CtxCanMint<Ctx, Block>).  A
// ColdInitCtx (Init row = {Init, Alloc, IO}, no Block) must be rejected —
// you do not sleep during init or on a non-blocking path.
//
// Distinct from neg_time_bounded_sleep_zero.cpp (a zero cap); here the
// failure is the ctx Block-capability gate.
//
// Expected diagnostic: constraints not satisfied / CtxCanMint / Block /
// no matching function / mint_bounded_sleep.

#include <crucible/fixy/Time.h>
#include <crucible/effects/ExecCtx.h>

int main() {
    ::crucible::effects::ColdInitCtx init{};  // Init row carries no Block

    // Should FAIL: Init context cannot mint a Block-effect sleeper.
    auto sleeper = ::crucible::fixy::time::mint_bounded_sleep<1'000>(init);
    sleeper.sleep_for(0);
    return 0;
}
