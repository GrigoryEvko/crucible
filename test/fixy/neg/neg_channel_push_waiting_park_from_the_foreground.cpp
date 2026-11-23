// A context of the hot path owns no Block, so the push that waits of a
// producer handle refuses it a wait policy that parks (the policy of
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
    auto tokens =
        ::foundation::permissions::mint_permission_split<Wire::producer_tag, Wire::consumer_tag>(std::move(root));
    auto token = ::fixy::concurrent::with_producer(
        channel, std::move(tokens.at(::foundation::core::index<0>())), [&room](auto producer) noexcept {
            ::fixy::concurrent::push_waiting<::fixy::concurrent::SpinThenPark<1000>>(
                ::foundation::effects::testing::foreground(), producer, room, 1);
            return std::move(producer).release();
        });
    return 0;
}
