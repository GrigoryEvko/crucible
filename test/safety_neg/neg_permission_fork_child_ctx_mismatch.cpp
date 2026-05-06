// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-073: child bodies inherit the row-admitted Ctx used at the mint.
//
// Violation: the mint uses BgDrainCtx, but both child bodies demand
// HotFgCtx.  The permission split is valid and BgDrainCtx admits Bg, so
// the failure is specifically the child callable signature gate:
// Callable_i(Permission<Child_i>, BgDrainCtx const&) is required.
//
// Expected diagnostic: permission_fork_ctx_callables / constraints not
// satisfied for the child callable signature.

#include <crucible/permissions/PermissionFork.h>

namespace neg_permission_fork_child_ctx_mismatch {

struct Whole {};
struct Left {};
struct Right {};

}  // namespace neg_permission_fork_child_ctx_mismatch

namespace crucible::safety {

template <>
struct splits_into_pack<
    neg_permission_fork_child_ctx_mismatch::Whole,
    neg_permission_fork_child_ctx_mismatch::Left,
    neg_permission_fork_child_ctx_mismatch::Right> : std::true_type {};

}  // namespace crucible::safety

int main() {
    namespace tags = neg_permission_fork_child_ctx_mismatch;
    namespace eff = ::crucible::effects;
    namespace safe = ::crucible::safety;

    auto whole = safe::mint_permission_root<tags::Whole>();
    auto rebuilt = safe::mint_permission_fork<tags::Left, tags::Right>(
        eff::BgDrainCtx{},
        std::move(whole),
        [](safe::Permission<tags::Left>, eff::HotFgCtx const&) noexcept {},
        [](safe::Permission<tags::Right>, eff::HotFgCtx const&) noexcept {}
    );
    safe::permission_drop(std::move(rebuilt));
    return 0;
}
