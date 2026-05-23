// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-228 fixture #5 — ColdInitCtx rejected at mint_head_advancer.
//
// `CtxFitsHeadAdvancerMint<Ctx, Extras...>` folds in
// `::crucible::fixy::fs::CtxAdmitsIoBlock<Ctx>` — the caller's row
// must admit BOTH Effect::IO AND Effect::Block.  HEAD-pointer advance
// involves `::open()` + later `::renameat2(RENAME_NOREPLACE)` and a
// directory `::fsync()` on the parent dirfd; any of those can block.
// Minting from a non-blocking init phase would license the eventual
// commit to park indefinitely from a context that promised it would
// not.
//
// `ColdInitCtx` is `Row<Init, Alloc, IO>` — admits IO but NOT Block,
// so `CtxAdmitsIoBlock<ColdInitCtx>` is false and the §XXI mint's
// requires-clause refuses the instantiation.
//
// Mismatch class: ctx-row engagement gap (lacks Effect::Block).
// Distinct from fixture #6 (extras_engage_atomic_write).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "no matching function" / "CtxFitsHeadAdvancerMint" /
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
        "/tmp/crucible_neg_v228_head_ctx_lacks_block"};

    // Should FAIL: mint_head_advancer's CtxFitsHeadAdvancerMint folds
    // in CtxAdmitsIoBlock<Ctx>; ColdInitCtx's row lacks Effect::Block.
    [[maybe_unused]] auto r = fwcd::mint_head_advancer<>(
        ctx, std::move(path));
    return 0;
}
