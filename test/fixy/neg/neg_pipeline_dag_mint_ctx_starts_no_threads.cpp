// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The foreground context mints a graph of two foreground stages.  The
// graph is well formed and its row is empty, so only the authority to
// start threads is missing.  The foreground context owns neither Bg nor
// Init, so the graph mint refuses it.

#include <fixy/Ctx.h>
#include <fixy/concurrent/Pipeline.h>

#include <optional>
#include <utility>

namespace {

namespace cc = fixy::concurrent;

template <typename T>
struct FakeConsumer {
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct FakeProducer {
    [[nodiscard]] bool try_push(T const&) noexcept { return false; }
};

inline void pass_through(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}

using PlainStage = cc::Stage<&pass_through, fixy::HotFgCtx>;
using TwoStageGraph = cc::StageGraph<cc::StagePack<PlainStage, PlainStage>, cc::EdgePack<cc::StageEdge<0, 1>>>;

}  // namespace

int main() {
    fixy::HotFgCtx ctx = ::foundation::effects::testing::foreground();
    auto first = cc::mint_stage<&pass_through>(ctx, FakeConsumer<int>{}, FakeProducer<int>{});
    auto second = cc::mint_stage<&pass_through>(ctx, FakeConsumer<int>{}, FakeProducer<int>{});

    auto bad = cc::mint_pipeline_dag(ctx, TwoStageGraph{}, std::move(first), std::move(second));
    (void)bad;
    return 0;
}
