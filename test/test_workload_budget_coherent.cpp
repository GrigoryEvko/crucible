// crucible::concurrent::WorkloadBudgetCoherent smoke test (FIXY-V-075).
//
// Verifies the type-level coherence concept rejects ctx/pipeline pairs
// that contradict each other on one of three cost-model axes
// (workload byte budget, alloc class WS ceiling, NUMA WS floor) and
// admits coherent pairs.
//
// Positive cases live as `static_assert(...)` cells below; negative
// cases live in test/effects_neg/neg_workload_budget_coherent_*.cpp.

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

// ── A small stage: 4 KiB consumer + 4 KiB producer = 8 KiB WS ─────────
static void small_stage(Consumer<4 * KiB>&&, Producer<4 * KiB>&&) noexcept {}
using SmallStage = cc::Stage<&small_stage, eff::BgDrainCtx>;
static_assert(cc::stage_per_call_ws_v<SmallStage> == 8 * KiB);

// ── A medium stage: 128 KiB consumer + 128 KiB producer = 256 KiB ─────
static void medium_stage(Consumer<128 * KiB>&&, Producer<128 * KiB>&&) noexcept {}
using MediumStage = cc::Stage<&medium_stage, eff::BgDrainCtx>;
static_assert(cc::stage_per_call_ws_v<MediumStage> == 256 * KiB);

// ── A large stage: 4 MiB consumer + 4 MiB producer = 8 MiB ────────────
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

using SmallPipeline = cc::Pipeline<SmallStage>;          // 8 KiB
using MediumPipeline = cc::Pipeline<MediumStage>;        // 256 KiB
using LargePipeline = cc::Pipeline<LargeStage>;          // 8 MiB

static_assert(SmallPipeline::aggregate_per_call_working_set == 8 * KiB);
static_assert(MediumPipeline::aggregate_per_call_working_set == 256 * KiB);
static_assert(LargePipeline::aggregate_per_call_working_set == 8 * MiB);
static_assert(SmallPipeline::aggregate_working_set_known);
static_assert(MediumPipeline::aggregate_working_set_known);
static_assert(LargePipeline::aggregate_working_set_known);

// ════════════════════════════════════════════════════════════════════
// ── (1) Workload byte-budget admission ──────────────────────────────
// ════════════════════════════════════════════════════════════════════

// Default ctx with Unspecified workload → admits everything.
static_assert(cc::WorkloadBudgetCoherent<eff::BgDrainCtx, SmallPipeline>);
static_assert(cc::WorkloadBudgetCoherent<eff::BgDrainCtx, LargePipeline>);

// Ctx with ByteBudget<16 KiB> admits SmallPipeline (8 KiB ≤ 16 KiB).
using TinyBudgetCtx = decltype(eff::BgDrainCtx{}.with_workload<
    eff::ctx_workload::ByteBudget<16 * KiB>>());
static_assert(cc::WorkloadBudgetCoherent<TinyBudgetCtx, SmallPipeline>);

// Ctx with ByteBudget<16 KiB> REJECTS MediumPipeline (256 KiB > 16 KiB).
static_assert(!cc::WorkloadBudgetCoherent<TinyBudgetCtx, MediumPipeline>);

// Ctx with ByteBudget<16 MiB> admits LargePipeline (8 MiB ≤ 16 MiB).
using LargeBudgetCtx = decltype(eff::BgDrainCtx{}.with_workload<
    eff::ctx_workload::ByteBudget<16 * MiB>>());
static_assert(cc::WorkloadBudgetCoherent<LargeBudgetCtx, LargePipeline>);

// Ctx with ChannelBudget<8 KiB> REJECTS MediumPipeline (256 KiB > 8 KiB).
using ChannelBudgetCtx = decltype(eff::BgDrainCtx{}.with_workload<
    eff::ctx_workload::ChannelBudget<8 * KiB, 1, 1, false>>());
static_assert(!cc::WorkloadBudgetCoherent<ChannelBudgetCtx, MediumPipeline>);

// ════════════════════════════════════════════════════════════════════
// ── (2) Alloc-class WS ceiling admission ────────────────────────────
// ════════════════════════════════════════════════════════════════════

// HotFgCtx uses ctx_alloc::Stack — admits SmallPipeline (8 KiB ≤ 1 MiB).
static_assert(cc::WorkloadBudgetCoherent<eff::HotFgCtx, SmallPipeline>);

// HotFgCtx with Stack REJECTS LargePipeline (8 MiB > 1 MiB stack limit).
static_assert(!cc::WorkloadBudgetCoherent<eff::HotFgCtx, LargePipeline>);

// BgDrainCtx uses ctx_alloc::Arena — admits LargePipeline (no ceiling).
static_assert(cc::WorkloadBudgetCoherent<eff::BgDrainCtx, LargePipeline>);

// ════════════════════════════════════════════════════════════════════
// ── (3) NUMA-policy WS floor admission ──────────────────────────────
// ════════════════════════════════════════════════════════════════════

// Default BgDrainCtx uses ctx_numa::Local — no floor; admits all.
static_assert(cc::WorkloadBudgetCoherent<eff::BgDrainCtx, SmallPipeline>);

// ColdInitCtx uses ctx_numa::Spread — REJECTS workloads below 4 MiB.
// SmallPipeline 8 KiB → reject; MediumPipeline 256 KiB → reject;
// LargePipeline 8 MiB → admit (≥ 4 MiB).
static_assert(!cc::WorkloadBudgetCoherent<eff::ColdInitCtx, SmallPipeline>);
static_assert(!cc::WorkloadBudgetCoherent<eff::ColdInitCtx, MediumPipeline>);
// LargePipeline + ColdInitCtx: passes NUMA floor (8 MiB ≥ 4 MiB);
// passes alloc class (Heap is unbounded); passes workload hint
// (Unspecified is unbounded).  All three axes admit.
static_assert(cc::WorkloadBudgetCoherent<eff::ColdInitCtx, LargePipeline>);

// ════════════════════════════════════════════════════════════════════
// ── (4) Trivially-true (unknown working set) admission ──────────────
// ════════════════════════════════════════════════════════════════════

// A stage whose per_call_working_set is NOT static (no
// `static constexpr per_call_working_set`) yields a Pipeline with
// `aggregate_working_set_known == false` — the concept admits trivially.

template <std::size_t Ws>
struct StatelessConsumer {
    // NB: no `per_call_working_set` here.
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
    "StatelessPipeline should not advertise a static aggregate WS.");

// Even the most restrictive ctx admits a pipeline with unknown WS —
// no measurable contradiction.
static_assert(cc::WorkloadBudgetCoherent<TinyBudgetCtx, StatelessPipeline>);
static_assert(cc::WorkloadBudgetCoherent<eff::ColdInitCtx, StatelessPipeline>);

}  // namespace workload_budget_coherent_test

int main() {
    std::printf("concurrent::WorkloadBudgetCoherent smoke OK\n");
    return 0;
}
