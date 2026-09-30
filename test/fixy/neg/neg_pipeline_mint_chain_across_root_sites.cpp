// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Channels A and B have one payload, one capacity and one user tag.  Their
// roots come from two call sites, so the two channels have two brands and
// are two types.  The first stage writes channel A, and the second stage
// drains channel B, so no value goes from one stage to the other.  The
// chain asks that the producer of one stage and the consumer of the next
// name the same channel, and the two brands refuse the chain.

#include <fixy/Ctx.h>
#include <fixy/concurrent/PermissionedSpscChannel.h>
#include <fixy/concurrent/Pipeline.h>

#include <foundation/permissions/Permission.h>

#include <utility>

namespace {

namespace cc = fixy::concurrent;
namespace perm = foundation::permissions;

struct InTag {};
struct LinkTag {};
struct OutTag {};

inline auto in_root() noexcept { return perm::mint_permission_root<cc::spsc_tag::Whole<InTag>>(); }
inline auto a_root() noexcept { return perm::mint_permission_root<cc::spsc_tag::Whole<LinkTag>>(); }
inline auto b_root() noexcept { return perm::mint_permission_root<cc::spsc_tag::Whole<LinkTag>>(); }
inline auto out_root() noexcept { return perm::mint_permission_root<cc::spsc_tag::Whole<OutTag>>(); }

using InChannel = cc::spsc_channel_t<int, 8, decltype(in_root())>;
using AChannel = cc::spsc_channel_t<int, 8, decltype(a_root())>;
using BChannel = cc::spsc_channel_t<int, 8, decltype(b_root())>;
using OutChannel = cc::spsc_channel_t<int, 8, decltype(out_root())>;

inline void writes_a(InChannel::ConsumerHandle&&, AChannel::ProducerHandle&&) noexcept {}
inline void drains_b(BChannel::ConsumerHandle&&, OutChannel::ProducerHandle&&) noexcept {}

using WritesA = cc::Stage<&writes_a, fixy::HotFgCtx>;
using DrainsB = cc::Stage<&drains_b, fixy::HotFgCtx>;

[[maybe_unused]] void mint_across(fixy::BgDrainCtx const& coordinator, WritesA&& first, DrainsB&& second) {
    auto bad = cc::mint_pipeline(coordinator, std::move(first), std::move(second));
    (void)bad;
}

}  // namespace

int main() { return 0; }
