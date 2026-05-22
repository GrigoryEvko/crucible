// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-083 HS14 fixture #1 of 2 for fixy::spawn::mint_spawn:
// CtxFitsSpawn rejects when the supplied Ctx::row does not admit
// Effect::Bg.
//
// Violation: HotFgCtx has row=Row<>.  Forking jthreads is an
// Effect::Bg event; the substrate's CtxFitsPermissionFork rejects
// on the row predicate, and the fixy facade's CtxFitsSpawn folds
// that gate IDENTICALLY.  Routing through
// `fixy::spawn::mint_spawn` must reject identically to
// `fixy::perm::mint_permission_fork` — the §XXI ctx-bound facade
// adds no escape hatch.
//
// Distinct from fixture #2 (no_splits_into_pack):
//   * Fixture #1 — splits_into_pack IS declared; HotFgCtx's empty
//     row trips the Bg-admission gate.
//   * Fixture #2 — BgDrainCtx admits Bg, but splits_into_pack is
//     NOT declared, tripping the structural-disjointness gate.
// Two distinct CtxFitsSpawn rejection paths ⇒ HS14 floor satisfied.
//
// Expected diagnostic: CtxFitsSpawn / CtxFitsPermissionFork /
// row_contains_v constraint is not satisfied.

#include <crucible/fixy/spawn/Spawn.h>

namespace neg_fixy_spawn_mint_spawn_ctx_no_bg {

struct Whole {};
struct Left {};
struct Right {};

}  // namespace neg_fixy_spawn_mint_spawn_ctx_no_bg

namespace crucible::safety {

// splits_into_pack IS declared — the rejection MUST fire on the
// Bg-row admission axis instead of the structural-disjointness axis.
template <>
struct splits_into_pack<
    neg_fixy_spawn_mint_spawn_ctx_no_bg::Whole,
    neg_fixy_spawn_mint_spawn_ctx_no_bg::Left,
    neg_fixy_spawn_mint_spawn_ctx_no_bg::Right> : std::true_type {};

template <>
struct splits_into_pack_authoring_witness<
    neg_fixy_spawn_mint_spawn_ctx_no_bg::Whole,
    neg_fixy_spawn_mint_spawn_ctx_no_bg::Left,
    neg_fixy_spawn_mint_spawn_ctx_no_bg::Right> : std::true_type {};

}  // namespace crucible::safety

int main() {
    namespace tags   = neg_fixy_spawn_mint_spawn_ctx_no_bg;
    namespace eff    = ::crucible::effects;
    namespace fspawn = ::crucible::fixy::spawn;
    namespace safe   = ::crucible::safety;

    auto whole = safe::mint_permission_root<tags::Whole>();
    // HotFgCtx::row = Row<> — no Bg.  CtxFitsSpawn folds
    // CtxFitsPermissionFork → CtxOwnsCapability<Ctx, Bg> → fail.
    auto rebuilt = fspawn::mint_spawn<tags::Left, tags::Right>(
        eff::HotFgCtx{},
        std::move(whole),
        [](safe::Permission<tags::Left>, eff::HotFgCtx const&) noexcept {},
        [](safe::Permission<tags::Right>, eff::HotFgCtx const&) noexcept {}
    );
    safe::permission_drop(std::move(rebuilt));
    return 0;
}
