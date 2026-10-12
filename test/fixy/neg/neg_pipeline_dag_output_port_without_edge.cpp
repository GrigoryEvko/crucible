// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Output 0 of a fan-out stage feeds a plain stage, and output 1 has no
// edge.  The fan-out stage is not the last stage, so its outputs are not
// the outputs of the graph, and the port check refuses the graph.  An
// output with no edge is a channel that no stage drains.

#include <fixy/Ctx.h>
#include <fixy/concurrent/PermissionedSpscChannel.h>
#include <fixy/concurrent/Pipeline.h>

namespace output_without_edge_fixture {
namespace cc = ::fixy::concurrent;

// The fixture builds no channel, so one named brand stands in for the
// brand of a root site.  Each handle names one channel, so each edge of
// the graph connects two handles of one channel.
struct LinkTag {};
struct FixtureBrand {};
using Link = cc::PermissionedSpscChannel<int, 8, LinkTag, FixtureBrand>;
inline void plain(Link::ConsumerHandle&&, Link::ProducerHandle&&) noexcept {}
inline void fan_out(Link::ConsumerHandle&&, Link::ProducerHandle&&, Link::ProducerHandle&&) noexcept {}
using Plain = cc::Stage<&plain, fixy::HotFgCtx>;
using FanOut = cc::MpmcStage<&fan_out, fixy::HotFgCtx>;
using HalfDrained = cc::StageGraph<cc::StagePack<FanOut, Plain>, cc::EdgePack<cc::StageEdge<0, 1, 0, 0>>>;

// The size is the one use of the graph, so the refusal is the only error.
[[maybe_unused]] constexpr auto bad_dag_size = sizeof(cc::PipelineDag<HalfDrained>);
}  // namespace output_without_edge_fixture

int main() { return 0; }
