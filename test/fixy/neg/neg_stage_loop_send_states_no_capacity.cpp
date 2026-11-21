// The flush arm of a loop in the shape of the loop of the Vigil, in which the
// build stage sends into edge 3 through the type of the channel, which states
// no capacity, and the last stage receives through a handle of eight values.
// The door of a loop refuses an end that states no capacity, so no capacity
// of a channel comes from one of its ends alone.  The same loop with the
// handle of eight values on the two ends is FlushArm of stage_loop_fakes.h,
// which the door of test_pipeline_dag.cpp admits.

#include <fixy/concurrent/StageNet.h>

#include "../stage_loop_fakes.h"

namespace neg_stage_loop_send_states_no_capacity_types {

namespace cc = ::fixy::concurrent;
namespace loop = ::stage_loop_fakes;

using UnstatedSend =
    cc::StageLoop<loop::ForegroundFlush, loop::DrainProcess, loop::DetectProcess,
                  cc::LoopProcess<loop::BuildName, cc::InputsOf<loop::BuildStage>, cc::SendsOn<loop::EdgeChannel<3>>>,
                  loop::PublishFlush>;

constexpr int require_live() noexcept {
    cc::ensure_loop_live<UnstatedSend>();
    return 0;
}

}  // namespace neg_stage_loop_send_states_no_capacity_types

using namespace neg_stage_loop_send_states_no_capacity_types;

int main() { return require_live(); }
