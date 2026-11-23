// Negative compile test.  This file must fail to compile.
//
// The two outputs of a fan-out stage each have one edge, and the two edges
// end at input 0 of one plain stage.  Two edges on one input give one
// channel two producers, so the port check refuses the graph.  A fan-in
// stage with two inputs is the correct shape.

#include <fixy/Ctx.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <fixy/concurrent/Pipeline.h>

namespace input_two_edges_fixture {
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
inline void fan_out(Link::ConsumerEnd&&, Link::ProducerEnd&&, Link::ProducerEnd&&) noexcept {}
using Plain = cc::Stage<&plain, fixy::HotFgCtx>;
using FanOut = cc::MpmcStage<&fan_out, fixy::HotFgCtx>;
using SharedInput =
    cc::StageGraph<cc::StagePack<FanOut, Plain>, cc::EdgePack<cc::StageEdge<0, 1, 0, 0>, cc::StageEdge<0, 1, 1, 0>>>;

// The size is the one use of the graph, so the refusal is the only error.
[[maybe_unused]] constexpr auto bad_dag_size = sizeof(cc::PipelineDag<SharedInput>);
}  // namespace input_two_edges_fixture

int main() { return 0; }
