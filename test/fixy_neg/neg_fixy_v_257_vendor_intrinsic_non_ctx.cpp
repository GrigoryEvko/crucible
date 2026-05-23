// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-257 mint_vendor_intrinsic fixture 2/2 — non-ExecCtx rejected.
//
// The trailing parameter is constrained `effects::IsExecCtx Ctx`.
// A bare `char` is not an ExecCtx, so no overload matches even though
// the intrinsic id and vendor backend are well-formed.
//
// Mismatch class: ExecCtx gate.  Distinct from
// neg_fixy_v_257_vendor_intrinsic_empty_id.cpp (intrinsic-id floor).
//
// Expected diagnostic: "constraints not satisfied" / "no matching
// function" / "IsExecCtx".

#include <crucible/fixy/Hw.h>

int main() {
    char not_a_ctx = 'x';
    namespace hw = ::crucible::fixy::hw;
    // Should FAIL: char does not satisfy effects::IsExecCtx.
    [[maybe_unused]] auto g =
        hw::mint_vendor_intrinsic<"vfmadd231ps", hw::VendorBackend::NV>(not_a_ctx);
    return 0;
}
