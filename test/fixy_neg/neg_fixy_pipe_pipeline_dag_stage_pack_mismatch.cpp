// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-AUDIT-D7 fixture: `mint_pipeline_dag` via fixy:: alias rejects
// when the supplied Stages... pack does not match the Graph's
// declared stage_pack_type.
//
// Violation: the Graph declares a StagePack<PlainStage, PlainStage>
// (2 stages, edge 0→1) but only ONE stage is provided.  Phase 2 of
// `pipeline_dag_mint_gate::compute()` compares
// `traits::stage_pack_type` (StagePack<PlainStage, PlainStage>) to
// `StagePack<std::remove_cvref_t<Stages>...>` (StagePack<PlainStage>)
// and returns false.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at CtxFitsPipelineDagMint / pipeline_dag_mint_gate.

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Pipe.h>

#include <optional>
#include <utility>

namespace eff   = crucible::effects;
namespace fpipe = crucible::fixy::pipe;
namespace conc  = crucible::concurrent;

template <typename T>
struct FakeConsumer {
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct FakeProducer {
    [[nodiscard]] bool try_push(T const&) noexcept { return false; }
};

inline void pass_through(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}

using PlainStage = fpipe::Stage<&pass_through, eff::HotFgCtx>;

int main() {
    eff::HotFgCtx ctx;

    // Graph declares a TWO-stage chain (stage 0 → stage 1).
    using TwoStageGraph = conc::StageGraph<
        conc::StagePack<PlainStage, PlainStage>,
        conc::EdgePack<conc::StageEdge<0, 1>>>;

    // But the caller supplies only ONE stage — pack-mismatch gate
    // fires.
    auto one_stage = fpipe::mint_stage<&pass_through>(
        ctx, FakeConsumer<int>{}, FakeProducer<int>{});

    auto bad = fpipe::mint_pipeline_dag(
        ctx, TwoStageGraph{}, std::move(one_stage));
    (void)bad;
    return 0;
}
