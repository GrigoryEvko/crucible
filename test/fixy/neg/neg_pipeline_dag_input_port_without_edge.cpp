// Negative compile test.  This file must fail to compile.
//
// A plain stage feeds input 0 of a fan-in stage, and input 1 of the fan-in
// stage has no edge.  The fan-in stage is not the first stage, so its
// inputs are not the inputs of the graph, and the port check refuses the
// graph.  An input with no edge is a channel that no stage fills.

#include <fixy/Ctx.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <fixy/concurrent/Pipeline.h>

namespace input_without_edge_fixture {
namespace cc = ::fixy::concurrent;

// The fixture builds no channel, so one named brand stands in for the
// brand of a root site.  The graph is a fact of the types, so each body
// takes the ends of its channels, and no frame door opens a handle.  Each
// end names one channel, so each edge of the graph connects two ends of
// one channel.
struct LinkTag {};
struct FixtureBrand {};
using Link = cc::PermissionedChannel<cc::ChannelForm::wire, int, 8, LinkTag, FixtureBrand>;
inline void plain(Link::ConsumerEnd&&, Link::ProducerEnd&&) noexcept {}
inline void fan_in(Link::ConsumerEnd&&, Link::ConsumerEnd&&, Link::ProducerEnd&&) noexcept {}
using Plain = cc::Stage<&plain, fixy::HotFgCtx>;
using FanIn = cc::MpmcStage<&fan_in, fixy::HotFgCtx>;
using HalfWired = cc::StageGraph<cc::StagePack<Plain, FanIn>, cc::EdgePack<cc::StageEdge<0, 1, 0, 0>>>;

// The size is the one use of the graph, so the refusal is the only error.
[[maybe_unused]] constexpr auto bad_dag_size = sizeof(cc::PipelineDag<HalfWired>);
}  // namespace input_without_edge_fixture

int main() { return 0; }
