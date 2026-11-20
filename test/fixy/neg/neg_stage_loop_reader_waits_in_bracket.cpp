// The flush arm of a loop in the shape of the loop of the Vigil, with a wait
// inside a read: the last stage receives its input before it leaves its read
// of the holders.  A read that meets a write of the foreground runs again, and
// the receive inside the read runs again with it, so one read can take two
// values from the channel.  The door refuses the loop.  The same loop with the
// receive before the read is FlushArm of stage_loop_fakes.h, which the door
// of test_pipeline_dag.cpp admits.

#include <fixy/concurrent/StageNet.h>

#include "../stage_loop_fakes.h"

namespace neg_stage_loop_reader_waits_in_bracket_types {

namespace cc = ::fixy::concurrent;
namespace loop = ::stage_loop_fakes;

using ReaderWaitsInBracket =
    cc::StageLoop<loop::ForegroundFlush, loop::DrainProcess, loop::DetectProcess, loop::BuildProcess,
                  cc::LoopProcess<loop::PublishName, cc::ReadsLock<loop::Holders, cc::InputsOf<loop::PublishStage>>,
                                  cc::SendsOn<loop::Count>>>;

constexpr int require_live() noexcept {
    cc::ensure_loop_live<ReaderWaitsInBracket>();
    return 0;
}

}  // namespace neg_stage_loop_reader_waits_in_bracket_types

using namespace neg_stage_loop_reader_waits_in_bracket_types;

int main() { return require_live(); }
