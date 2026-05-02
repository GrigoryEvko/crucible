// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Tier 3 commit 2 (CLAUDE.md §XXI HS14): mint_pipeline's
// CtxFitsPipeline gate's pipeline_chain conjunct requires
// (IsStage<Stages> && ...) — every element of the variadic pack
// must be a Stage<FnPtr_i, Ctx_i> specialization.
//
// Violation: passes a bare int as a stage.  IsStage<int> is false;
// pipeline_chain rejects.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at CtxFitsPipeline / pipeline_chain / IsStage.

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

inline void pass_through(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}

int main() {
    eff::HotFgCtx ctx;

    FakeConsumer<int> in;
    FakeProducer<int> out;
    auto stage = conc::mint_stage<&pass_through>(
        ctx, std::move(in), std::move(out));

    int not_a_stage = 42;

    auto bad = conc::mint_pipeline(ctx, std::move(stage), not_a_stage);
    (void)bad;
    return 0;
}
