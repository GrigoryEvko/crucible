// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// `mint_pipeline_dag` rejects a call when the given Stages pack is not
// the stage_pack_type of the Graph.
//
// Violation: the Graph declares StagePack<PlainStage, PlainStage> (2
// stages, edge 0 to 1), but the caller gives only ONE stage.  The second
// check of `pipeline_dag_mint_gate::compute()` compares
// `traits::stage_pack_type` (StagePack<PlainStage, PlainStage>) with
// `StagePack<std::remove_cvref_t<Stages>...>` (StagePack<PlainStage>),
// and it returns false.
//
// Expected diagnostic: "associated constraints are not satisfied"
// at CtxFitsPipelineDagMint or pipeline_dag_mint_gate.

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

using PlainStage = conc::Stage<&pass_through, eff::HotFgCtx>;

int main() {
    eff::HotFgCtx ctx;

    // The Graph declares a chain of TWO stages (stage 0 to stage 1).
    using TwoStageGraph =
        conc::StageGraph<conc::StagePack<PlainStage, PlainStage>, conc::EdgePack<conc::StageEdge<0, 1>>>;

    // The caller gives only ONE stage, so the stage-pack check rejects the call.
    auto one_stage = conc::mint_stage<&pass_through>(ctx, FakeConsumer<int>{}, FakeProducer<int>{});

    auto bad = conc::mint_pipeline_dag(ctx, TwoStageGraph{}, std::move(one_stage));
    (void)bad;
    return 0;
}
