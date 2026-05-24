// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-FOUND-091 fixture #4/4 — ColdInitCtx rejected at
// mint_durable_append_file (no Block cap).
//
// `mint_durable_append_file` delegates to `mint_file<...>` which folds
// `CtxAdmitsIoBlock<Ctx>` — Effect::IO AND Effect::Block both required.
// ColdInitCtx (Row<Init, Alloc, IO>) admits IO but NOT Block.  Even
// though append+O_DSYNC handles per-write durability without an
// explicit fsync, the underlying open(2) + write(2) syscalls still
// park-eligible (Block); the gate is unconditional.
//
// Mismatch class: ctx-row engagement gap (lacks Effect::Block).
// Companion to fixture #2 (truncate form).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "constraints not satisfied" / "no matching function" /
//   "CtxFitsFileMint" / "CtxAdmitsIoBlock" / "row_contains" / "Block".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Source.h>           // fixy::tags::source::*
#include <crucible/fixy/Wrap.h>

int main() {
    namespace fwfs = ::crucible::fixy::wrap::fs;

    ::crucible::effects::ColdInitCtx ctx{};

    fwfs::Path<::crucible::fixy::tags::source::Sanitized> path{
        "/tmp/crucible_neg_v226_durable_append_no_block"};

    [[maybe_unused]] auto r =
        fwfs::mint_durable_append_file(ctx, std::move(path));
    return 0;
}
