// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A graph of two stages with one edge.  The source stage writes channel
// A, and the sink stage drains channel B.  Both channels carry int, so
// the edge agrees in payload, and no value crosses it.  An edge asks that
// the producer at its source and the consumer at its target name the same
// channel, so the graph is not well formed.  No context and no stage is
// built from nothing here, so the mint takes them as parameters.

#include <fixy/Ctx.h>
#include <fixy/concurrent/PermissionedSpscChannel.h>
#include <fixy/concurrent/Pipeline.h>

#include <utility>

namespace {

namespace cc = fixy::concurrent;

// The fixture builds no channel, so one named brand stands in for the
// brand of a root site.  The tags keep the four channels apart.
struct InTag {};
struct ATag {};
struct BTag {};
struct OutTag {};
struct FixtureBrand {};
using InChannel = cc::PermissionedSpscChannel<int, 8, InTag, FixtureBrand>;
using AChannel = cc::PermissionedSpscChannel<int, 8, ATag, FixtureBrand>;
using BChannel = cc::PermissionedSpscChannel<int, 8, BTag, FixtureBrand>;
using OutChannel = cc::PermissionedSpscChannel<int, 8, OutTag, FixtureBrand>;

inline void writes_a(InChannel::ConsumerHandle&&, AChannel::ProducerHandle&&) noexcept {}
inline void drains_b(BChannel::ConsumerHandle&&, OutChannel::ProducerHandle&&) noexcept {}

using WritesA = cc::Stage<&writes_a, fixy::HotFgCtx>;
using DrainsB = cc::Stage<&drains_b, fixy::HotFgCtx>;
using CrossedGraph = cc::StageGraph<cc::StagePack<WritesA, DrainsB>, cc::EdgePack<cc::StageEdge<0, 1>>>;

[[maybe_unused]] void mint_across(fixy::BgDrainCtx const& coordinator, WritesA&& source, DrainsB&& sink) {
    auto bad = cc::mint_pipeline_dag(coordinator, CrossedGraph{}, std::move(source), std::move(sink));
    (void)bad;
}

}  // namespace

int main() { return 0; }
