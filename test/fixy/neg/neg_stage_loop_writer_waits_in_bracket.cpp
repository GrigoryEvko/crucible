// The flush arm of a loop in the shape of the loop of the Vigil, with a wait
// inside a bracket: the foreground waits on the count before it leaves its
// bracket of the holders.  The last stage reads the holders before it sends
// the count, and the read waits while the bracket is open, so each of the two
// waits on the other.  A lock adds no action to the marked system, so only the
// rule of the brackets refuses the loop.  The same loop with the wait after
// the bracket is FlushArm of stage_loop_fakes.h, which the door of
// test_pipeline_dag.cpp admits.

#include <fixy/concurrent/StageNet.h>

#include "../stage_loop_fakes.h"

namespace neg_stage_loop_writer_waits_in_bracket_types {

namespace cc = ::fixy::concurrent;
namespace loop = ::stage_loop_fakes;

using WriterWaitsInBracket =
    cc::StageLoop<cc::LoopProcess<loop::ForegroundName, cc::SendsOn<loop::Ring>,
                                  cc::WritesLock<loop::Holders, cc::ReceivesOn<loop::Count>>>,
                  loop::DrainProcess, loop::DetectProcess, loop::BuildProcess, loop::PublishFlush>;

constexpr int require_live() noexcept {
    cc::ensure_loop_live<WriterWaitsInBracket>();
    return 0;
}

}  // namespace neg_stage_loop_writer_waits_in_bracket_types

using namespace neg_stage_loop_writer_waits_in_bracket_types;

int main() { return require_live(); }
