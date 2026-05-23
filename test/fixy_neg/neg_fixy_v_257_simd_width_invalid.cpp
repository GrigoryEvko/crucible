// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-257 mint_simd_width fixture 1/2 — invalid width rejected.
//
// CtxFitsSimdWidthMint<Ctx, WidthBits> requires
// `valid_simd_width_v<WidthBits>` — WidthBits MUST be one of the
// recognized register-width classes {0, 128, 256, 512}.  A bogus width
// (100) is not a SIMD register width on any supported ISA.
//
// Mismatch class: width-validity gate.  Distinct from
// neg_fixy_v_257_simd_width_non_ctx.cpp, which fires on the ExecCtx gate.
//
// Expected diagnostic: "constraints not satisfied" / "no matching
// function" / "CtxFitsSimdWidthMint" / "valid_simd_width".

#include <crucible/fixy/Hw.h>
#include <crucible/effects/ExecCtx.h>

int main() {
    ::crucible::effects::TestRunnerCtx ctx{};
    // Should FAIL: 100 is not in {0, 128, 256, 512}.
    [[maybe_unused]] auto g = ::crucible::fixy::hw::mint_simd_width<100>(ctx);
    return 0;
}
