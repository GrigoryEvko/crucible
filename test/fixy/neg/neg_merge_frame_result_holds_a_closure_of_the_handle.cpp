// The body of a frame door of a merge gives back a closure that holds its
// producer handle, so the handle and its share of the pool would leave the
// frame inside the closure.  The type of the closure names the scope brand of
// the frame, so the gate of the door refuses the result.

#include <fixy/concurrent/PermissionedChannel.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace c = ::fixy::concurrent;
namespace perm = ::foundation::permissions;

namespace {
struct LogTag {};
auto log_root() noexcept { return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::merge, LogTag>>(); }
}  // namespace

using Channel = c::channel_t<int, 8, decltype(log_root())>;

int main() {
    auto tokens = perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(log_root());
    Channel channel{std::move(tokens.at(::foundation::core::index<0>()))};
    auto escaped = c::with_producer(channel, ::foundation::effects::testing::test().nondet, [](auto producer) noexcept {
        return [held = std::move(producer)]() mutable noexcept { return held.try_push(7); };
    });
    return escaped.is_some() ? 0 : 1;
}
