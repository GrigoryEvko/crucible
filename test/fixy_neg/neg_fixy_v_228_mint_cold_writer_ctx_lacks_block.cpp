// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-228 fixture #3 — ColdInitCtx rejected at mint_cold_writer.
//
// `CtxFitsColdWriterMint<Ctx, Extras...>` folds in
// `::crucible::fixy::fs::CtxAdmitsIoBlock<Ctx>` — the caller's row
// must admit BOTH Effect::IO AND Effect::Block.  The cold-tier writer
// opens with O_SYNC (every write syscall blocks on fsync to the
// underlying device); minting from a non-blocking ctx would license
// future O_SYNC writes to park indefinitely from a phase that
// declared it would never block.
//
// `ColdInitCtx` is `Row<Init, Alloc, IO>` — admits IO but NOT Block.
// `CtxAdmitsIoBlock<ColdInitCtx>` is false, so the §XXI mint's
// requires-clause refuses the instantiation.
//
// Mismatch class: ctx-row engagement gap (lacks Effect::Block).
// Distinct from fixture #4 (extras_engage_durable).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "no matching function" / "CtxFitsColdWriterMint" /
//   "CtxAdmitsIoBlock" / "constraints not satisfied" / "row_contains" /
//   "Block".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Source.h>
#include <crucible/fixy/Wrap.h>

int main() {
    namespace fwcd = ::crucible::fixy::wrap::cipher::durable;
    namespace fwfs = ::crucible::fixy::wrap::fs;

    // ColdInitCtx — Row<Init, Alloc, IO> — admits IO but NOT Block.
    ::crucible::effects::ColdInitCtx ctx{};

    fwfs::Path<::crucible::fixy::tags::source::Sanitized> path{
        "/tmp/crucible_neg_v228_cold_ctx_lacks_block"};

    // Should FAIL: mint_cold_writer's CtxFitsColdWriterMint folds in
    // CtxAdmitsIoBlock<Ctx>; ColdInitCtx's row lacks Effect::Block.
    [[maybe_unused]] auto r = fwcd::mint_cold_writer<>(
        ctx, std::move(path));
    return 0;
}
