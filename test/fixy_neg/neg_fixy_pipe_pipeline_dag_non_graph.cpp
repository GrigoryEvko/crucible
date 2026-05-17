// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-AUDIT-D7 fixture: `mint_pipeline_dag` via fixy:: alias rejects
// when the Graph parameter is not a StageGraph specialisation.
//
// Violation: passes a bare int as the `Graph` parameter.
// `CtxFitsPipelineDag<Ctx, int>` is false (int is not an IsStageGraph)
// → `CtxFitsPipelineDagMint` fails → requires-clause fires.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at CtxFitsPipelineDagMint / CtxFitsPipelineDag /
// IsStageGraph / pipeline_dag_mint_gate.
//
// Distinct rejection class from the stage-pack-mismatch fixture:
// this exercises the IsStageGraph well-formedness gate (Phase 1 of
// the mint gate), not the stage-pack equality gate (Phase 2).

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

inline void pass_through(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}

int main() {
    eff::HotFgCtx ctx;

    auto stage = fpipe::mint_stage<&pass_through>(
        ctx, FakeConsumer<int>{}, FakeProducer<int>{});

    int not_a_graph = 0;  // ← must be a StageGraph<StagePack<...>, EdgePack<...>>

    auto bad = fpipe::mint_pipeline_dag(
        ctx, not_a_graph, std::move(stage));
    (void)bad;
    return 0;
}
