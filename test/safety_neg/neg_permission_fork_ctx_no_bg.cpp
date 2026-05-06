// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-073: mint_permission_fork(ctx, ...) requires ctx.row to admit Bg.
//
// Violation: HotFgCtx has row=Row<>.  Forking jthreads is an Effect::Bg
// event, so the CtxFitsPermissionFork gate rejects the call before any
// permission split or child body invocation can be minted.
//
// Expected diagnostic: CtxFitsPermissionFork / row_contains_v constraint
// is not satisfied.

#include <crucible/permissions/PermissionFork.h>

namespace neg_permission_fork_ctx_no_bg {

struct Whole {};
struct Left {};
struct Right {};

}  // namespace neg_permission_fork_ctx_no_bg

namespace crucible::safety {

template <>
struct splits_into_pack<
    neg_permission_fork_ctx_no_bg::Whole,
    neg_permission_fork_ctx_no_bg::Left,
    neg_permission_fork_ctx_no_bg::Right> : std::true_type {};

}  // namespace crucible::safety

int main() {
    namespace tags = neg_permission_fork_ctx_no_bg;
    namespace eff = ::crucible::effects;
    namespace safe = ::crucible::safety;

    auto whole = safe::mint_permission_root<tags::Whole>();
    auto rebuilt = safe::mint_permission_fork<tags::Left, tags::Right>(
        eff::HotFgCtx{},
        std::move(whole),
        [](safe::Permission<tags::Left>, eff::HotFgCtx const&) noexcept {},
        [](safe::Permission<tags::Right>, eff::HotFgCtx const&) noexcept {}
    );
    safe::permission_drop(std::move(rebuilt));
    return 0;
}
