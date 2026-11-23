// A producer handle owns the linear producer permission of the channel.
// A copy would leave two handles, and each would hold the one producer
// role.  One thread writes the head of the SPSC ring, with no
// synchronization against a second writer.
//
// The frame door gives the body the handle, and the consumer side and the
// channel stay as they are.  So only the deleted copy constructor of the
// handle can refuse this.

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
            auto second = producer;
            [[maybe_unused]] bool const is_pushed = second.try_push(1);
            return std::move(producer).release();
        });
    return channel.is_closed() ? 0 : 1;
}
