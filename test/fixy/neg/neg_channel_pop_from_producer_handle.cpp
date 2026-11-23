// The role lives in the handle type.  A producer handle exposes
// try_push and the snapshots, and it has no try_pop.  So a call site
// cannot drain a channel from the producer side.
//
// The permission is the right one and the frame door opens the producer
// handle, so the only thing missing is the member.

#include <fixy/concurrent/PermissionedChannel.h>

#include <utility>

namespace c = fixy::concurrent;
namespace perm = foundation::permissions;

namespace {
struct TraceTag {};
auto trace_root() noexcept {
    return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, TraceTag>>();
}
}  // namespace

using Channel = c::channel_t<int, 8, decltype(trace_root())>;

int main() {
    Channel channel{};
    auto tokens = perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(trace_root());
    auto token =
        c::with_producer(channel, std::move(tokens.at(::foundation::core::index<0>())), [](auto producer) noexcept {
            [[maybe_unused]] auto item = producer.try_pop();
            return std::move(producer).release();
        });
    return channel.is_closed() ? 0 : 1;
}
