// A context of the hot path owns no Block, so the consumer handle of a channel
// refuses it a wait policy that parks (the policy of
// fixy/concurrent/EventCount.h).  The foreground asks the handle for
// SpinThenPark, so it would sleep in the kernel on the hot path.

#include <fixy/concurrent/EventCount.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

struct WireTag {};

int main() {
    auto root = ::foundation::permissions::mint_permission_root<
        ::fixy::concurrent::channel_tag::Whole<::fixy::concurrent::ChannelForm::wire, WireTag>>();
    using Wire = ::fixy::concurrent::channel_t<int, 8, decltype(root)>;
    Wire channel{};
    ::fixy::concurrent::EventCount events{};
    auto tokens =
        ::foundation::permissions::mint_permission_split<Wire::producer_tag, Wire::consumer_tag>(std::move(root));
    auto token = ::fixy::concurrent::with_consumer(
        channel, std::move(tokens.at(::foundation::core::index<1>())), [&events](auto consumer) noexcept {
            [[maybe_unused]] auto got = consumer.template receive_waiting<::fixy::concurrent::SpinThenPark<1000>>(
                ::foundation::effects::testing::foreground(), events);
            return std::move(consumer).release();
        });
    return 0;
}
