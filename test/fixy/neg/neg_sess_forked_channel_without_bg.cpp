// A forked channel starts two threads, which is background work.  The
// foreground context has no Bg effect, so the context gate refuses the
// fork, and so the channel.  The bodies here are correct.

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
using FgCtx = eff::detail::ctx_witnesses::FgWitness;
using Proto = s::Send<Msg, s::End>;
}  // namespace

namespace foundation::permissions {
template <>
struct splits_into_pack<Whole, Left, Right> : std::true_type {};
template <>
struct splits_into_pack_authoring_witness<Whole, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    auto whole = perm::mint_permission_root<Whole>();
    auto back = s::mint_forked_channel<Proto, Left, Right>(
        FgCtx{}, std::move(whole), Wire{}, Wire{},
        [](auto head, perm::Permission<Left>, FgCtx const&) noexcept {
            return std::move(head).send(Msg{}, [](Wire&, Msg&&) noexcept {});
        },
        [](auto head, perm::Permission<Right>, FgCtx const&) noexcept {
            auto [msg, at_end] = std::move(head).recv([](Wire&) noexcept { return Msg{}; });
            (void)msg;
            return std::move(at_end);
        });
    perm::permission_drop(std::move(back));
    return 0;
}
