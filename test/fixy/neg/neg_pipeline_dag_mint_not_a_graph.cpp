// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_pipeline_dag with a bare int where the stage graph belongs.  The
// mint gate's first clause is that the graph is a StageGraph at all,
// before the stage pack is compared against it.

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

inline void pass_through(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}

}  // namespace

int main() {
    fixy::HotFgCtx ctx;
    auto stage = fixy::concurrent::mint_stage<&pass_through>(ctx, FakeConsumer<int>{}, FakeProducer<int>{});
    int not_a_graph = 0;

    auto bad = fixy::concurrent::mint_pipeline_dag(ctx, not_a_graph, std::move(stage));
    (void)bad;
    return 0;
}
