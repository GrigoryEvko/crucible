// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-075 HS14 fixture #2: `WorkloadBudgetCoherent<Ctx, Pipeline>`
// rejects a ctx whose `ctx_alloc::Stack` declaration cannot admit a
// pipeline whose aggregate per-call working set exceeds the
// `stack_alloc_max_working_set_bytes` ceiling (1 MiB).  Exercises the
// ALLOC-CLASS axis of the coherence concept.
//
// Why this matters: V-075's load-bearing claim is that ctx cost-model
// declarations must structurally cohere with the pipeline.  A ctx that
// claims "I allocate on the call stack" paired with a pipeline whose
// per-call working set is 8 MiB would guarantee a stack overflow at
// invocation — the type system catches the contradiction structurally,
// before the runtime has a chance to crash.  Linux pthread default
// stack is 8 MiB; we reserve 1 MiB as the call-tree margin and forbid
// Stack ctx + pipeline WS > 1 MiB.
//
// Distinct mismatch class (per HS14 "≥2 distinct mismatch classes"):
// this fixture exercises the ALLOC-CLASS half.  Sibling
// `neg_workload_budget_coherent_byte_budget_exceeded.cpp` exercises
// the WORKLOAD-BYTE-BUDGET half.
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

// Pipeline aggregate WS = 8 MiB — far above the 1 MiB Stack ceiling.
// Uses BgDrainCtx as the stage ctx because HotFgCtx's row would reject
// this pipeline at substrate-binding time; we want the WORKLOAD/ALLOC
// coherence check to be the failure point, not row admission.
static void huge_stage(Consumer<4 * MiB>&&, Producer<4 * MiB>&&) noexcept {}
using HugeStage = cc::Stage<&huge_stage, eff::BgDrainCtx>;

}  // anonymous namespace

// stage_inline_safe is keyed by the test's stage type.
namespace crucible::concurrent {
template <>
struct stage_inline_safe<::HugeStage> : std::true_type {};
}  // namespace crucible::concurrent

int main() {
    using HugePipeline = cc::Pipeline<HugeStage>;
    static_assert(HugePipeline::aggregate_per_call_working_set == 8 * MiB,
        "smoke: aggregate WS must be 8 MiB to make this fixture sound.");

    // Sanity smoke — 1 MiB safety budget per V-075's Stack ceiling.
    static_assert(cc::stack_alloc_max_working_set_bytes == 1 * MiB,
        "smoke: Stack alloc class declares a 1 MiB working-set ceiling.");

    // HotFgCtx is the canonical `ctx_alloc::Stack` sentinel context.
    // 8 MiB WS contradicts the 1 MiB stack budget; V-075 must reject.
    // (Default Stack ctxs pair with `ctx_workload::Unspecified` which
    // admits any byte budget — the failure here is purely ALLOC-CLASS.)

    // The load-bearing static_assert — MUST FAIL.
    static_assert(cc::WorkloadBudgetCoherent<eff::HotFgCtx, HugePipeline>,
        "Stack ctx (1 MiB ceiling) contradicts pipeline aggregate WS 8 MiB "
        "— WorkloadBudgetCoherent must reject this pair.");

    return 0;
}
