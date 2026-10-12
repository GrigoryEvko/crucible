// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The output of stage 0 has two edges, to stage 1 and to input 0 of the
// fan-in stage 2.  A port is one end of one channel, so two edges on one
// output give one channel two consumers, and the port check refuses the
// graph.  A fan-out stage with two outputs is the correct shape.

#include <fixy/Ctx.h>
#include <fixy/concurrent/PermissionedSpscChannel.h>
#include <fixy/concurrent/Pipeline.h>

namespace output_two_edges_fixture {
namespace cc = ::fixy::concurrent;

// The fixture builds no channel, so one named brand stands in for the
// brand of a root site.  Each handle names one channel, so each edge of
// the graph connects two handles of one channel.
struct LinkTag {};
struct FixtureBrand {};
using Link = cc::PermissionedSpscChannel<int, 8, LinkTag, FixtureBrand>;
inline void plain(Link::ConsumerHandle&&, Link::ProducerHandle&&) noexcept {}
inline void fan_in(Link::ConsumerHandle&&, Link::ConsumerHandle&&, Link::ProducerHandle&&) noexcept {}
using Plain = cc::Stage<&plain, fixy::HotFgCtx>;
using FanIn = cc::MpmcStage<&fan_in, fixy::HotFgCtx>;
using SharedOutput =
    cc::StageGraph<cc::StagePack<Plain, Plain, FanIn>,
                   cc::EdgePack<cc::StageEdge<0, 1, 0, 0>, cc::StageEdge<0, 2, 0, 0>, cc::StageEdge<1, 2, 0, 1>>>;

// The size is the one use of the graph, so the refusal is the only error.
[[maybe_unused]] constexpr auto bad_dag_size = sizeof(cc::PipelineDag<SharedOutput>);
}  // namespace output_two_edges_fixture

int main() { return 0; }
