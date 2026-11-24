// The consumer of a parked channel (fixy/concurrent/ParkedChannel.h) waits on
// the items count that the channel owns, so its receive_waiting takes no
// event count.  A caller that gives one would pair the wait with a count that
// no push notifies, and the consumer would park with no wake.

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
    ::fixy::concurrent::EventCount events{};
    ::fixy::BgLoadCtx const ctx{::foundation::effects::testing::bg()};
    auto tokens =
        ::foundation::permissions::mint_permission_split<Parked::producer_tag, Parked::consumer_tag>(std::move(root));
    auto token = ::fixy::concurrent::with_consumer(
        channel, std::move(tokens.at(::foundation::core::index<1>())), [&events, &ctx](auto consumer) noexcept {
            [[maybe_unused]] auto got =
                consumer.template receive_waiting<::fixy::concurrent::SpinThenPark<0>>(ctx, events);
            return std::move(consumer).release();
        });
    return 0;
}
