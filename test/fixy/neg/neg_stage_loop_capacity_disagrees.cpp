// The flush arm of a loop in the shape of the loop of the Vigil, in which the
// two ends of edge 3 state two capacities: the build stage sends through a
// handle of eight values, and the last stage receives through a handle of
// four.  The door of a loop refuses two ends of one channel that state two
// capacities.  The same loop with the handle of eight values on the two ends
// is FlushArm of stage_loop_fakes.h, which the door of test_pipeline_dag.cpp
// admits.

#include <fixy/concurrent/StageNet.h>

#include "../stage_loop_fakes.h"

namespace neg_stage_loop_capacity_disagrees_types {

namespace cc = ::fixy::concurrent;
namespace loop = ::stage_loop_fakes;

using NarrowReceive = cc::StageLoop<loop::ForegroundFlush, loop::DrainProcess, loop::DetectProcess, loop::BuildProcess,
                                    cc::LoopProcess<loop::PublishName, cc::InputsOf<loop::NarrowPublishStage>,
                                                    cc::ReadsLock<loop::Holders>, cc::SendsOn<loop::Count>>>;

constexpr int require_live() noexcept {
    cc::ensure_loop_live<NarrowReceive>();
    return 0;
}

}  // namespace neg_stage_loop_capacity_disagrees_types

using namespace neg_stage_loop_capacity_disagrees_types;

int main() { return require_live(); }
