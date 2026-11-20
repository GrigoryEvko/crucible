// The job arm of a loop in the shape of the loop of the Vigil, with one end
// wired to the wrong mailbox: the last stage serves another mailbox than the
// mailbox that the foreground posts into.  The mailbox of the foreground then
// has a send and no receive, so the door refuses the loop.  The same loop
// with the right mailbox is JobArm of stage_loop_fakes.h, which the door of
// test_pipeline_dag.cpp admits.

#include <fixy/concurrent/StageNet.h>

#include "../stage_loop_fakes.h"

namespace neg_stage_loop_wrong_mailbox_types {

namespace cc = ::fixy::concurrent;
namespace loop = ::stage_loop_fakes;

using WrongMailbox = cc::StageLoop<
    loop::ForegroundJob, loop::DrainProcess, loop::DetectProcess, loop::BuildProcess,
    cc::LoopProcess<loop::PublishName, cc::ReceivesOn<loop::OtherMailbox>, cc::SendsOn<loop::JobDone>,
                    cc::InputsOf<loop::PublishStage>, cc::ReadsLock<loop::Holders>, cc::SendsOn<loop::Count>>>;

constexpr int require_live() noexcept {
    cc::ensure_loop_live<WrongMailbox>();
    return 0;
}

}  // namespace neg_stage_loop_wrong_mailbox_types

using namespace neg_stage_loop_wrong_mailbox_types;

int main() { return require_live(); }
