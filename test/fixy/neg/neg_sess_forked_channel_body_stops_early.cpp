// Each body of a forked channel must return its endpoint at End.  This
// peer body returns its endpoint before the receive, so the protocol is
// not done when the thread ends.  The body gate refuses it, and the
// channel is never made.

#include <fixy/session/Handle.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <type_traits>
#include <utility>

namespace {
namespace s = ::fixy::session;
namespace eff = ::foundation::effects;
namespace perm = ::foundation::permissions;

struct Whole {
    using permission_row = eff::Row<>;
};
struct Left {
    using permission_row = eff::Row<>;
};
struct Right {
    using permission_row = eff::Row<>;
};

struct Msg {};
struct Wire {};
using DrainCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;
using Proto = s::Send<Msg, s::End>;
}  // namespace

namespace foundation::permissions {
template <>
struct splits_into_pack<Whole, Left, Right> : std::true_type {};
template <>
struct splits_into_pack_authoring_witness<Whole, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    const DrainCtx ctx{eff::testing::bg()};
    auto whole = perm::mint_permission_root<Whole>();
    auto back = s::mint_forked_channel<Proto, Left, Right>(
        ctx, std::move(whole), Wire{}, Wire{},
        [](auto head, perm::Permission<Left>, DrainCtx const&) noexcept {
            return std::move(head).send(Msg{}, [](Wire&, Msg&&) noexcept {});
        },
        [](auto head, perm::Permission<Right>, DrainCtx const&) noexcept { return head; });
    perm::permission_drop(std::move(back));
    return 0;
}
