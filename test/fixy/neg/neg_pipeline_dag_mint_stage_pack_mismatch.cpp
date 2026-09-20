// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The graph declares two stages joined by one edge, and the call
// supplies one stage.  The graph is well formed; the mint gate's second
// clause, that the supplied pack is the graph's stage pack, is what
// refuses.

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
    fixy::HotFgCtx ctx;
    auto only_stage = cc::mint_stage<&pass_through>(ctx, FakeConsumer<int>{}, FakeProducer<int>{});

    auto bad = cc::mint_pipeline_dag(ctx, TwoStageGraph{}, std::move(only_stage));
    (void)bad;
    return 0;
}
