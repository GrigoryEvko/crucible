// The same pair as neg_sess_async_channel_peer_drops_the_exit, with the
// sides in the other order.  The self side never sends the stop that the
// peer waits for, so the self side does not refine the dual of the peer.
// The mint refuses the pair in each order.

#include <fixy/session/AsyncChannel.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
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
struct foundation::permissions::can_split_into_pack<tags::Whole, tags::Left, tags::Right> : std::true_type {};
template <>
struct foundation::permissions::has_split_pack_authoring_witness<tags::Whole, tags::Left, tags::Right>
    : std::true_type {};

namespace {
using BgCtx = eff::detail::ctx_witnesses::BgWitness;
struct Job {};
struct StopCmd {};
struct RingEnd {
    static constexpr std::size_t channel_capacity = 1;
};
using AwaitsStop = s::Loop<s::Offer<s::Recv<Job, s::Continue>, s::Recv<StopCmd, s::End>>>;
using NeverStops = s::Loop<s::Select<s::Send<Job, s::Continue>>>;

struct AnyBody {
    template <typename Head, typename View>
    auto operator()(Head head, View const&, BgCtx const&) noexcept {
        return head;
    }
};
}  // namespace

int main() {
    const BgCtx ctx{eff::testing::bg()};
    auto back = s::mint_forked_async_channel<NeverStops, AwaitsStop, tags::Left, tags::Right>(
        ctx, perm::mint_permission_root<tags::Whole>(), RingEnd{}, RingEnd{}, AnyBody{}, AnyBody{});
    perm::permission_drop(std::move(back));
    return 0;
}
