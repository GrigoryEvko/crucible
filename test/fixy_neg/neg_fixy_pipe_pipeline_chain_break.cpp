// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-MINT-Pipe fixture #3: mint_pipeline via fixy:: alias rejects
// when two well-formed Stages do NOT chain — the output element type
// of stage[i] differs from the input element type of stage[i+1].
//
// Distinct mismatch class from neg_fixy_pipe_pipeline_non_stage.cpp
// (fixture #2): that passes a NON-Stage element, failing the
// `(IsStage<Stages> && ...)` conjunct of pipeline_chain.  This passes
// two VALID Stages that fail the `stages_chain<S_i, S_{i+1}>`
// adjacent-compatibility fold (Pipeline.h:309-311 requires
// is_same_v<stage_output_value_t<S0,0>, stage_input_value_t<S1,0>>).
//
// Both stages mint cleanly on their own (int→int and double→double,
// empty payload rows under HotFgCtx); the ONLY rejection is at
// mint_pipeline, where stage0 outputs `int` but stage1 consumes
// `double`, so the pipeline_chain fold reddens and CtxFitsPipeline
// rejects.  Routing through `fixy::pipe::mint_pipeline` must reject
// identically to the substrate.  Mirrors the in-header static_assert
// `static_assert(!stages_chain<S_int_to_int, S_float_to_double>)`
// (Pipeline.h:1064).
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at CtxFitsPipeline / pipeline_chain / stages_chain.

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Pipe.h>

#include <optional>
#include <utility>

namespace eff   = crucible::effects;
namespace fpipe = crucible::fixy::pipe;

template <typename T>
struct FakeConsumer {
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct FakeProducer {
    [[nodiscard]] bool try_push(T const&) noexcept { return false; }
};

inline void int_stage(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}
inline void double_stage(FakeConsumer<double>&&, FakeProducer<double>&&) noexcept {}

int main() {
    eff::HotFgCtx ctx;

    FakeConsumer<int> in0;
    FakeProducer<int> out0;
    auto stage0 = fpipe::mint_stage<&int_stage>(
        ctx, std::move(in0), std::move(out0));

    FakeConsumer<double> in1;
    FakeProducer<double> out1;
    auto stage1 = fpipe::mint_stage<&double_stage>(
        ctx, std::move(in1), std::move(out1));

    // stage0 outputs int, stage1 consumes double — stages_chain<S0, S1>
    // is false, so pipeline_chain<S0, S1> reddens.
    auto bad = fpipe::mint_pipeline(ctx, std::move(stage0), std::move(stage1));
    (void)bad;
    return 0;
}
