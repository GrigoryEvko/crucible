// A parked handle has the scope brand of the call of its frame door, as a
// plain handle has.  A body that gives its handle out of the frame would let
// the handle outlive the frame, and the gate of the result of the door
// refuses it.

#include <fixy/concurrent/ParkedChannel.h>
#include <foundation/permissions/Permission.h>

#include <utility>

struct WireTag {};

int main() {
    auto root = ::foundation::permissions::mint_permission_root<
        ::fixy::concurrent::channel_tag::Whole<::fixy::concurrent::ChannelForm::wire, WireTag>>();
    using Parked = ::fixy::concurrent::ParkedChannel<::fixy::concurrent::channel_t<int, 8, decltype(root)>>;
    Parked channel{};
    auto tokens =
        ::foundation::permissions::mint_permission_split<Parked::producer_tag, Parked::consumer_tag>(std::move(root));
    auto escaped = ::fixy::concurrent::with_producer(channel, std::move(tokens.at(::foundation::core::index<0>())),
                                                     [](auto producer) noexcept { return producer; });
    return 0;
}
