// The coherence concept refuses a context and pipeline that contradict
// each other on any of three cost-model axes: the workload byte budget,
// the ceiling an allocation class puts on a working set, and the floor a
// NUMA policy puts under one.  Only admission is testable here.  A pair
// that must fail to compile lives in a negative-compile fixture instead.

#include <crucible/concurrent/Pipeline.h>
#include <crucible/concurrent/WorkloadBudgetCoherent.h>
#include <crucible/effects/ExecCtx.h>

#include <cstddef>
#include <cstdio>
#include <optional>

namespace cc = crucible::concurrent;
namespace eff = crucible::effects;

namespace workload_budget_coherent_test {

constexpr std::size_t KiB = 1024;
constexpr std::size_t MiB = 1024 * KiB;

template <std::size_t Ws>
struct Consumer {
    static constexpr std::size_t per_call_working_set = Ws;
    [[nodiscard]] std::optional<int> try_pop() noexcept { return 1; }
};

template <std::size_t Ws>
struct Producer {
    static constexpr std::size_t per_call_working_set = Ws;
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

static void small_stage(Consumer<4 * KiB>&&, Producer<4 * KiB>&&) noexcept {}
using SmallStage = cc::Stage<&small_stage, eff::BgDrainCtx>;
static_assert(cc::stage_per_call_ws_v<SmallStage> == 8 * KiB);

static void medium_stage(Consumer<128 * KiB>&&, Producer<128 * KiB>&&) noexcept {}
using MediumStage = cc::Stage<&medium_stage, eff::BgDrainCtx>;
static_assert(cc::stage_per_call_ws_v<MediumStage> == 256 * KiB);

static void large_stage(Consumer<4 * MiB>&&, Producer<4 * MiB>&&) noexcept {}
using LargeStage = cc::Stage<&large_stage, eff::BgDrainCtx>;
static_assert(cc::stage_per_call_ws_v<LargeStage> == 8 * MiB);

}  // namespace workload_budget_coherent_test

namespace crucible::concurrent {

template <>
struct stage_inline_safe<workload_budget_coherent_test::SmallStage> : std::true_type {};

template <>
struct stage_inline_safe<workload_budget_coherent_test::MediumStage> : std::true_type {};

template <>
struct stage_inline_safe<workload_budget_coherent_test::LargeStage> : std::true_type {};

}  // namespace crucible::concurrent

namespace workload_budget_coherent_test {

using SmallPipeline = cc::Pipeline<SmallStage>;
using MediumPipeline = cc::Pipeline<MediumStage>;
using LargePipeline = cc::Pipeline<LargeStage>;

static_assert(SmallPipeline::aggregate_per_call_working_set == 8 * KiB);
static_assert(MediumPipeline::aggregate_per_call_working_set == 256 * KiB);
static_assert(LargePipeline::aggregate_per_call_working_set == 8 * MiB);
static_assert(SmallPipeline::aggregate_working_set_known);
static_assert(MediumPipeline::aggregate_working_set_known);
static_assert(LargePipeline::aggregate_working_set_known);

// The default workload hint is unspecified, which bounds nothing.
static_assert(cc::WorkloadBudgetCoherent<eff::BgDrainCtx, SmallPipeline>);
static_assert(cc::WorkloadBudgetCoherent<eff::BgDrainCtx, LargePipeline>);

using TinyBudgetCtx = decltype(eff::BgDrainCtx{}.with_workload<eff::ctx_workload::ByteBudget<16 * KiB>>());
static_assert(cc::WorkloadBudgetCoherent<TinyBudgetCtx, SmallPipeline>);
static_assert(!cc::WorkloadBudgetCoherent<TinyBudgetCtx, MediumPipeline>);

using LargeBudgetCtx = decltype(eff::BgDrainCtx{}.with_workload<eff::ctx_workload::ByteBudget<16 * MiB>>());
static_assert(cc::WorkloadBudgetCoherent<LargeBudgetCtx, LargePipeline>);

// A channel budget bounds the working set the same way a byte budget does.
using ChannelBudgetCtx =
    decltype(eff::BgDrainCtx{}.with_workload<eff::ctx_workload::ChannelBudget<8 * KiB, 1, 1, false>>());
static_assert(!cc::WorkloadBudgetCoherent<ChannelBudgetCtx, MediumPipeline>);

// The hot foreground context allocates on the stack, which caps a
// working set at one mebibyte.
static_assert(cc::WorkloadBudgetCoherent<eff::HotFgCtx, SmallPipeline>);
static_assert(!cc::WorkloadBudgetCoherent<eff::HotFgCtx, LargePipeline>);

// The background context allocates from an arena and caps nothing.
static_assert(cc::WorkloadBudgetCoherent<eff::BgDrainCtx, LargePipeline>);

// A local NUMA policy sets no floor.
static_assert(cc::WorkloadBudgetCoherent<eff::BgDrainCtx, SmallPipeline>);

// The cold init context spreads across nodes, which is only worth doing
// above four mebibytes, so it refuses anything smaller.
static_assert(!cc::WorkloadBudgetCoherent<eff::ColdInitCtx, SmallPipeline>);
static_assert(!cc::WorkloadBudgetCoherent<eff::ColdInitCtx, MediumPipeline>);
// The large pipeline clears that floor, and its allocation class and
// workload hint are both unbounded, so all three axes admit it.
static_assert(cc::WorkloadBudgetCoherent<eff::ColdInitCtx, LargePipeline>);

// A stage that declares no static working set yields a pipeline whose
// aggregate is unknown, and the concept then has nothing to contradict.

template <std::size_t Ws>
struct StatelessConsumer {
    // The absence of per_call_working_set here is the point.
    [[nodiscard]] std::optional<int> try_pop() noexcept { return 1; }
};

template <std::size_t Ws>
struct StatelessProducer {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

static void stateless_stage(StatelessConsumer<0>&&, StatelessProducer<0>&&) noexcept {}
using StatelessStage = cc::Stage<&stateless_stage, eff::BgDrainCtx>;
using StatelessPipeline = cc::Pipeline<StatelessStage>;

static_assert(!StatelessPipeline::aggregate_working_set_known,
              "StatelessPipeline must not advertise a static aggregate working set.");

// Even the most restrictive context admits it.
static_assert(cc::WorkloadBudgetCoherent<TinyBudgetCtx, StatelessPipeline>);
static_assert(cc::WorkloadBudgetCoherent<eff::ColdInitCtx, StatelessPipeline>);

}  // namespace workload_budget_coherent_test

int main() {
    std::printf("concurrent::WorkloadBudgetCoherent smoke OK\n");
    return 0;
}
