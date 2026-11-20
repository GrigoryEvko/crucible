// Two arms of a loop in the shape of the loop of the Vigil.  Each arm alone is
// live, but edge 3 holds eight values in the flush arm and four in the narrow
// flush arm (stage_loop_fakes.h).  The door over the arms refuses two arms
// that state two capacities of one channel.  The three arms of the loop of
// the Vigil agree, and the door of test_pipeline_dag.cpp admits them.

#include <fixy/concurrent/StageNet.h>

#include "../stage_loop_fakes.h"

namespace neg_stage_loop_arms_disagree_on_capacity_types {

namespace cc = ::fixy::concurrent;
namespace loop = ::stage_loop_fakes;

constexpr int require_live() noexcept {
    cc::ensure_arms_live<loop::FlushArm, loop::NarrowFlushArm>();
    return 0;
}

}  // namespace neg_stage_loop_arms_disagree_on_capacity_types

using namespace neg_stage_loop_arms_disagree_on_capacity_types;

int main() { return require_live(); }
