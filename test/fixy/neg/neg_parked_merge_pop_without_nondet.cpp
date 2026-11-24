// A pop of a merge reads the schedule of its producers, so each pop door of a
// parked merge takes a context that owns Nondet (pops_with).  The context
// here owns Block, so it can park, and it owns no Nondet.  The pop door of the
// merge refuses it.  The same context pops a parked wire.

#include <fixy/Ctx.h>
#include <fixy/concurrent/ParkedChannel.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <cstdint>
#include <utility>

struct MergeTag {};

int main() {
    namespace eff = ::foundation::effects;
    namespace cc = ::fixy::concurrent;
    auto root =
        ::foundation::permissions::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::merge, MergeTag>>();
    using Parked = cc::ParkedChannel<cc::channel_t<std::uint64_t, 2, decltype(root)>>;
    auto tokens =
        ::foundation::permissions::mint_permission_split<Parked::producer_tag, Parked::consumer_tag>(std::move(root));
    Parked channel{std::move(tokens.at(::foundation::core::index<0>()))};
    ::fixy::BgLoadCtx const ctx{eff::testing::bg()};
    auto const parks = ctx.in_row<eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>();
    cc::with_consumer(channel, std::move(tokens.at(::foundation::core::index<1>())),
                      [&ctx, &parks](auto consumer) noexcept {
                          static_cast<void>(consumer.try_pop(parks));
                          std::move(consumer).close(ctx);
                      });
    return 0;
}
