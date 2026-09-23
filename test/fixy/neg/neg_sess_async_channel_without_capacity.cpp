// The two ends state no channel capacity.  The mint then has no capacity
// that it can trust for the asynchronous check, so it refuses the ends,
// although the pair holds at every capacity of one or more.

#include <fixy/session/AsyncChannel.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <type_traits>
#include <utility>

namespace s = ::fixy::session;
namespace perm = ::foundation::permissions;
namespace eff = ::foundation::effects;

namespace tags {
struct Whole {
    using permission_row = eff::Row<>;
};
struct Left {
    using permission_row = eff::Row<>;
};
struct Right {
    using permission_row = eff::Row<>;
};
}  // namespace tags

template <>
struct foundation::permissions::splits_into_pack<tags::Whole, tags::Left, tags::Right> : std::true_type {};
template <>
struct foundation::permissions::splits_into_pack_authoring_witness<tags::Whole, tags::Left, tags::Right>
    : std::true_type {};

namespace {
using BgCtx = eff::detail::ctx_witnesses::BgWitness;
struct Ping {};
struct Pong {};
struct SilentEnd {};
using LeftProto = s::Send<Ping, s::Recv<Pong, s::End>>;
using RightProto = s::Send<Pong, s::Recv<Ping, s::End>>;

struct AnyBody {
    template <typename Head, typename Perm>
    auto operator()(Head head, Perm, BgCtx const&) noexcept {
        return head;
    }
};
}  // namespace

int main() {
    const BgCtx ctx{eff::testing::bg()};
    auto back = s::mint_forked_async_channel<LeftProto, RightProto, tags::Left, tags::Right>(
        ctx, perm::mint_permission_root<tags::Whole>(), SilentEnd{}, SilentEnd{}, AnyBody{}, AnyBody{});
    perm::permission_drop(std::move(back));
    return 0;
}
