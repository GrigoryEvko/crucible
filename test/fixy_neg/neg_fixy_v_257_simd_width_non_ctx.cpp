// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-257 mint_simd_width fixture 2/2 — non-ExecCtx rejected.
//
// The trailing parameter is constrained `effects::IsExecCtx Ctx`.
// A bare `double` is not an ExecCtx, so no overload matches even though
// the width (256) is valid.
//
// Mismatch class: ExecCtx gate.  Distinct from
// neg_fixy_v_257_simd_width_invalid.cpp, which fires on the width value.
//
// Expected diagnostic: "constraints not satisfied" / "no matching
// function" / "IsExecCtx".

#include <crucible/fixy/Hw.h>

int main() {
    double not_a_ctx = 0.0;
    // Should FAIL: double does not satisfy effects::IsExecCtx.
    [[maybe_unused]] auto g = ::crucible::fixy::hw::mint_simd_width<256>(not_a_ctx);
    return 0;
}
