// pipeline_stage_output_value_t recovers the element type a stage's
// producer end fills.  Same gate as the input alias, and both are
// required: the output alias indexes from the input count, so a stage
// shape that admitted a non-stage would read its output from an offset
// computed out of a count that means nothing.
//
// Sibling of neg_stage_shape_input_value_on_non_stage.cpp.
//
// VIOLATION: a TU asks a function whose parameters are a producer then a
// consumer — the data-flow order reversed — for its stage output value.
//
// Expected diagnostic: the PipelineStage constraint on
// pipeline_stage_output_value_t fails.

#include <fixy/concurrent/StageShape.h>

#include <optional>

namespace {

template <typename T>
struct fake_consumer {
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct fake_producer {
    [[nodiscard]] bool try_push(T const&) noexcept { return true; }
};

// Both parameters are real handles of the right kinds, and each is a
// non-const rvalue reference.  Only the order is wrong, which is what
// makes this a sharper probe than a function of plain ints: everything
// the shape checks except the order is satisfied.
void producer_first(fake_producer<int>&&, fake_consumer<int>&&) noexcept {}

// Declared and never used, for the reason given in the sibling fixture.
using forged = ::fixy::concurrent::pipeline_stage_output_value_t<&producer_first>;

}  // namespace

int main() { return 0; }
