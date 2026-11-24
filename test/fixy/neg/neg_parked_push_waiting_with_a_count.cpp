// The producer of a parked channel (fixy/concurrent/ParkedChannel.h) waits on
// the room count that the channel owns, so its push_waiting takes no event
// count.  A caller that gives one would pair the wait with a count that no
// pop notifies, and the producer would park on a full ring with no wake.

#include <fixy/Ctx.h>
#include <fixy/concurrent/EventCount.h>
#include <fixy/concurrent/ParkedChannel.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

struct WireTag {};

int main() {
    auto root = ::foundation::permissions::mint_permission_root<
        ::fixy::concurrent::channel_tag::Whole<::fixy::concurrent::ChannelForm::wire, WireTag>>();
    using Parked = ::fixy::concurrent::ParkedChannel<::fixy::concurrent::channel_t<int, 8, decltype(root)>>;
    Parked channel{};
    ::fixy::concurrent::EventCount room{};
    ::fixy::BgLoadCtx const ctx{::foundation::effects::testing::bg()};
    auto tokens =
        ::foundation::permissions::mint_permission_split<Parked::producer_tag, Parked::consumer_tag>(std::move(root));
    auto token = ::fixy::concurrent::with_producer(
        channel, std::move(tokens.at(::foundation::core::index<0>())), [&room, &ctx](auto producer) noexcept {
            [[maybe_unused]] auto pushed =
                producer.template push_waiting<::fixy::concurrent::SpinThenPark<0>>(ctx, room, 1);
            return std::move(producer).release();
        });
    return 0;
}
