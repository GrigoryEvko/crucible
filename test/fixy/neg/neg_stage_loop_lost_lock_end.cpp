// The flush arm of a loop in the shape of the loop of the Vigil, with a lost
// end: the last stage does not read the lock of the holders that the
// foreground writes.  The lock then has a writer and no reader, so the door
// refuses the loop before it checks the waits.  The same loop with the read
// of the lock is FlushArm of stage_loop_fakes.h, which the door of
// test_pipeline_dag.cpp admits.

#include <fixy/concurrent/StageNet.h>

#include "../stage_loop_fakes.h"

namespace neg_stage_loop_lost_lock_end_types {

namespace cc = ::fixy::concurrent;
namespace loop = ::stage_loop_fakes;

using LostLockEnd =
    cc::StageLoop<loop::ForegroundFlush, loop::DrainProcess, loop::DetectProcess, loop::BuildProcess,
                  cc::LoopProcess<loop::PublishName, cc::InputsOf<loop::PublishStage>, cc::SendsOn<loop::Count>>>;

constexpr int require_live() noexcept {
    cc::ensure_loop_live<LostLockEnd>();
    return 0;
}

}  // namespace neg_stage_loop_lost_lock_end_types

using namespace neg_stage_loop_lost_lock_end_types;

int main() { return require_live(); }
