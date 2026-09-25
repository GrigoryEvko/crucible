// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// `mint_pipeline_dag` rejects a call when the Graph argument is not a
// StageGraph specialisation.
//
// Violation: a bare int is the `Graph` argument.
// `CtxFitsPipelineDag<Ctx, int>` is false, because int does not satisfy
// IsStageGraph.  Then `CtxFitsPipelineDagMint` is false, and the requires
// clause rejects the call.
//
// Expected diagnostic: "associated constraints are not satisfied"
// at CtxFitsPipelineDagMint, CtxFitsPipelineDag, IsStageGraph or
// pipeline_dag_mint_gate.
//
// The stage-pack fixture stops at a different check.  This fixture stops
// at the IsStageGraph check, and that fixture stops at the stage-pack
// equality check.

#include <crucible/concurrent/_Pipeline.h>
#include <crucible/concurrent/_Stage.h>
#include <crucible/effects/_ExecCtx.h>

#include <optional>
#include <utility>

namespace eff = crucible::effects;
namespace conc = crucible::concurrent;

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

    auto stage = conc::mint_stage<&pass_through>(ctx, FakeConsumer<int>{}, FakeProducer<int>{});

    int not_a_graph = 0;  // This argument must be a StageGraph<StagePack<...>, EdgePack<...>>.

    auto bad = conc::mint_pipeline_dag(ctx, not_a_graph, std::move(stage));
    (void)bad;
    return 0;
}
