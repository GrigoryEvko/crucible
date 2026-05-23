// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-257 mint_vendor_intrinsic fixture 1/2 — empty intrinsic id.
//
// CtxFitsVendorIntrinsicMint<Ctx, Id> requires `rationale_nonempty_v<Id>`
// — the intrinsic mnemonic identifies WHICH vendor intrinsic is pinned
// (wgmma / v_mfma / ...); an empty id carries no identity.
//
// Mismatch class: intrinsic-id engagement floor.  Distinct from
// neg_fixy_v_257_vendor_intrinsic_non_ctx.cpp (ExecCtx gate).
//
// Expected diagnostic: "constraints not satisfied" / "no matching
// function" / "CtxFitsVendorIntrinsicMint" / "rationale_nonempty".

#include <crucible/fixy/Hw.h>
#include <crucible/effects/ExecCtx.h>

int main() {
    ::crucible::effects::TestRunnerCtx ctx{};
    namespace hw = ::crucible::fixy::hw;
    // Should FAIL: empty intrinsic id fails rationale_nonempty_v.
    [[maybe_unused]] auto g =
        hw::mint_vendor_intrinsic<"", hw::VendorBackend::NV>(ctx);
    return 0;
}
