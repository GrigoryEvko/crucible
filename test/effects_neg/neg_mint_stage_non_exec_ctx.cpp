// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Tier 3 commit 1 (CLAUDE.md §XXI HS14): mint_stage<auto FnPtr>(ctx,
// in, out) requires the ctx parameter to satisfy IsExecCtx (Crucible's
// ExecCtx<...> shape with the four static facts: residency_tier,
// cap_type, row_type, locality_hint).
//
// Violation: passes a bare int as the Ctx parameter.  int is not an
// ExecCtx specialization; the CtxFitsStage<FnPtr, Ctx> concept's
// IsExecCtx<Ctx> conjunct fires.  Even though FnPtr satisfies
// PipelineStage, the ctx-side gate kicks in.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at CtxFitsStage or IsExecCtx.

#include <crucible/concurrent/Stage.h>

#include <optional>
#include <utility>

namespace conc = crucible::concurrent;

template <typename T>
struct FakeConsumer {
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct FakeProducer {
    [[nodiscard]] bool try_push(T const&) noexcept { return false; }
};

// Valid PipelineStage shape — only the ctx is wrong.
inline void valid_stage(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}

int main() {
    int not_a_ctx = 0;  // bare int, not an ExecCtx
    FakeConsumer<int> in;
    FakeProducer<int> out;

    auto bad = conc::mint_stage<&valid_stage>(
        not_a_ctx, std::move(in), std::move(out));  // IsExecCtx fails
    (void)bad;
    return 0;
}
