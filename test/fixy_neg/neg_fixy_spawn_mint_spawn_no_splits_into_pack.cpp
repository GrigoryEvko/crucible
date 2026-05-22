// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-083 HS14 fixture #2 of 2 for fixy::spawn::mint_spawn:
// CtxFitsSpawn rejects when `splits_into_pack<Parent, Children...>`
// is NOT declared.
//
// Distinct from fixture #1 (ctx_no_bg):
//   * Fixture #1 — splits_into_pack IS declared; HotFgCtx's empty
//     row trips the Bg-admission gate (row predicate inside
//     CtxFitsPermissionFork fails).
//   * Fixture #2 — BgDrainCtx admits Bg (row predicate passes),
//     but splits_into_pack<Whole, Left, Right> is NOT specialized;
//     the default false_type makes splits_into_pack_v evaluate
//     false → CtxFitsSpawn fails on the structural-disjointness
//     axis.
// Two distinct CtxFitsSpawn rejection paths ⇒ HS14 floor satisfied.
//
// Expected diagnostic: splits_into_pack / CtxFitsSpawn /
// CtxFitsPermissionFork constraint is not satisfied.

#include <crucible/fixy/spawn/Spawn.h>

namespace neg_fixy_spawn_mint_spawn_no_splits {

// Three pure tags with NO splits_into_pack specialization anywhere
// in the program.  Default trait => false_type =>
// splits_into_pack_v<Whole, Left, Right> == false.
struct Whole {};
struct Left {};
struct Right {};

}  // namespace neg_fixy_spawn_mint_spawn_no_splits

int main() {
    namespace tags   = neg_fixy_spawn_mint_spawn_no_splits;
    namespace eff    = ::crucible::effects;
    namespace fspawn = ::crucible::fixy::spawn;
    namespace safe   = ::crucible::safety;

    // BgDrainCtx::row contains Effect::Bg, so the Bg-admission
    // predicate inside CtxFitsSpawn passes.  The remaining gate —
    // `splits_into_pack_v<Whole, Left, Right>` — fails because no
    // specialization exists.
    auto whole = safe::mint_permission_root<tags::Whole>();
    auto rebuilt = fspawn::mint_spawn<tags::Left, tags::Right>(
        eff::BgDrainCtx{},
        std::move(whole),
        [](safe::Permission<tags::Left>, eff::BgDrainCtx const&) noexcept {},
        [](safe::Permission<tags::Right>, eff::BgDrainCtx const&) noexcept {}
    );
    safe::permission_drop(std::move(rebuilt));
    return 0;
}
