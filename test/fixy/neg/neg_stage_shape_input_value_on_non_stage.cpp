// pipeline_stage_input_value_t recovers the element type a stage's
// consumer end drains.  It is constrained on PipelineStage, so asking it
// about a function that is not a stage is refused at the alias rather
// than answering with whatever the handle traits made of a plain int.
//
// VIOLATION: a TU asks a two-int function for its stage input value.
//
// Expected diagnostic: the PipelineStage constraint on
// pipeline_stage_input_value_t fails.

#include <fixy/concurrent/StageShape.h>

namespace {

void not_a_stage(int, int) noexcept {}

// The requirement is the one use of the alias.  An alias declaration
// gives a second error at the declaration, and a second use gives a
// second error at the use.  A fixture that rejects at two places cannot
// say which rejection its regexes witnessed.
static_assert(requires { typename ::fixy::concurrent::pipeline_stage_input_value_t<&not_a_stage>; });

}  // namespace

int main() { return 0; }
