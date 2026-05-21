// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture for FIXY-V-009 (§XXI Universal Mint Pattern compliance).
//
// Premise: Pipeline<Stages...> exists ONLY to be the load-bearing
// product of mint_pipeline<Ctx, Stages...>(ctx, ...).  The single
// concept gate `CtxFitsPipeline<Ctx, Stages...>` (== IsExecCtx + every
// stage_chains + Subrow<required_row, ctx_row>) is checked by mint
// before the ctor runs.  A production-side `Pipeline<S1, S2>{s1, s2}`
// direct-construction would emit a Pipeline whose effect row never
// passes through `decide::row_subset<required, ctx>` — that is the
// exact §XXI bypass the Agent 4 audit flagged as load-bearing.
//
// Fix shape (shipped in FIXY-V-009): Pipeline's previously-public
// ctor `Pipeline(Stages&&...)` moved to `private:`, and
// `mint_pipeline` friended so the only authorized authorization
// point remains the mint factory.  Any direct construction site is
// now ill-formed.
//
// This fixture witnesses the gate fires: it constructs a Stage pair
// through mint_stage (the legitimate path for stages, V-010 not yet
// landed) and then attempts the rejected direct-construction of
// Pipeline.  Build MUST fail; diagnostic MUST contain "private".

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

    // ── The §XXI bypass attempt ────────────────────────────────────
    //
    // Direct construction of Pipeline<S, S> bypasses mint_pipeline
    // and therefore bypasses CtxFitsPipeline's row admission.  With
    // the V-009 fix, the ctor is private and this line is ill-formed.
    // Before V-009 this would have compiled silently.
    conc::Pipeline<S, S> pipeline{std::move(stage_a), std::move(stage_b)};

    return 0;
}
