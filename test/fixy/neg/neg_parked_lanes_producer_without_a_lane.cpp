// A producer of a parked lanes channel parks on the count of room of its lane,
// so the producer door names the lane.  A door with no lane, as the door of a
// merge, does not compile.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/ParkedChannel.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <cstdint>
#include <utility>

struct LanesTag {};

int main() {
    namespace eff = ::foundation::effects;
    namespace cc = ::fixy::concurrent;
    auto root =
        ::foundation::permissions::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::lanes, LanesTag>>();
    using Parked = cc::ParkedChannel<cc::channel_t<std::uint64_t, 2, decltype(root), cc::LaneCount{2}>>;
    auto tokens =
        ::foundation::permissions::mint_permission_split<Parked::producer_tag, Parked::consumer_tag>(std::move(root));
    Parked channel{std::move(tokens.at(::foundation::core::index<0>()))};
    ::fixy::BgLoadCtx const ctx{eff::testing::bg()};
    static_cast<void>(cc::with_producer(channel, eff::extract_bare_from_ctx<eff::Effect::Nondet>(ctx),
                                        [&ctx](auto producer) noexcept -> void { std::move(producer).close(ctx); }));
    return 0;
}
