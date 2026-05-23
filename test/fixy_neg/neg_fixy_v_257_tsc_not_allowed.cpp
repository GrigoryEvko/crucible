// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-257 mint_tsc_grant fixture 1/2 — Mode==NotAllowed rejected.
//
// CtxFitsTscMint<Ctx, Mode> requires `Mode != TscMode::NotAllowed`.
// NotAllowed is the strict default — there is no grant to mint for "no
// TSC read at all", so requesting one is a programmer error (the same
// trap sync<sync_op::None> closes in Fs.h).
//
// Mismatch class: strict-default posture (NotAllowed).  Distinct from
// neg_fixy_v_257_tsc_missing_proof.cpp, which fires on the absent
// CpuPinProof argument.
//
// Expected diagnostic: "constraints not satisfied" / "no matching
// function" / "CtxFitsTscMint" / "NotAllowed".

#include <crucible/fixy/Hw.h>
#include <crucible/effects/ExecCtx.h>

int main() {
    ::crucible::effects::TestRunnerCtx ctx{};
    namespace hw = ::crucible::fixy::hw;
    // Should FAIL: NotAllowed is the strict default; no grant to mint.
    [[maybe_unused]] auto g =
        hw::mint_tsc_grant<hw::TscMode::NotAllowed>(ctx, hw::CpuPinProof{});
    return 0;
}
