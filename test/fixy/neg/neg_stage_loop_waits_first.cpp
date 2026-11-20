// The flush arm of a loop in the shape of the loop of the Vigil, with the
// ends of the foreground in the wrong order: the foreground waits on the count
// before it records.  The count waits on the last stage, which waits on the
// ops of the record, so the cycle through the five processes holds no token,
// and the door refuses the loop.  The same loop with the record first is
// FlushArm of stage_loop_fakes.h, which the door of test_pipeline_dag.cpp
// admits.

#include <fixy/concurrent/StageNet.h>

#include "../stage_loop_fakes.h"

namespace neg_stage_loop_waits_first_types {

namespace cc = ::fixy::concurrent;
namespace loop = ::stage_loop_fakes;

using WaitsFirst = cc::StageLoop<cc::LoopProcess<loop::ForegroundName, cc::ReceivesOn<loop::Count>,
                                                 cc::SendsOn<loop::Ring>, cc::WritesLock<loop::Holders>>,
                                 loop::DrainProcess, loop::DetectProcess, loop::BuildProcess, loop::PublishFlush>;

constexpr int require_live() noexcept {
    cc::ensure_loop_live<WaitsFirst>();
    return 0;
}

}  // namespace neg_stage_loop_waits_first_types

using namespace neg_stage_loop_waits_first_types;

int main() { return require_live(); }
