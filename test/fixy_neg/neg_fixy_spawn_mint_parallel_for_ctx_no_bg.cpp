// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-083 HS14 fixture #1 of 2 for fixy::spawn::mint_parallel_for:
// CtxFitsParallelFor rejects when the supplied Ctx::row does not
// admit Effect::Bg.
//
// Violation: HotFgCtx has row=Row<>.  parallel_for_views<N>'s N>=2
// path spawns N jthreads — an Effect::Bg event.  The fixy facade's
// CtxFitsParallelFor folds CtxOwnsCapability<Ctx, Effect::Bg> into
// its single concept gate so the rejection is uniform across N
// (even N=1 fails for shape consistency).  Routing through
// `fixy::spawn::mint_parallel_for` must reject identically.
//
// Distinct from fixture #2 (body_signature):
//   * Fixture #1 — body shape is CORRECT (takes OwnedRegion of
//     Slice<Whole, 0>); HotFgCtx's empty row trips the Bg-admission
//     gate.  Rejection on the CtxOwnsCapability axis.
//   * Fixture #2 — BgDrainCtx admits Bg; body takes the WRONG
//     OwnedRegion shape (Whole-tagged instead of Slice-tagged).
//     Rejection on the body-signature noexcept-invocable axis.
// Two distinct CtxFitsParallelFor rejection paths ⇒ HS14 floor.
//
// Expected diagnostic: CtxFitsParallelFor / CtxOwnsCapability /
// row_contains_v constraint is not satisfied.

#include <crucible/fixy/spawn/Spawn.h>
#include <crucible/permissions/Permission.h>

namespace neg_fixy_spawn_mint_parallel_for_ctx_no_bg {

struct Whole {};

// Static storage backing the OwnedRegion — keeps the fixture free
// of arena setup.  parallel_for_views does not read the bytes; the
// rejection fires at the concept-gate before any body invocation.
static int storage_[8];

}  // namespace neg_fixy_spawn_mint_parallel_for_ctx_no_bg

int main() {
    namespace tags   = neg_fixy_spawn_mint_parallel_for_ctx_no_bg;
    namespace eff    = ::crucible::effects;
    namespace fspawn = ::crucible::fixy::spawn;
    namespace safe   = ::crucible::safety;

    auto whole = safe::mint_permission_root<tags::Whole>();
    auto region = safe::OwnedRegion<int, tags::Whole>::wrap(
        tags::storage_, 8, std::move(whole));

    // HotFgCtx::row = Row<> — no Bg.  CtxFitsParallelFor folds
    // CtxOwnsCapability<HotFgCtx, Effect::Bg> → fail.
    auto rebuilt = fspawn::mint_parallel_for<2>(
        eff::HotFgCtx{},
        std::move(region),
        [](safe::OwnedRegion<int, safe::Slice<tags::Whole, 0>>&&) noexcept {}
    );
    (void)rebuilt;
    return 0;
}
