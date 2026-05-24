// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-215 HS14 fixture #2 of 2 for fixy::pipe::pool_submit:
// CtxFitsPoolSubmit rejects when the supplied Ctx's row does not
// admit Effect::Bg.
//
// Violation: HotFgCtx::row = Row<> — Pool dispatches work onto worker
// jthreads, which is an Effect::Bg event.  CtxOwnsCapability<Ctx, Bg>
// fails, and the fixy facade's CtxFitsPoolSubmit folds that gate
// identically.  Routing through `fixy::pipe::pool_submit` must
// reject identically — the §XXI ctx-bound facade adds no escape hatch.
//
// Distinct from fixture #1 (permission_capture):
//   * Fixture #1 — Ctx admits Bg; closure is non-copy-constructible
//     (captured Permission<Tag> deletes copy), PermissionFreeJob fails.
//   * Fixture #2 — Ctx does NOT admit Bg; closure IS copy-constructible,
//     but the row-fit gate fails on the CtxOwnsCapability axis.
// Two distinct CtxFitsPoolSubmit rejection axes ⇒ HS14 floor satisfied.
//
// Expected diagnostic: CtxFitsPoolSubmit / CtxOwnsCapability /
// row_contains_v constraint is not satisfied.

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Pipe.h>

#include <utility>

int main() {
    namespace eff   = ::crucible::effects;
    namespace fpipe = ::crucible::fixy::pipe;

    // HotFgCtx::row = Row<> — no Effect::Bg.  This is the canonical
    // foreground-dispatch ctx; Pool work routing is a Bg event.
    eff::HotFgCtx fg{};
    fpipe::Pool<> pool{fpipe::CoreCount{1}};

    // The JOB shape is fine — capture-free, copy-constructible — so
    // PermissionFreeJob would have passed.  The rejection rides on
    // CtxOwnsCapability<HotFgCtx, Effect::Bg> instead.
    auto safe_job = [](){};

    // This call MUST fail to compile.  If it ever succeeds, work could
    // route onto Bg workers while the caller's ctx claims foreground-
    // only progress — a row-coherence violation that breaks the
    // ParallelismDecision cost model + every downstream observer
    // metric that conditions on Ctx::row.
    fpipe::pool_submit(fg, pool, safe_job);

    pool.wait_idle();
    return 0;
}
