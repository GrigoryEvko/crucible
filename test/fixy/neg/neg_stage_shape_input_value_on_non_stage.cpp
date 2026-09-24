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

// The alias is declared and never used.  Naming it in an expression
// would fail a second time, at the use, and a fixture that rejects at
// two of its own lines cannot say which rejection its regexes witnessed.
using forged = ::fixy::concurrent::pipeline_stage_input_value_t<&not_a_stage>;

}  // namespace

int main() { return 0; }
