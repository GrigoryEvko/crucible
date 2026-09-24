// pipeline_stage_is_value_preserving_v compares a stage's input element
// type with its output element type.  Both come from the constrained
// aliases, so the variable is constrained on PipelineStage too.  Were it
// not, it would compare two types recovered from a function that has
// neither, and answer true for a nullary function because void equals
// void.
//
// VIOLATION: a TU asks a nullary function whether it preserves its value.
//
// Expected diagnostic: the PipelineStage constraint on
// pipeline_stage_is_value_preserving_v fails.

#include <fixy/concurrent/StageShape.h>

namespace {
void nullary_body() noexcept {}
}  // namespace

int main() {
    return ::fixy::concurrent::pipeline_stage_is_value_preserving_v<&nullary_body> ? 1 : 0;
}
