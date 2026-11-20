// Two arms of a loop in the shape of the loop of the Vigil.  Each arm alone is
// live, but the foreground writes the holders in the flush arm, and the last
// stage writes them in the swapped arm (stage_loop_fakes.h).  The door over
// the arms refuses two arms that name two writers of one lock.  The three
// arms of the loop of the Vigil agree, and the door of test_pipeline_dag.cpp
// admits them.

#include <fixy/concurrent/StageNet.h>

#include "../stage_loop_fakes.h"

namespace neg_stage_loop_arms_disagree_on_writer_types {

namespace cc = ::fixy::concurrent;
namespace loop = ::stage_loop_fakes;

constexpr int require_live() noexcept {
    cc::ensure_arms_live<loop::FlushArm, loop::SwappedLockArm>();
    return 0;
}

}  // namespace neg_stage_loop_arms_disagree_on_writer_types

using namespace neg_stage_loop_arms_disagree_on_writer_types;

int main() { return require_live(); }
