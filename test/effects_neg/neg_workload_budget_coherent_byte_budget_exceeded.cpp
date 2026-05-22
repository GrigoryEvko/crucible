// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-075 HS14 fixture #1: `WorkloadBudgetCoherent<Ctx, Pipeline>`
// rejects a ctx whose `ctx_workload::ByteBudget<N>` is smaller than
// the pipeline's static `aggregate_per_call_working_set`.  Exercises
// the WORKLOAD-BYTE-BUDGET axis of the coherence concept.
//
// Why this matters: V-075's load-bearing claim is that ctx
// cost-model declarations must structurally cohere with the pipeline
// they will run.  A ctx that claims "≤ 8 KiB workload" paired with a
// pipeline measuring 256 KiB aggregate is a type-level contradiction —
// every NUMA / batch / prefetch decision made on the basis of the
// 8 KiB claim was wrong, and the runtime cost model would emit a
// degenerate decision.  This fixture proves the gate fires.
//
// Distinct mismatch class (per HS14 "≥2 distinct mismatch classes"):
// this fixture exercises the BYTE-BUDGET half.  Sibling
// `neg_workload_budget_coherent_stack_overflow.cpp` exercises the
// ALLOC-CLASS half (Stack ctx + WS > 1 MiB).
//
// Expected diagnostic: "static assertion failed" /
// "WorkloadBudgetCoherent" / "coherence" / "constraints not satisfied".

#include <crucible/concurrent/Pipeline.h>
#include <crucible/concurrent/WorkloadBudgetCoherent.h>
#include <crucible/effects/ExecCtx.h>

#include <cstddef>
#include <optional>

namespace cc = crucible::concurrent;
namespace eff = crucible::effects;

namespace {

constexpr std::size_t KiB = 1024;

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

// Pipeline aggregate WS = 256 KiB.
static void big_stage(Consumer<128 * KiB>&&, Producer<128 * KiB>&&) noexcept {}
using BigStage = cc::Stage<&big_stage, eff::BgDrainCtx>;

}  // anonymous namespace

// stage_inline_safe is keyed by the test's stage type.
namespace crucible::concurrent {
template <>
struct stage_inline_safe<::BigStage> : std::true_type {};
}  // namespace crucible::concurrent

int main() {
    using BigPipeline = cc::Pipeline<BigStage>;
    static_assert(BigPipeline::aggregate_per_call_working_set == 256 * KiB,
        "smoke: aggregate WS must be 256 KiB to make this fixture sound.");

    // Ctx declares ByteBudget<8 KiB> — a third of the pipeline's WS.
    // WorkloadBudgetCoherent must reject the pair.
    using TooTightCtx = decltype(eff::BgDrainCtx{}.with_workload<
        eff::ctx_workload::ByteBudget<8 * KiB>>());

    // The load-bearing static_assert — MUST FAIL.
    static_assert(cc::WorkloadBudgetCoherent<TooTightCtx, BigPipeline>,
        "ctx ByteBudget<8 KiB> contradicts pipeline aggregate WS 256 KiB "
        "— WorkloadBudgetCoherent must reject this pair.");

    return 0;
}
