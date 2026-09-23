// The diamond graph, run for real: four stages over a StageGraph whose
// edges fan out from one source and back into one sink, each stage body
// counted exactly once.
//
// Old spelling: test/test_pipeline_dag.cpp.  Its three other cases build
// their stages from channel endpoints through the endpoint bridge, and
// arrive with that bridge.

#include <fixy/Ctx.h>
#include <fixy/concurrent/Pipeline.h>

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <utility>

namespace cc = fixy::concurrent;

namespace pipeline_dag_test {

template <typename T>
struct FakeConsumer {
    static constexpr std::size_t per_call_working_set = 64;
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct FakeProducer {
    static constexpr std::size_t per_call_working_set = 64;
    [[nodiscard]] bool try_push(T const&) noexcept { return true; }
};

std::atomic<int> dag_calls{0};

static void reset() noexcept { dag_calls.store(0, std::memory_order_relaxed); }

static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::abort();
    }
}

static void one_to_one_body(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {
    dag_calls.fetch_add(1, std::memory_order_relaxed);
}

using PlainStage = cc::Stage<&one_to_one_body, fixy::HotFgCtx>;

static void test_diamond_dag_runtime() {
    reset();
    fixy::HotFgCtx ctx{};

    auto s0 = cc::mint_stage<&one_to_one_body>(ctx, FakeConsumer<int>{}, FakeProducer<int>{});
    auto s1 = cc::mint_stage<&one_to_one_body>(ctx, FakeConsumer<int>{}, FakeProducer<int>{});
    auto s2 = cc::mint_stage<&one_to_one_body>(ctx, FakeConsumer<int>{}, FakeProducer<int>{});
    auto s3 = cc::mint_stage<&one_to_one_body>(ctx, FakeConsumer<int>{}, FakeProducer<int>{});

    using Graph = cc::StageGraph<
        cc::StagePack<PlainStage, PlainStage, PlainStage, PlainStage>,
        cc::EdgePack<cc::StageEdge<0, 1>, cc::StageEdge<0, 2>, cc::StageEdge<1, 3>, cc::StageEdge<2, 3>>>;
    using Cycle = cc::StageGraph<cc::StagePack<PlainStage, PlainStage>, cc::EdgePack<cc::StageEdge<1, 0>>>;
    using Unreachable =
        cc::StageGraph<cc::StagePack<PlainStage, PlainStage, PlainStage>, cc::EdgePack<cc::StageEdge<0, 1>>>;

    static_assert(cc::StageGraphWellFormed<Graph>);
    static_assert(!cc::StageGraphWellFormed<Cycle>);
    static_assert(!cc::StageGraphWellFormed<Unreachable>);

    auto pipeline = cc::mint_pipeline_dag(ctx, Graph{}, std::move(s0), std::move(s1), std::move(s2), std::move(s3));
    std::move(pipeline).run();

    require(dag_calls.load(std::memory_order_relaxed) == 4, "diamond DAG did not run all four stages");
}

}  // namespace pipeline_dag_test

int main() {
    pipeline_dag_test::test_diamond_dag_runtime();
    std::fprintf(stderr, "test_pipeline_dag: ALL PASSED\n");
    return EXIT_SUCCESS;
}
