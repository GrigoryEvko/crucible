// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The two outputs of a fan-out stage each have one edge, and the two edges
// end at input 0 of one plain stage.  Two edges on one input give one
// channel two producers, so the port check refuses the graph.  A fan-in
// stage with two inputs is the correct shape.

#include <fixy/Ctx.h>
#include <fixy/concurrent/PermissionedSpscChannel.h>
#include <fixy/concurrent/Pipeline.h>

namespace input_two_edges_fixture {
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
using SharedInput =
    cc::StageGraph<cc::StagePack<FanOut, Plain>, cc::EdgePack<cc::StageEdge<0, 1, 0, 0>, cc::StageEdge<0, 1, 1, 0>>>;

// The size is the one use of the graph, so the refusal is the only error.
[[maybe_unused]] constexpr auto bad_dag_size = sizeof(cc::PipelineDag<SharedInput>);
}  // namespace input_two_edges_fixture

int main() { return 0; }
