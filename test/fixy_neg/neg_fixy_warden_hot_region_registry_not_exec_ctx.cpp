// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-120b negative fixture #6 (HS14 ≥2 floor, mint #3 of 4):
// `mint_hot_region_registry_handle` IsExecCtx-half failure routed
// through the `fixy::warden::` re-export (Warden.h:136).
//
// Sibling of fixture #5 (BgDrainCtx — Init-row half).  THIS fixture
// passes a raw struct that fails `IsExecCtx<Ctx>` — the function
// template's parameter-introducer concept.  Two mismatch classes
// isolate the two conjuncts of `CtxFitsHotRegionRegistryMint`.
//
// Expected diagnostic: "no matching function" / "constraints not
// satisfied" / "IsExecCtx" / "NotAnExecCtx" / "row_type".

#include <crucible/fixy/Warden.h>

namespace test_fixy_warden_hot_region_registry_not_exec_ctx {

struct NotAnExecCtx {};

}  // namespace test_fixy_warden_hot_region_registry_not_exec_ctx

int main() {
    auto handle = crucible::fixy::warden::mint_hot_region_registry_handle(
        test_fixy_warden_hot_region_registry_not_exec_ctx::NotAnExecCtx{});
    (void)handle;
    return 0;
}
