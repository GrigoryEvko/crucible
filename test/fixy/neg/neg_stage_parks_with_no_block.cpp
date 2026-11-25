// Negative compile test.  This file must fail to compile.
//
// A parked handle parks through the context of its stage, and its body has no
// other context.  Here the context of the stage owns no Block, so the wait of
// the body does not compile.
//
// Expected diagnostic: the context does not fit the wait policy.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/ParkedChannel.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <fixy/concurrent/Stage.h>
#include <foundation/Linear.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <utility>

namespace neg_stage_parks_with_no_block_types {

namespace cc = ::fixy::concurrent;

struct WireTag {};

struct Sink {
    using channel_type = WireTag;
    static constexpr std::size_t channel_capacity = 0;

    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

template <class Input, class Ctx>
void drain(Input&& in, Sink&& /*out*/, Ctx const& ctx) noexcept {
    while (in.template receive_waiting<cc::SpinThenPark<0>>(ctx).is_ok()) {}
    ::foundation::linear::drop(std::move(in).release());
}

}  // namespace neg_stage_parks_with_no_block_types

int main() {
    using namespace neg_stage_parks_with_no_block_types;
    ::fixy::BgDrainCtx const ctx{::foundation::effects::testing::bg()};
    auto root =
        ::foundation::permissions::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::wire, WireTag>>();
    using Parked = cc::ParkedChannel<cc::channel_t<int, 8, decltype(root)>>;
    Parked channel{};
    auto [producer_perm, consumer_perm] =
        ::foundation::permissions::mint_permission_split<Parked::producer_tag, Parked::consumer_tag>(std::move(root));
    cc::with_consumer(channel, std::move(consumer_perm), [&ctx](auto consumer) noexcept {
        auto stage = cc::mint_stage<&drain<decltype(consumer), ::fixy::BgDrainCtx>>(ctx, std::move(consumer), Sink{});
        std::move(stage).run();
    });
    ::foundation::linear::drop(std::move(producer_perm));
    return 0;
}
