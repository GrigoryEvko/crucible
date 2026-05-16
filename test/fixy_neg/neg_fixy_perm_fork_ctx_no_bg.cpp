// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Perm fixture #2: permission_fork via fixy:: alias rejects
// when the supplied Ctx::row does not admit Effect::Bg.
//
// Violation: HotFgCtx has row=Row<>.  Forking jthreads is an
// Effect::Bg event, so CtxFitsPermissionFork rejects.  Routing
// through `fixy::perm::mint_permission_fork` must reject identically.
//
// Expected diagnostic: CtxFitsPermissionFork / row_contains_v
// constraint is not satisfied.

#include <crucible/fixy/Perm.h>

namespace neg_fixy_perm_fork_ctx_no_bg {

struct Whole {};
struct Left {};
struct Right {};

}  // namespace neg_fixy_perm_fork_ctx_no_bg

namespace crucible::safety {

template <>
struct splits_into_pack<
    neg_fixy_perm_fork_ctx_no_bg::Whole,
    neg_fixy_perm_fork_ctx_no_bg::Left,
    neg_fixy_perm_fork_ctx_no_bg::Right> : std::true_type {};

}  // namespace crucible::safety

int main() {
    namespace tags  = neg_fixy_perm_fork_ctx_no_bg;
    namespace eff   = ::crucible::effects;
    namespace fperm = ::crucible::fixy::perm;
    namespace safe  = ::crucible::safety;

    auto whole = fperm::mint_permission_root<tags::Whole>();
    auto rebuilt = fperm::mint_permission_fork<tags::Left, tags::Right>(
        eff::HotFgCtx{},
        std::move(whole),
        [](safe::Permission<tags::Left>, eff::HotFgCtx const&) noexcept {},
        [](safe::Permission<tags::Right>, eff::HotFgCtx const&) noexcept {}
    );
    safe::permission_drop(std::move(rebuilt));
    return 0;
}
