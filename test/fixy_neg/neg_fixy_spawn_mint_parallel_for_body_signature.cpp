// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-083 HS14 fixture #2 of 2 for fixy::spawn::mint_parallel_for:
// CtxFitsParallelFor rejects when the body callable does not accept
// the per-shard `OwnedRegion<T, Slice<Whole, 0>>&&` rvalue.
//
// Violation: BgDrainCtx admits Bg (the row predicate passes), but
// the body takes `OwnedRegion<T, Whole>&&` instead of the
// Slice-tagged sub-region rvalue.  Slice<Whole, 0> is structurally
// disjoint from Whole (distinct tag → distinct Permission identity);
// the substrate's parallel_for_views<N> partitions Whole into N
// disjoint Slice<Whole, I>'s and dispatches each to a worker.  A
// body that expects the whole tag cannot be invoked on a slice.
//
// CtxFitsParallelFor folds the noexcept-invocability gate
//   std::is_nothrow_invocable_v<Body&,
//                               OwnedRegion<T, Slice<Whole, 0>>&&>
// into its single concept gate.  Wrong-shape body fails here.
//
// Distinct from fixture #1 (ctx_no_bg):
//   * Fixture #1 — body shape is CORRECT; HotFgCtx's empty row
//     trips the Bg-admission gate.  Rejection on CtxOwnsCapability.
//   * Fixture #2 — BgDrainCtx admits Bg; body shape is WRONG.
//     Rejection on the body-signature is_nothrow_invocable axis.
// Two distinct CtxFitsParallelFor rejection paths ⇒ HS14 floor.
//
// Expected diagnostic: CtxFitsParallelFor / is_nothrow_invocable /
// OwnedRegion<.*, Slice<.*>> / constraint is not satisfied.

#include <crucible/fixy/spawn/Spawn.h>
#include <crucible/permissions/Permission.h>

namespace neg_fixy_spawn_mint_parallel_for_body_signature {

struct Whole {};

static int storage_[8];

}  // namespace neg_fixy_spawn_mint_parallel_for_body_signature

int main() {
    namespace tags   = neg_fixy_spawn_mint_parallel_for_body_signature;
    namespace eff    = ::crucible::effects;
    namespace fspawn = ::crucible::fixy::spawn;
    namespace safe   = ::crucible::safety;

    auto whole = safe::mint_permission_root<tags::Whole>();
    auto region = safe::OwnedRegion<int, tags::Whole>::wrap(
        tags::storage_, 8, std::move(whole));

    // BgDrainCtx::row admits Bg, so CtxOwnsCapability passes.  Body
    // takes the WHOLE tag, not Slice<Whole, 0> — the noexcept-invocable
    // gate inside CtxFitsParallelFor rejects on parameter-type mismatch.
    auto rebuilt = fspawn::mint_parallel_for<2>(
        eff::BgDrainCtx{},
        std::move(region),
        [](safe::OwnedRegion<int, tags::Whole>&&) noexcept {}
    );
    (void)rebuilt;
    return 0;
}
