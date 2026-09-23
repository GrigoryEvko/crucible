// The left side sends two messages before it receives, and the two ends
// state a channel that holds one message each way.  The relation admits
// the pair at capacity two, but the mint checks at the capacity that the
// ends state, so it refuses the pair.

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
struct Ping {};
struct Pong {};
struct NarrowEnd {
    static constexpr std::size_t channel_capacity = 1;
};
using EagerLeft = s::Send<Ping, s::Send<Ping, s::Recv<Pong, s::End>>>;
using PatientRight = s::Send<Pong, s::Recv<Ping, s::Recv<Ping, s::End>>>;

struct AnyBody {
    template <typename Head, typename Perm>
    auto operator()(Head head, Perm, BgCtx const&) noexcept {
        return head;
    }
};
}  // namespace

int main() {
    const BgCtx ctx{eff::testing::bg()};
    auto back = s::mint_forked_async_channel<EagerLeft, PatientRight, tags::Left, tags::Right>(
        ctx, perm::mint_permission_root<tags::Whole>(), NarrowEnd{}, NarrowEnd{}, AnyBody{}, AnyBody{});
    perm::permission_drop(std::move(back));
    return 0;
}
