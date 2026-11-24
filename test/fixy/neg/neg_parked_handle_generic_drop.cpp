// A parked handle owes a consume at its end: close ends the stream and
// notifies items, and release gives the token back.  The generic drop door
// of foundation/Linear.h would end the handle with no notify, so a consumer
// that parks on items would never wake.  The drop door refuses a class that
// owes a consume, as it refuses a live fixy::Linear.

#include <fixy/concurrent/ParkedChannel.h>
#include <foundation/Linear.h>
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
    ::fixy::concurrent::with_producer(channel, std::move(tokens.at(::foundation::core::index<0>())),
                                      [](auto producer) noexcept { ::foundation::linear::drop(std::move(producer)); });
    return 0;
}
