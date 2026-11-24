// A parked handle owes a consume at its end, so a body that holds one is no
// body that end_unrun_body of foundation/Linear.h can end.  The door would
// end the handle with no notify, and a consumer that parks on items would
// never wake.  The deleted twin of the door refuses the body.

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
    ::fixy::concurrent::with_producer(
        channel, std::move(tokens.at(::foundation::core::index<0>())), [](auto producer) noexcept {
            auto body = [handle = std::move(producer)]() mutable noexcept { return std::move(handle).release(); };
            ::foundation::linear::end_unrun_body(std::move(body), ::foundation::linear::UnrunReason::refusal_test);
        });
    return 0;
}
