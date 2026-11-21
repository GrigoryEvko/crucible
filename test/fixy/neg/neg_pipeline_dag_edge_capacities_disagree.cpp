// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A graph of two stages with one edge.  The build stage sends into edge 3
// through a handle of eight values, and the last stage receives from it
// through a handle of four.  The two handles name one channel and carry one
// payload, so only the capacity tells them apart.  An edge asks that its two
// handles state the same capacity, so the graph is not well formed.  The
// same graph with the handle of eight values on the two ends is a part of
// FakeGraph of stage_loop_fakes.h.

#include <fixy/Ctx.h>
#include <fixy/concurrent/Pipeline.h>

#include <utility>

#include "../stage_loop_fakes.h"

namespace {

namespace cc = fixy::concurrent;
namespace loop = ::stage_loop_fakes;

using NarrowEdgeGraph =
    cc::StageGraph<cc::StagePack<loop::BuildStage, loop::NarrowPublishStage>, cc::EdgePack<cc::StageEdge<0, 1>>>;

[[maybe_unused]] void join_narrow_edge(fixy::BgDrainCtx const& coordinator, loop::BuildStage&& build,
                                       loop::NarrowPublishStage&& publish) {
    auto bad = cc::mint_pipeline_dag(coordinator, NarrowEdgeGraph{}, std::move(build), std::move(publish));
    (void)bad;
}

}  // namespace

int main() { return 0; }
