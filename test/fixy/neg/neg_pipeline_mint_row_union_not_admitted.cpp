// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The payloads chain and every stage fit its own context; what fails is
// the coordinator.  One stage was minted under the background drain
// context and one under the startup context, so the union the pipeline
// needs is Bg, Alloc, Init and IO, and the foreground coordinator admits
// none of them.  Neither of those contexts is built from nothing, so the
// stages are typed through declval.
//
// The refusal is CtxFitsPipeline, in the requires clause.  It was the
// row-mismatch block inside the body until the clause gained the gate:
// a body assertion is a hard error rather than a constraint, so
// `requires { mint_pipeline(bad_ctx, ...) }` answered true and no caller
// could probe the mint before calling it.  The clause answers honestly.

#include <fixy/Ctx.h>
#include <fixy/concurrent/Pipeline.h>

#include <optional>
#include <utility>

namespace {

template <typename T>
struct FakeConsumer {
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct FakeProducer {
    [[nodiscard]] bool try_push(T const&) noexcept { return false; }
};

inline void pass(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}

using DrainStage = decltype(fixy::concurrent::mint_stage<&pass>(std::declval<fixy::BgDrainCtx const&>(),
                                                                std::declval<FakeConsumer<int>&&>(),
                                                                std::declval<FakeProducer<int>&&>()));
using StartupStage = decltype(fixy::concurrent::mint_stage<&pass>(std::declval<fixy::ColdInitCtx const&>(),
                                                                  std::declval<FakeConsumer<int>&&>(),
                                                                  std::declval<FakeProducer<int>&&>()));

using Bad = decltype(fixy::concurrent::mint_pipeline(std::declval<fixy::HotFgCtx const&>(),
                                                     std::declval<DrainStage&&>(), std::declval<StartupStage&&>()));

}  // namespace

int main() { return 0; }
