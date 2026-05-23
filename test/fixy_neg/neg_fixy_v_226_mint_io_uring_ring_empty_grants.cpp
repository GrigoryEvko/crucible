// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-226 fixture #1 — empty Grants pack rejected at mint_io_uring_ring.
//
// `CtxFitsIoUringMint<Ctx, Grants...>` requires the Grants pack to
// engage BOTH (a) at least one `engine<E>` (which must additionally
// be `engine::IoUring`) and (b) at least one `sq_entries<N>` (which
// must additionally be power-of-2 and ≤ 32768).  An empty pack fails
// `has_engine_grant_v<>` AND `has_sq_entries_grant_v<>`; either alone
// is fatal to the requires-clause.
//
// Mismatch class: missing engine + missing sq_entries engagement.
// Mirrors FIXY-V-225 fixture #1 (empty Grants on mint_mmap).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "no matching function" / "CtxFitsIoUringMint" /
//   "constraints not satisfied" / "has_engine_grant" /
//   "has_sq_entries_grant".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Wrap.h>

int main() {
    namespace fwio = ::crucible::fixy::wrap::io;

    ::crucible::effects::TestRunnerCtx ctx{};

    // Should FAIL: empty Grants pack fails has_engine_grant_v +
    // has_sq_entries_grant_v predicates in CtxFitsIoUringMint.
    [[maybe_unused]] auto r = fwio::mint_io_uring_ring<>(ctx);
    return 0;
}
