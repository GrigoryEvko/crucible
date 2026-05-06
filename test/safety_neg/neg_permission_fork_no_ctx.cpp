// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-073: ctx-aware permission_fork bodies require the ctx-bound mint.
//
// Violation: the caller supplies child bodies with the
// Callable_i(Permission<Child_i>, ChildCtx_i) shape but omits the Ctx
// argument.  There is no no-ctx mint_permission_fork overload.
//
// Expected diagnostic: no matching mint_permission_fork overload.

#include <crucible/permissions/PermissionFork.h>

namespace neg_permission_fork_no_ctx {

struct Whole {};
struct Left {};
struct Right {};

}  // namespace neg_permission_fork_no_ctx

namespace crucible::safety {

template <>
struct splits_into_pack<
    neg_permission_fork_no_ctx::Whole,
    neg_permission_fork_no_ctx::Left,
    neg_permission_fork_no_ctx::Right> : std::true_type {};

}  // namespace crucible::safety

int main() {
    namespace tags = neg_permission_fork_no_ctx;
    namespace eff = ::crucible::effects;
    namespace safe = ::crucible::safety;

    auto whole = safe::mint_permission_root<tags::Whole>();
    auto rebuilt = safe::mint_permission_fork<tags::Left, tags::Right>(
        std::move(whole),
        [](safe::Permission<tags::Left>, eff::BgDrainCtx const&) noexcept {},
        [](safe::Permission<tags::Right>, eff::BgDrainCtx const&) noexcept {}
    );
    safe::permission_drop(std::move(rebuilt));
    return 0;
}
