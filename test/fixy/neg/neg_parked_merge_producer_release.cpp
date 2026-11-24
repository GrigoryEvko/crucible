// A producer of a merge holds a share of the pool of the producer root, and
// no token, so a parked producer of a merge has close and no release.  The
// same body with close compiles.

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
    static_cast<void>(
        cc::with_producer(channel, eff::extract_bare_from_ctx<eff::Effect::Nondet>(ctx),
                          [](auto producer) noexcept -> void { static_cast<void>(std::move(producer).release()); }));
    return 0;
}
