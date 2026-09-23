// The self side waits for a stop, and the peer side never sends one.  The
// self side refines the dual of the peer, because a receiver keeps no
// exit, so a check in that one direction admits the pair.  The peer does
// not refine the dual of the self side, because it drops the exit.  The
// mint asks for the two directions, so it refuses the pair.

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
struct foundation::permissions::splits_into_pack<tags::Whole, tags::Left, tags::Right> : std::true_type {};
template <>
struct foundation::permissions::splits_into_pack_authoring_witness<tags::Whole, tags::Left, tags::Right>
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

static_assert(s::is_subtype_async_v<AwaitsStop, s::dual_of_t<NeverStops>, RingEnd>,
              "the check in one direction admits the pair");

struct AnyBody {
    template <typename Head, typename Perm>
    auto operator()(Head head, Perm, BgCtx const&) noexcept {
        return head;
    }
};
}  // namespace

int main() {
    const BgCtx ctx{eff::testing::bg()};
    auto back = s::mint_forked_async_channel<AwaitsStop, NeverStops, tags::Left, tags::Right>(
        ctx, perm::mint_permission_root<tags::Whole>(), RingEnd{}, RingEnd{}, AnyBody{}, AnyBody{});
    perm::permission_drop(std::move(back));
    return 0;
}
