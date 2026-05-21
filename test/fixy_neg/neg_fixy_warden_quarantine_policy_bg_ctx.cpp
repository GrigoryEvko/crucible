// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-120b negative fixture #7 (HS14 ≥2 floor, mint #4 of 4):
// `mint_quarantine_policy<Ctx, MaxCogs, MaxEvents>(ctx, config)`
// Init-row gate routed through the `fixy::warden::` re-export
// (Warden.h:156).
//
// `mint_quarantine_policy` is the only warden mint with non-type
// template parameters; the §XXI form pins (Ctx, MaxCogs, MaxEvents)
// explicitly so the using-decl-preserved gate is unambiguously
// witnessed at a concrete instantiation.  CtxFitsQuarantineMint
// rejects BgDrainCtx because fleet-shape decisions belong to Init.
// (BgDrainCtx IS accepted by the companion CtxFitsQuarantineRecord
// concept for record-only callers — that surface is not the mint;
// see fixy::warden::CtxFitsQuarantineRecord re-export.)
//
// Expected diagnostic: "no matching function" / "constraints not
// satisfied" / "CtxFitsQuarantineMint" / "Init".

#include <crucible/fixy/Warden.h>
#include <crucible/effects/ExecCtx.h>

int main() {
    auto policy = crucible::fixy::warden::mint_quarantine_policy<
        crucible::effects::BgDrainCtx, 2>(crucible::effects::BgDrainCtx{});
    (void)policy;
    return 0;
}
