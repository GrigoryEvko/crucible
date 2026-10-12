// The diamond graph, run for real: a fan-out stage gives each value to
// two stages, a fan-in stage reads the two, and each stage body runs one
// time.  Static assertions check the ports of the diamond and the row of a
// pipeline.
//
// test/fixy/test_endpoint.cpp runs a fan-in stage that it builds from
// channel endpoints.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/Pipeline.h>
#include <foundation/effects/Row.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <type_traits>
#include <utility>
#include "../test_assert.h"

namespace cc = fixy::concurrent;

namespace pipeline_dag_test {

// Each edge joins two handles, and the handles of an edge name one channel.
template <typename T>
struct FakeChannel {};

template <typename T>
struct FakeConsumer {
    using channel_type = FakeChannel<T>;
    static constexpr std::size_t per_call_working_set = 64;
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct FakeProducer {
    using channel_type = FakeChannel<T>;
    static constexpr std::size_t per_call_working_set = 64;
    [[nodiscard]] bool try_push(T const&) noexcept { return true; }
};

// The count of the stage bodies that ran.
fixy::Tally dag_calls;

static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::abort();
    }
}

static void one_to_one_body(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept { dag_calls.add(1); }

// A fan-out stage has two output ports, and a fan-in stage has two input
// ports.  Each edge names the port that it leaves and the port that it
// enters.
static void fan_out_body(FakeConsumer<int>&&, FakeProducer<int>&&, FakeProducer<int>&&) noexcept { dag_calls.add(1); }
static void fan_in_body(FakeConsumer<int>&&, FakeConsumer<int>&&, FakeProducer<int>&&) noexcept { dag_calls.add(1); }

using PlainStage = cc::Stage<&one_to_one_body, fixy::HotFgCtx>;
using FanOutStage = cc::MpmcStage<&fan_out_body, fixy::HotFgCtx>;
using FanInStage = cc::MpmcStage<&fan_in_body, fixy::HotFgCtx>;

static_assert(cc::stage_input_count_v<FanOutStage> == 1 && cc::stage_output_count_v<FanOutStage> == 2);
static_assert(cc::stage_input_count_v<FanInStage> == 2 && cc::stage_output_count_v<FanInStage> == 1);

// The diamond.  Each port of the fan-out and the fan-in has one edge.  An
// edge from port 0 takes the port of the first edge, and port 2 is a port
// that the fan-out does not have.
template <std::size_t SecondPort>
using Diamond = cc::StageGraph<cc::StagePack<FanOutStage, PlainStage, PlainStage, FanInStage>,
                               cc::EdgePack<cc::StageEdge<0, 1, 0, 0>, cc::StageEdge<0, 2, SecondPort, 0>,
                                            cc::StageEdge<1, 3, 0, 0>, cc::StageEdge<2, 3, 0, 1>>>;

static_assert(cc::StageGraphWellFormed<Diamond<1>>);
static_assert(!cc::StageGraphWellFormed<Diamond<0>>);
static_assert(!cc::StageGraphWellFormed<Diamond<2>>);

// The coordinating context must admit the row of every stage, so the row
// of a pipeline is the union of the stage rows, and no effect more.
namespace eff = ::foundation::effects;
using BgStage = cc::Stage<&one_to_one_body, fixy::BgDrainCtx>;
using InitStage = cc::Stage<&one_to_one_body, fixy::ColdInitCtx>;
using BgAndInitRow = eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::Init, eff::Effect::IO>;
using MixedRow = cc::pipeline_row_union_t<BgStage, PlainStage, InitStage>;

static_assert(std::is_same_v<cc::pipeline_row_union_t<PlainStage, PlainStage>, eff::Row<>>);
static_assert(eff::Subrow<MixedRow, BgAndInitRow> && eff::Subrow<BgAndInitRow, MixedRow>);

static void test_diamond_dag_runtime() {
    fixy::HotFgCtx ctx = ::foundation::effects::testing::foreground();

    auto fan_out =
        cc::mint_mpmc_stage<&fan_out_body>(ctx, FakeConsumer<int>{}, FakeProducer<int>{}, FakeProducer<int>{});
    auto left = cc::mint_stage<&one_to_one_body>(ctx, FakeConsumer<int>{}, FakeProducer<int>{});
    auto right = cc::mint_stage<&one_to_one_body>(ctx, FakeConsumer<int>{}, FakeProducer<int>{});
    auto fan_in = cc::mint_mpmc_stage<&fan_in_body>(ctx, FakeConsumer<int>{}, FakeConsumer<int>{}, FakeProducer<int>{});

    using Cycle = cc::StageGraph<cc::StagePack<PlainStage, PlainStage>, cc::EdgePack<cc::StageEdge<1, 0>>>;
    static_assert(!cc::StageGraphWellFormed<Cycle>);

    const fixy::BgDrainCtx coordinator{::foundation::effects::testing::bg()};
    auto pipeline = cc::mint_pipeline_dag(coordinator, Diamond<1>{}, std::move(fan_out), std::move(left),
                                          std::move(right), std::move(fan_in));
    std::move(pipeline).run(coordinator);

    require(dag_calls.read() == 4, "the diamond did not run each of its four stages one time");
}

}  // namespace pipeline_dag_test

int main() {
    pipeline_dag_test::test_diamond_dag_runtime();
    crucible::test::pass("test_pipeline_dag: ALL PASSED\n");
    return EXIT_SUCCESS;
}
