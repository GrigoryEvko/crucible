// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// detail::permission_fork_ is the body of mint_permission_fork.  Any
// translation unit can name it, so the body asserts the mint's gate
// itself: a direct call under the foreground context, which owns no
// background effect, is refused the same way the mint refuses it.
//
// Expected diagnostic: the body's static assertion names the mint's gate.

#include <crucible/permissions/_PermissionFork.h>

namespace neg_permission_fork_detail_body_needs_the_gate {

struct Whole {};
struct Left {};
struct Right {};

}  // namespace neg_permission_fork_detail_body_needs_the_gate

namespace crucible::safety {

template <>
struct splits_into_pack<neg_permission_fork_detail_body_needs_the_gate::Whole,
                        neg_permission_fork_detail_body_needs_the_gate::Left,
                        neg_permission_fork_detail_body_needs_the_gate::Right> : std::true_type {};

}  // namespace crucible::safety

int main() {
    namespace tags = neg_permission_fork_detail_body_needs_the_gate;
    namespace eff = ::crucible::effects;
    namespace safe = ::crucible::safety;

    auto whole = safe::mint_permission_root<tags::Whole>();
    auto rebuilt = safe::detail::permission_fork_<tags::Left, tags::Right>(
        eff::HotFgCtx{}, std::move(whole), [](safe::Permission<tags::Left>, eff::HotFgCtx const&) noexcept {},
        [](safe::Permission<tags::Right>, eff::HotFgCtx const&) noexcept {});
    safe::permission_drop(std::move(rebuilt));
    return 0;
}
