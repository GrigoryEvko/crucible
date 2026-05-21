// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-120b negative fixture #5 (HS14 ≥2 floor, mint #3 of 4):
// `mint_hot_region_registry_handle(ctx)` Init-row gate routed through
// the `fixy::warden::` re-export (Warden.h:136).
//
// Registering new hot regions is an Init-row act (the registry table
// backs Hardening::apply()'s mlock2 / MADV_HUGEPAGE walk).  Bg drain
// contexts must not register / unregister regions — that would race
// against an in-flight apply() and is also a structural confusion of
// the cold init-time setup with steady-state drain work.
// BgDrainCtx::row = Row<Bg, Alloc> — Init capability absent →
// CtxFitsHotRegionRegistryMint fails its second conjunct.
//
// Expected diagnostic: "no matching function" / "constraints not
// satisfied" / "CtxFitsHotRegionRegistryMint" / "Init".

#include <crucible/fixy/Warden.h>
#include <crucible/effects/ExecCtx.h>

int main() {
    auto handle = crucible::fixy::warden::mint_hot_region_registry_handle(
        crucible::effects::BgDrainCtx{});
    (void)handle;
    return 0;
}
