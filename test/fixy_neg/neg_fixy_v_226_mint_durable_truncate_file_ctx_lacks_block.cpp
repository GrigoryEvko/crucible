// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-FOUND-091 fixture #2/4 — ColdInitCtx rejected at
// mint_durable_truncate_file (no Block cap).
//
// Filesystem syscalls (open, fsync, linkat) cross the kernel boundary
// (IO) and can park the caller until disk responds (Block).
// `mint_durable_truncate_file` delegates to `mint_file<...>` which
// requires `CtxAdmitsIoBlock<Ctx>` — both Effect::IO AND Effect::Block
// must be admitted by the caller's ExecCtx row.
//
// `ColdInitCtx` is defined with `Row<Init, Alloc, IO>` — admits IO but
// NOT Block.  A process-init mint that only declares IO cannot durably
// write through fsync (Fsync MUST be allowed to park; durable<Fsync>'s
// whole point is to block until on-disk).
//
// Mismatch class: ctx-row engagement gap (lacks Effect::Block) at the
// underlying mint_file's CtxAdmitsIoBlock fold.  Distinct from #1
// (path source tag) — this fires on the ctx row.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "constraints not satisfied" / "no matching function" /
//   "CtxFitsFileMint" / "CtxAdmitsIoBlock" / "row_contains" / "Block".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Source.h>           // fixy::tags::source::*
#include <crucible/fixy/Wrap.h>

int main() {
    namespace fwfs = ::crucible::fixy::wrap::fs;

    // ColdInitCtx — Row<Init, Alloc, IO> — admits IO but NOT Block.
    ::crucible::effects::ColdInitCtx ctx{};

    fwfs::Path<::crucible::fixy::tags::source::Sanitized> path{
        "/tmp/crucible_neg_v226_durable_truncate_no_block"};

    // Should FAIL: mint_durable_truncate_file delegates to mint_file,
    // whose CtxFitsFileMint folds in CtxAdmitsIoBlock<Ctx>.  ColdInitCtx's
    // row lacks Effect::Block; the requires-clause refuses instantiation.
    [[maybe_unused]] auto r =
        fwfs::mint_durable_truncate_file(ctx, std::move(path));
    return 0;
}
