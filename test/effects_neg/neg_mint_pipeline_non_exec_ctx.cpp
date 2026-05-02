// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Tier 3 commit 2 (CLAUDE.md §XXI HS14): mint_pipeline requires the
// ctx parameter to satisfy IsExecCtx (Crucible's ExecCtx<...> shape
// with the four static facts).
//
// Violation: passes a bare int as the Ctx parameter.  CtxFitsPipeline's
// IsExecCtx<Ctx> conjunct fires.  Distinct from non_stage / chain
// failure axes — this is the ctx-side gate.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at CtxFitsPipeline / IsExecCtx.

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
    eff::HotFgCtx good_ctx;  // for stage construction
    FakeConsumer<int> in;
    FakeProducer<int> out;
    auto stage = conc::mint_stage<&pass_through>(
        good_ctx, std::move(in), std::move(out));

    int not_a_ctx = 0;

    auto bad = conc::mint_pipeline(not_a_ctx, std::move(stage));
    (void)bad;
    return 0;
}
