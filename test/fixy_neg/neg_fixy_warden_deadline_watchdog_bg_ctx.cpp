// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-120b negative fixture #3 (HS14 ≥2 floor, mint #2 of 4):
// `mint_deadline_watchdog(ctx, senses, policy)` Init-row gate routed
// through the `fixy::warden::` re-export (Warden.h:123).
//
// Standing up a fresh watchdog requires reading Senses + Policy +
// steady_clock to baseline the rolling window — an Init-row act.  The
// Bg drain thread that polls observe() each tick reads from an
// EXISTING watchdog minted at Init time; it does not mint its own.
// BgDrainCtx::row = Row<Bg, Alloc> — IsExecCtx admits but Init is
// absent → CtxFitsDeadlineWatchdogMint fails its second conjunct.
//
// Expected diagnostic: "no matching function" / "constraints not
// satisfied" / "CtxFitsDeadlineWatchdogMint" / "Init".

#include <crucible/fixy/Warden.h>
#include <crucible/effects/ExecCtx.h>

int main() {
    crucible::fixy::warden::Policy p{};
    p.deadline_miss_budget = 100;
    p.watchdog_window_sec = 1;
    auto watchdog = crucible::fixy::warden::mint_deadline_watchdog(
        crucible::effects::BgDrainCtx{}, /*senses=*/nullptr, p);
    (void)watchdog;
    return 0;
}
