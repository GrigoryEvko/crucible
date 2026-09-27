// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The first stage writes channel A, and the second stage drains channel B.
// The two channels carry int, so the payloads agree, and no value goes from
// one stage to the other: the second stage waits on B forever.  Each handle
// names its channel, and the chain asks that the producer of one stage and
// the consumer of the next name the same channel.  No context and no stage
// is built from nothing here, so the mint takes them as parameters.

#include <fixy/Ctx.h>
#include <fixy/concurrent/PermissionedSpscChannel.h>
#include <fixy/concurrent/Pipeline.h>

#include <utility>

namespace {

namespace cc = fixy::concurrent;

struct InTag {};
struct ATag {};
struct BTag {};
struct OutTag {};
using InChannel = cc::PermissionedSpscChannel<int, 8, InTag>;
using AChannel = cc::PermissionedSpscChannel<int, 8, ATag>;
using BChannel = cc::PermissionedSpscChannel<int, 8, BTag>;
using OutChannel = cc::PermissionedSpscChannel<int, 8, OutTag>;

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
