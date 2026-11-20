// The job arm of a loop in the shape of the loop of the Vigil, with a lost
// end: the foreground posts a job and does not wait on its done mark.  Each
// period then leaves a done mark in its cell, so the period does not give back
// the marking where it starts, and the done mark of the next job waits for a
// free cell for ever.  The door refuses the loop.  The same loop with the wait
// on the done mark is JobArm of stage_loop_fakes.h, which the door of
// test_pipeline_dag.cpp admits.

#include <fixy/concurrent/StageNet.h>

#include "../stage_loop_fakes.h"

namespace neg_stage_loop_token_left_behind_types {

namespace cc = ::fixy::concurrent;
namespace loop = ::stage_loop_fakes;

using TokenLeftBehind =
    cc::StageLoop<cc::LoopProcess<loop::ForegroundName, cc::SendsOn<loop::Ring>, cc::SendsOn<loop::Mailbox>,
                                  cc::WritesLock<loop::Holders>, cc::ReceivesOn<loop::Count>>,
                  loop::DrainProcess, loop::DetectProcess, loop::BuildProcess, loop::PublishJob>;

constexpr int require_live() noexcept {
    cc::ensure_loop_live<TokenLeftBehind>();
    return 0;
}

}  // namespace neg_stage_loop_token_left_behind_types

using namespace neg_stage_loop_token_left_behind_types;

int main() { return require_live(); }
