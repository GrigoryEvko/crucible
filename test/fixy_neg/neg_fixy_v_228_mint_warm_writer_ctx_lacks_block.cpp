// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-228 fixture #1 — ColdInitCtx rejected at mint_warm_writer.
//
// `CtxFitsWarmWriterMint<Ctx, Extras...>` folds in
// `::crucible::fixy::fs::CtxAdmitsIoBlock<Ctx>` — the caller's row
// must admit BOTH Effect::IO AND Effect::Block.  Opening a warm-tier
// writer in a non-blocking context would license a future ::open()
// call to block from a phase that promised it would not.
//
// `ColdInitCtx` is `Row<Init, Alloc, IO>` — admits IO but NOT Block,
// so `CtxAdmitsIoBlock` fails and the §XXI mint's requires-clause
// refuses the instantiation.
//
// Mismatch class: ctx-row engagement gap (lacks Effect::Block).
// Distinct from fixture #2 (extras_engage_mode).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "no matching function" / "CtxFitsWarmWriterMint" /
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
        "/tmp/crucible_neg_v228_warm_ctx_lacks_block"};

    // Should FAIL: mint_warm_writer's CtxFitsWarmWriterMint folds in
    // CtxAdmitsIoBlock<Ctx>; ColdInitCtx's row lacks Effect::Block.
    [[maybe_unused]] auto r = fwcd::mint_warm_writer<>(
        ctx, std::move(path));
    return 0;
}
