// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Tier 3 commit 2 (CLAUDE.md §XXI HS14): mint_pipeline<>(ctx, stages...)
// requires pipeline_chain<Stages...>: every adjacent (i, i+1) pair must
// satisfy stages_chain<S_i, S_{i+1}> — i.e., the output payload type
// of stage_i equals the input payload type of stage_{i+1}.
//
// Violation: chains an int→int pass-through stage with an int→int
// pass-through stage prefix, then a float→double transform — the
// (int→int, float→double) adjacency mismatches: "int" output cannot
// chain into "float" input.  pipeline_chain's adjacent-pair fold
// produces false; CtxFitsPipeline's pipeline_chain conjunct fires.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at CtxFitsPipeline / pipeline_chain.

#include <crucible/concurrent/Pipeline.h>
#include <crucible/effects/ExecCtx.h>

#include <optional>
#include <utility>

namespace conc = crucible::concurrent;
namespace eff  = crucible::effects;

template <typename T>
struct FakeConsumer {
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct FakeProducer {
    [[nodiscard]] bool try_push(T const&) noexcept { return false; }
};

// int → int pass-through.
inline void int_pass(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}

// float → double transform — chain-incompatible after int_pass.
inline void float_to_double(FakeConsumer<float>&&, FakeProducer<double>&&) noexcept {}

int main() {
    eff::HotFgCtx ctx;

    FakeConsumer<int> in0;   FakeProducer<int>    out0;
    auto stage0 = conc::mint_stage<&int_pass>(ctx, std::move(in0), std::move(out0));

    FakeConsumer<float> in1; FakeProducer<double> out1;
    auto stage1 = conc::mint_stage<&float_to_double>(ctx, std::move(in1), std::move(out1));

    auto bad = conc::mint_pipeline(ctx, std::move(stage0), std::move(stage1));
    (void)bad;
    return 0;
}
