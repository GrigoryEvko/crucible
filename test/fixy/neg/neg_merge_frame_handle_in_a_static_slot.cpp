// The body of a frame door of a merge moves its producer handle into an
// object of static storage duration, which outlives the frame, the pool share
// and the channel.  The type of the handle carries the scope brand of the
// frame, and the rule brand-escape of the quarantine plugin refuses an object
// of static or thread storage duration that holds a scope brand.

#include <fixy/Core.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace c = ::fixy::concurrent;
namespace perm = ::foundation::permissions;

namespace {
struct LogTag {};
auto log_root() noexcept { return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::merge, LogTag>>(); }

template <class Value>
::fixy::Option<Value>& slot_of() noexcept {
    static ::fixy::Option<Value> slot{};
    return slot;
}
}  // namespace

using Channel = c::channel_t<int, 8, decltype(log_root())>;

int main() {
    auto tokens = perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(log_root());
    Channel channel{std::move(tokens.at(::foundation::core::index<0>()))};
    auto const lent =
        c::with_producer(channel, ::foundation::effects::testing::test().nondet,
                         [](auto producer) noexcept { slot_of<decltype(producer)>().fill(std::move(producer)); });
    return lent.is_some() ? 0 : 1;
}
