// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-120b negative fixture #4 (HS14 ≥2 floor, mint #2 of 4):
// `mint_deadline_watchdog` IsExecCtx-half failure routed through the
// `fixy::warden::` re-export (Warden.h:123).
//
// Sibling of fixture #3 (BgDrainCtx exercises the Init-row half);
// THIS fixture passes a raw struct that fails `IsExecCtx<Ctx>` — the
// template-parameter constraint introducer on the function template
// itself.  Two distinct mismatch classes isolate the two conjuncts
// of `CtxFitsDeadlineWatchdogMint`.
//
// Expected diagnostic: "no matching function" / "constraints not
// satisfied" / "IsExecCtx" / "NotAnExecCtx" / "row_type".

#include <crucible/fixy/Warden.h>

namespace test_fixy_warden_deadline_watchdog_not_exec_ctx {

struct NotAnExecCtx {};

}  // namespace test_fixy_warden_deadline_watchdog_not_exec_ctx

int main() {
    crucible::fixy::warden::Policy p{};
    auto watchdog = crucible::fixy::warden::mint_deadline_watchdog(
        test_fixy_warden_deadline_watchdog_not_exec_ctx::NotAnExecCtx{},
        /*senses=*/nullptr, p);
    (void)watchdog;
    return 0;
}
