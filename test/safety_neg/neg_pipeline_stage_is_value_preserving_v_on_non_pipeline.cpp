// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D19 fixture — pins the constrained-extractor discipline.
// pipeline_stage_is_value_preserving_v is constrained on
// `requires PipelineStage<FnPtr>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/PipelineStage.h>

inline void neg_witness_two_ints(int, int) noexcept {}

int main() {
    bool b = crucible::safety::extract::pipeline_stage_is_value_preserving_v<
        &::neg_witness_two_ints>;
    (void)b;
    return 0;
}
