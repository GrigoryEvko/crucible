// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-224 fixture #6 — ColdInitCtx rejected at mint_file (no Block).
//
// `CtxAdmitsIoBlock<Ctx>` requires the caller's ExecCtx row to admit
// BOTH `Effect::IO` AND `Effect::Block`.  Filesystem syscalls do
// both — they cross the kernel boundary (IO) and can park the caller
// until disk responds (Block).
//
// `ColdInitCtx` is defined with `Row<Effect::Init, Effect::Alloc,
// Effect::IO>` — admits IO but NOT Block.  A process-init mint that
// only declares IO cannot durably write through fsync because Block
// would lift the "init phase MUST NOT park on slow disk" discipline.
// The mint_file refusal points to the Block gap.
//
// Mismatch class: ctx-row engagement gap (lacks Effect::Block).
// Distinct from fixtures #1-#5 (path tags, grants, sync_op) — this
// fires on the ctx's row at the OUTER concept layer.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "constraints not satisfied" / "no matching function" /
//   "CtxFitsFileMint" / "CtxAdmitsIoBlock" / "row_contains" / "Block".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Source.h>           // fixy::tags::source::*
#include <crucible/fixy/Wrap.h>

int main() {
    namespace fwfs = ::crucible::fixy::wrap::fs;
    namespace om   = fwfs::open_mode;

    // ColdInitCtx — Row<Init, Alloc, IO> — admits IO but NOT Block.
    ::crucible::effects::ColdInitCtx ctx{};

    fwfs::Path<::crucible::fixy::tags::source::Sanitized> path{
        "/tmp/crucible_neg_v224_ctx_no_block"};

    // Should FAIL: mint_file's CtxFitsFileMint folds in
    // CtxAdmitsIoBlock<Ctx>; ColdInitCtx's row lacks Effect::Block,
    // so the requires-clause refuses the instantiation.
    [[maybe_unused]] auto r = fwfs::mint_file<
        fwfs::grant::mode<om::ReadOnly>
    >(ctx, std::move(path));
    return 0;
}
