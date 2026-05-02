// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Tier 3 commit 1 (CLAUDE.md §XXI HS14): mint_stage<auto FnPtr>(ctx,
// in, out) requires FnPtr to satisfy the canonical PipelineStage shape
// (FOUND-D19, safety/PipelineStage.h): arity 2, parameter 0 is a
// ConsumerHandle&&, parameter 1 is a ProducerHandle&&, return void.
//
// Violation: passes a function with arity 1 (consumer-only, no
// producer).  The CtxFitsStage<FnPtr, Ctx> concept's PipelineStage<FnPtr>
// conjunct fires.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at CtxFitsStage or PipelineStage.

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

// Wrong arity: takes only the consumer side, no producer side.
inline void wrong_arity_stage(FakeConsumer<int>&&) noexcept {}

int main() {
    eff::HotFgCtx ctx;
    FakeConsumer<int> in;
    FakeProducer<int> out;

    auto bad = conc::mint_stage<&wrong_arity_stage>(
        ctx, std::move(in), std::move(out));  // CtxFitsStage / PipelineStage fails
    (void)bad;
    return 0;
}
