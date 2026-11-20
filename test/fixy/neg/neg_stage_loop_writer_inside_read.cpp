// The flush arm of a loop in the shape of the loop of the Vigil, in which the
// last stage writes a second lock inside its read of the holders.  A read of
// a sequence lock that meets a write runs again, and then the write inside
// it runs two times.  The door of a loop refuses a bracket of a writer inside
// a read.  The same loop with the write after the read is admitted in
// test_pipeline_dag.cpp.

#include <fixy/concurrent/StageNet.h>

#include "../stage_loop_fakes.h"

namespace neg_stage_loop_writer_inside_read_types {

namespace cc = ::fixy::concurrent;
namespace loop = ::stage_loop_fakes;

// The marks that the last stage writes, and that the foreground reads.
struct Marks {};

using WriteInRead =
    cc::StageLoop<cc::LoopProcess<loop::ForegroundName, cc::SendsOn<loop::Ring>, cc::WritesLock<loop::Holders>,
                                  cc::ReadsLock<Marks>, cc::ReceivesOn<loop::Count>>,
                  loop::DrainProcess, loop::DetectProcess, loop::BuildProcess,
                  cc::LoopProcess<loop::PublishName, cc::InputsOf<loop::PublishStage>,
                                  cc::ReadsLock<loop::Holders, cc::WritesLock<Marks>>, cc::SendsOn<loop::Count>>>;

constexpr int require_live() noexcept {
    cc::ensure_loop_live<WriteInRead>();
    return 0;
}

}  // namespace neg_stage_loop_writer_inside_read_types

using namespace neg_stage_loop_writer_inside_read_types;

int main() { return require_live(); }
