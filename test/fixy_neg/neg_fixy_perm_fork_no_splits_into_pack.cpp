// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Perm fixture #2b (FIXY-U-074 HS14 round-out for
// fixy::perm::mint_permission_fork):
// CtxFitsPermissionFork rejects when `splits_into_pack<Parent,
// Children...>` is NOT specialized (default = std::false_type =>
// splits_into_pack_v == false).
//
// Distinct from fixture #2 (neg_fixy_perm_fork_ctx_no_bg):
//   * Fixture #2 declares `splits_into_pack` correctly (the parent
//     CAN structurally split into the children) but supplies
//     HotFgCtx whose row=Row<> does not contain Effect::Bg —
//     rejection fires on the row predicate inside
//     CtxFitsPermissionFork.
//   * Fixture #2b supplies BgDrainCtx (row admits Bg, so the row
//     predicate passes) but DOES NOT declare a
//     `splits_into_pack<Whole, Left, Right>` specialization — the
//     default `false_type` makes `splits_into_pack_v` evaluate to
//     false, and CtxFitsPermissionFork rejects on the
//     splits_into_pack predicate instead.
//
// Two distinct CtxFitsPermissionFork rejection paths ⇒ HS14 is
// satisfied for the ctx-bound mint_permission_fork factory.
//
// Expected diagnostic: splits_into_pack | CtxFitsPermissionFork |
//                      constraints not satisfied.

#include <crucible/fixy/Perm.h>

namespace neg_fixy_perm_fork_no_splits_into_pack {

// Three pure tags with NO splits_into_pack specialization anywhere
// in the program.  Default trait => false_type =>
// splits_into_pack_v<Whole, Left, Right> == false.
struct Whole {};
struct Left {};
struct Right {};

}  // namespace neg_fixy_perm_fork_no_splits_into_pack

int main() {
    namespace tags  = neg_fixy_perm_fork_no_splits_into_pack;
    namespace eff   = ::crucible::effects;
    namespace fperm = ::crucible::fixy::perm;
    namespace safe  = ::crucible::safety;

    // BgDrainCtx::row contains Effect::Bg, so the row predicate
    // inside CtxFitsPermissionFork passes.  The remaining gate —
    // `splits_into_pack_v<Whole, Left, Right>` — is the one that
    // fails because no specialization exists.
    auto whole = fperm::mint_permission_root<tags::Whole>();
    auto rebuilt = fperm::mint_permission_fork<tags::Left, tags::Right>(
        eff::BgDrainCtx{},
        std::move(whole),
        [](safe::Permission<tags::Left>, eff::BgDrainCtx const&) noexcept {},
        [](safe::Permission<tags::Right>, eff::BgDrainCtx const&) noexcept {}
    );
    safe::permission_drop(std::move(rebuilt));
    return 0;
}
