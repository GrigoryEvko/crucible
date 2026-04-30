// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D19 fixture — pins the constrained-extractor discipline on
// PipelineStage.h.  pipeline_stage_input_value_t is constrained on
// `requires PipelineStage<FnPtr>`; instantiating it on a non-
// pipeline-shape function pointer must fail at the requires clause,
// NOT chain through consumer_handle_value_t.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/PipelineStage.h>

inline void neg_witness_two_ints(int, int) noexcept {}

int main() {
    using V = crucible::safety::extract::pipeline_stage_input_value_t<
        &::neg_witness_two_ints>;
    V const v{};
    (void)v;
    return 0;
}
