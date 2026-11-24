// A refused push of a Box through a parked wire gives the Box back in its
// refusal, and the Result of the push is a linear sum.  A caller that drops
// the Result would drop the Box of a refusal in silence, so the rule
// must-consume refuses the dropped Result.

#include <fixy/Ctx.h>
#include <fixy/concurrent/ParkedChannel.h>
#include <fixy/concurrent/PermissionedBoxChannel.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

struct ParcelTag {};

int main() {
    auto root = ::foundation::permissions::mint_permission_root<
        ::fixy::concurrent::channel_tag::Whole<::fixy::concurrent::ChannelForm::wire, ParcelTag>>();
    using Parked = ::fixy::concurrent::ParkedChannel<::fixy::concurrent::box_channel_t<int, 2, decltype(root)>>;
    Parked channel{};
    ::fixy::BgLoadCtx const ctx{::foundation::effects::testing::bg()};
    auto tokens =
        ::foundation::permissions::mint_permission_split<Parked::producer_tag, Parked::consumer_tag>(std::move(root));
    auto token = ::fixy::concurrent::with_producer(
        channel, std::move(tokens.at(::foundation::core::index<0>())), [&ctx](auto producer) noexcept {
            producer.try_push(ctx, ::foundation::core::mint_box<int>(::foundation::effects::testing::test().alloc, 1));
            return std::move(producer).release();
        });
    return 0;
}
