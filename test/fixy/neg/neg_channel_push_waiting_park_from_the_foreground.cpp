// A context of the hot path owns no Block, so the producer handle of a channel
// refuses it a wait policy that parks (the policy of
// fixy/concurrent/EventCount.h).  The foreground asks the push that waits for
// SpinThenPark, so a push into a full channel would sleep in the kernel on the
// hot path.

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
    ::fixy::concurrent::EventCount room{};
    auto [producer_perm, consumer_perm] =
        ::foundation::permissions::mint_permission_split<Wire::producer_tag, Wire::consumer_tag>(std::move(root));
    auto producer = channel.producer(std::move(producer_perm));
    ::fixy::concurrent::push_waiting<::fixy::concurrent::SpinThenPark<1000>>(
        ::foundation::effects::testing::foreground(), producer, room, 1);
    return 0;
}
