// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-226 fixture #5 — ColdInitCtx rejected at mint_io_uring_ring.
//
// `CtxAdmitsIoBlock<Ctx>` requires the caller's ExecCtx row to admit
// BOTH `Effect::IO` AND `Effect::Block`.  io_uring_setup itself never
// parks the caller, BUT the io_uring submission queue + completion
// queue use `IORING_SETUP_SQPOLL` / blocking enter syscalls that DO
// park; minting the ring without Effect::Block would license future
// `io_uring_enter()` callers to park from a context that declared it
// would never block (init phase).
//
// `ColdInitCtx` is `Row<Init, Alloc, IO>` — admits IO but NOT Block.
// `CtxFitsIoUringMint<Ctx, ...>` folds in `CtxAdmitsIoBlock<Ctx>` and
// rejects the mint at construction.
//
// Mismatch class: ctx-row engagement gap (lacks Effect::Block).
// Mirrors FIXY-V-224 fixture #6 / FIXY-V-225 fixture #6 (same kind
// of ctx-row gap on a different mint family).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "constraints not satisfied" / "no matching function" /
//   "CtxFitsIoUringMint" / "CtxAdmitsIoBlock" / "row_contains" / "Block".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Wrap.h>

int main() {
    namespace fwio  = ::crucible::fixy::wrap::io;
    namespace engine = fwio::engine;
    namespace grant  = fwio::grant;

    // ColdInitCtx — Row<Init, Alloc, IO> — admits IO but NOT Block.
    ::crucible::effects::ColdInitCtx ctx{};

    // Should FAIL: mint_io_uring_ring's CtxFitsIoUringMint folds in
    // CtxAdmitsIoBlock<Ctx>; ColdInitCtx's row lacks Effect::Block.
    [[maybe_unused]] auto r = fwio::mint_io_uring_ring<
        grant::engine<engine::IoUring>,
        grant::sq_entries<128>
    >(ctx);
    return 0;
}
