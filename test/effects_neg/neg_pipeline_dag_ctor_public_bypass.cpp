// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture for FIXY-V-012 (§XXI Universal Mint Pattern compliance).
//
// Premise: PipelineDag<StageGraph<...>> exists ONLY to be the load-
// bearing product of mint_pipeline_dag<Ctx, Graph, Stages...>(ctx,
// Graph{}, stages...).  The single concept gate
// `CtxFitsPipelineDagMint<Ctx, Graph, Stages...>` (== CtxFitsPipelineDag
// + StagePack identity + Subrow<stage_graph_row_union, ctx_row>) is
// checked by mint before the ctor runs.  A production-side
// `PipelineDag<Graph>{stages...}` direct-construction would emit a
// DAG whose effect row never passes through the row admission check —
// the §XXI bypass the Agent 4 audit flagged as load-bearing.
//
// Fix shape (shipped in FIXY-V-012): PipelineDag's previously-public
// ctor `PipelineDag(Stages&&...)` moved to `private:`, and
// `mint_pipeline_dag` friended so the only authorized authorization
// point remains the mint factory.  Any direct construction site is
// now ill-formed.
//
// This fixture witnesses the gate fires: it constructs two Stages
// through mint_stage (the legitimate path) and then attempts the
// rejected direct-construction of PipelineDag.  Build MUST fail;
// diagnostic MUST contain "private".

#include <crucible/concurrent/Pipeline.h>
#include <crucible/concurrent/Stage.h>
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

    auto stage_a = conc::mint_stage<&pass_through>(
        ctx, FakeConsumer<int>{}, FakeProducer<int>{});
    auto stage_b = conc::mint_stage<&pass_through>(
        ctx, FakeConsumer<int>{}, FakeProducer<int>{});

    using S = conc::Stage<&pass_through, eff::HotFgCtx>;
    using Graph = conc::StageGraph<
        conc::StagePack<S, S>,
        conc::EdgePack<conc::StageEdge<0, 1, 0, 0>>>;

    // ── The §XXI bypass attempt ────────────────────────────────────
    //
    // Direct construction of PipelineDag<Graph> bypasses
    // mint_pipeline_dag and therefore bypasses CtxFitsPipelineDagMint's
    // row admission.  With the V-012 fix, the ctor is private and this
    // line is ill-formed.  Before V-012 this would have compiled
    // silently.
    conc::PipelineDag<Graph> dag{std::move(stage_a), std::move(stage_b)};

    return 0;
}
