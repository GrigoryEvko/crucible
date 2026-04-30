// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D19 fixture — pins the constrained-extractor discipline.
// pipeline_stage_output_value_t is constrained on
// `requires PipelineStage<FnPtr>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/PipelineStage.h>

inline void neg_witness_two_ints(int, int) noexcept {}

int main() {
    using V = crucible::safety::extract::pipeline_stage_output_value_t<
        &::neg_witness_two_ints>;
    V const v{};
    (void)v;
    return 0;
}
