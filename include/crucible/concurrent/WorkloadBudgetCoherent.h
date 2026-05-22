#pragma once

// crucible::concurrent::WorkloadBudgetCoherent — type-level coherence
// gate between a Ctx's cost-model axes (Workload, Alloc, NUMA) and a
// Pipeline's compile-time aggregate working set (FIXY-V-075).
//
// ─── PURPOSE ──────────────────────────────────────────────────────────
//
// CtxFitsPipeline<Ctx, Stages...> (Pipeline.h) already checks ctx row
// admission — the pipeline must not engage capabilities the ctx forbids.
// What it does NOT check is whether the COST-MODEL axes the ctx
// declared cohere with the pipeline's measured working-set facts.
//
// Three load-bearing claims a ctx can make that the type system can
// CONTRADICT structurally:
//
//   (1) Workload byte budget — `ctx_workload::ByteBudget<N>` says "this
//       ctx runs over ≤ N bytes of data".  A Pipeline whose aggregate
//       per-call working set EXCEEDS N is a type-level lie: every cost
//       model decision the ctx authorized (NUMA, batch size,
//       prefetch hints) was based on a budget the pipeline overshoots.
//
//   (2) Alloc class — `ctx_alloc::Stack` says "this ctx allocates on
//       the call stack".  A Pipeline with multi-MiB aggregate WS would
//       guarantee a stack overflow.  Linux pthread default stack is
//       8 MiB; we reserve 1 MiB as the call-tree margin and forbid
//       Stack ctx + pipeline WS > 1 MiB.
//
//   (3) NUMA policy — `ctx_numa::Spread` says "distribute workers
//       across NUMA nodes".  Only profitable for DRAM-bound workloads
//       (working set ≥ L3).  Below that the cross-socket access cost
//       dwarfs any parallel speedup.  Below L3, Spread + small WS is
//       a guaranteed regression — the cache-tier rule (CLAUDE.md §IX).
//
// All three are structural type-level facts.  The runtime cost model
// (AdaptiveScheduler / WorkloadProfiler) handles the dynamic
// decisions; this concept catches the cases where the runtime would
// observe an incoherent type-level claim and report a degenerate
// decision.
//
// ─── WHEN TO USE ──────────────────────────────────────────────────────
//
// `WorkloadBudgetCoherent` is a STANDALONE, OPT-IN concept.  Existing
// mint_pipeline call sites are NOT retroactively gated on it — the gate
// would be a breaking change to every ctx that didn't yet declare a
// workload budget.  Production sites that DO declare workload budgets
// can opt in by spelling
//
//     static_assert(
//         crucible::concurrent::WorkloadBudgetCoherent<MyCtx, MyPipeline>,
//         "ctx/pipeline workload-budget incoherence — see concept doc");
//
// at the call site, or by combining it with `CtxFitsPipeline` in a new
// `WorkloadCoherentMintGate<Ctx, Stages...>` concept (future V-V-075
// follow-on).
//
// ─── TRIVIALLY-TRUE CASES ─────────────────────────────────────────────
//
// The concept is TRUE (admissible) when there is NO measurable
// contradiction:
//
//   • `Pipeline::aggregate_working_set_known == false` — no static
//     working-set fact exists; absence of evidence is not evidence
//     of incoherence.  Default-drain-loop stages don't expose a static
//     per-call working set; pipelines containing them admit any ctx.
//
//   • `Ctx::workload_hint == ctx_workload::Unspecified` — the ctx made
//     no byte-budget claim; check (1) trivially passes.
//
//   • `Ctx::alloc_class != ctx_alloc::Stack` — no stack-overflow risk;
//     check (2) trivially passes (other alloc classes have no ceiling).
//
//   • `Ctx::numa_policy != ctx_numa::Spread` — Spread is the only
//     NUMA policy with a WS floor; check (3) trivially passes for
//     Any / Local / Pinned<N>.
//
// ─── SAFETY POSTURE ───────────────────────────────────────────────────
//
//   • InitSafe:   all helper traits are pure metafunctions over types.
//   • TypeSafe:   strong-typed extractors; no raw integer punning.
//   • NullSafe:   no pointers; type-level facts only.
//   • MemSafe:    no heap; concept is consteval-only.
//   • BorrowSafe: no aliasing; no runtime state.
//   • ThreadSafe: no shared state.
//   • LeakSafe:   no resources owned.
//   • DetSafe:    identical inputs (Ctx, Pipeline types) → identical
//                 admit/reject decision; pure type-level evaluation.

#include <crucible/effects/ExecCtx.h>          // ctx_workload, ctx_alloc, ctx_numa
#include <crucible/concurrent/WorkingSet.h>    // unknown_per_call_working_set (concept) — implicit via Pipeline

#include <cstddef>
#include <limits>
#include <type_traits>

namespace crucible::concurrent {

// ═════════════════════════════════════════════════════════════════════
// ── workload_hint_byte_budget — extract bytes from a workload hint ──
// ═════════════════════════════════════════════════════════════════════
//
// Maps a `ctx_workload::*` type to a concrete byte budget at compile
// time.  Unspecified / ItemBudget hints return SIZE_MAX (unconstrained)
// — the concept can't enforce a budget the ctx didn't declare.  Byte-
// bearing hints (ByteBudget, ChannelBudget, ProducerOnlyChannel,
// ConsumerOnlyChannel) return the declared byte count.

template <class WorkloadHint>
struct workload_hint_byte_budget {
    static constexpr std::size_t value =
        std::numeric_limits<std::size_t>::max();
};

template <>
struct workload_hint_byte_budget<
    ::crucible::effects::ctx_workload::Unspecified> {
    static constexpr std::size_t value =
        std::numeric_limits<std::size_t>::max();
};

template <std::size_t N>
struct workload_hint_byte_budget<
    ::crucible::effects::ctx_workload::ByteBudget<N>> {
    static constexpr std::size_t value = N;
};

// ItemBudget — items are not bytes; without an item-size oracle we
// cannot infer a byte budget.  Conservatively unconstrained.
template <std::size_t N>
struct workload_hint_byte_budget<
    ::crucible::effects::ctx_workload::ItemBudget<N>> {
    static constexpr std::size_t value =
        std::numeric_limits<std::size_t>::max();
};

template <std::size_t Bytes,
          std::size_t Producers,
          std::size_t Consumers,
          bool LatestOnly>
struct workload_hint_byte_budget<
    ::crucible::effects::ctx_workload::ChannelBudget<
        Bytes, Producers, Consumers, LatestOnly>> {
    static constexpr std::size_t value = Bytes;
};

template <std::size_t Bytes, std::size_t Producers, bool LatestOnly>
struct workload_hint_byte_budget<
    ::crucible::effects::ctx_workload::ProducerOnlyChannel<
        Bytes, Producers, LatestOnly>> {
    static constexpr std::size_t value = Bytes;
};

template <std::size_t Bytes, std::size_t Consumers>
struct workload_hint_byte_budget<
    ::crucible::effects::ctx_workload::ConsumerOnlyChannel<
        Bytes, Consumers>> {
    static constexpr std::size_t value = Bytes;
};

template <class WorkloadHint>
inline constexpr std::size_t workload_hint_byte_budget_v =
    workload_hint_byte_budget<WorkloadHint>::value;

// ═════════════════════════════════════════════════════════════════════
// ── alloc_class_max_working_set — per-alloc-class WS ceiling ────────
// ═════════════════════════════════════════════════════════════════════
//
// Stack allocation has a hard practical ceiling: Linux pthread default
// stack is 8 MiB.  Reserving 1 MiB for the call tree (recursive
// callees, libstdc++ buffers, etc.) leaves 7 MiB raw — we declare the
// per-frame WS budget as 1 MiB so a pipeline ≤ 1 MiB safely shares a
// stack with reasonable call-tree depth.  Pipelines above this floor
// MUST use Arena / Pool / HugePage / Heap.
//
// Other alloc classes (Unbound, Arena, Pool, HugePage, Heap) have no
// type-level ceiling — heap is malloc-bound (TB-scale), arena/pool/
// hugepage are configurable at allocation time.

inline constexpr std::size_t stack_alloc_max_working_set_bytes =
    1 * 1024 * 1024;  // 1 MiB safety budget within an 8 MiB pthread stack

template <class AllocClass>
struct alloc_class_max_working_set {
    static constexpr std::size_t value =
        std::numeric_limits<std::size_t>::max();
};

template <>
struct alloc_class_max_working_set<::crucible::effects::ctx_alloc::Stack> {
    static constexpr std::size_t value = stack_alloc_max_working_set_bytes;
};

template <class AllocClass>
inline constexpr std::size_t alloc_class_max_working_set_v =
    alloc_class_max_working_set<AllocClass>::value;

// ═════════════════════════════════════════════════════════════════════
// ── numa_policy_min_working_set — NUMA-spread profitability floor ───
// ═════════════════════════════════════════════════════════════════════
//
// `ctx_numa::Spread` distributes workers across NUMA nodes.  This is
// only profitable when the workload is DRAM-bound — the parallel
// memory channels saturate and cross-socket transfer cost is amortised
// by larger sequential reads on each thread.  Below DRAM tier the
// cross-socket access cost dwarfs the speedup; CLAUDE.md §IX
// cache-tier rule.
//
// Conservative L3 floor: the smallest x86-64 / aarch64 cores we ship
// to (Zen 3 with 32 MiB L3, Sapphire Rapids with 96 MiB L3, Graviton 3
// with 32 MiB L3) all meet the 4 MiB threshold; we use this as the
// type-level minimum for Spread.  A pipeline below 4 MiB paired with
// Spread is a guaranteed regression and a structural contradiction.
//
// Any / Local / Pinned<N> have no minimum WS — those are unconditional
// claims about thread placement, not workload distribution.

inline constexpr std::size_t conservative_l3_total_bytes =
    4 * 1024 * 1024;  // 4 MiB — meet-or-exceed on every supported chip

template <class NumaPolicy>
struct numa_policy_min_working_set {
    static constexpr std::size_t value = 0;
};

template <>
struct numa_policy_min_working_set<::crucible::effects::ctx_numa::Spread> {
    static constexpr std::size_t value = conservative_l3_total_bytes;
};

template <class NumaPolicy>
inline constexpr std::size_t numa_policy_min_working_set_v =
    numa_policy_min_working_set<NumaPolicy>::value;

// ═════════════════════════════════════════════════════════════════════
// ── WorkloadBudgetCoherent<Ctx, Pipeline> ───────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// The load-bearing concept.  Three conjunctive coherence claims — all
// trivially true when the pipeline's aggregate working set is unknown.

template <class Ctx, class Pipeline>
concept WorkloadBudgetCoherent =
    // No measured fact to compare against → admit.
    !Pipeline::aggregate_working_set_known
    ||
    (
        // (1) Workload byte budget admits the pipeline's WS.
        Pipeline::aggregate_per_call_working_set
            <= workload_hint_byte_budget_v<typename Ctx::workload_hint>
        // (2) Alloc class admits the pipeline's WS.
     && Pipeline::aggregate_per_call_working_set
            <= alloc_class_max_working_set_v<typename Ctx::alloc_class>
        // (3) NUMA policy admits the pipeline's WS (Spread floor).
     && Pipeline::aggregate_per_call_working_set
            >= numa_policy_min_working_set_v<typename Ctx::numa_policy>
    );

// ═════════════════════════════════════════════════════════════════════
// ── Smoke checks — type-level sanity for the extractor traits ────────
// ═════════════════════════════════════════════════════════════════════
//
// Concept-level checks live alongside the production fixtures
// (test/test_workload_budget_coherent.cpp + test/effects_neg/...).
// Here we ship just enough to catch a regression at every consumer's
// include time — same pattern as fixy::* self_test sentinels.

namespace workload_budget_coherent_self_test {

// (a) workload_hint_byte_budget extractor — point spot checks.
static_assert(workload_hint_byte_budget_v<
    ::crucible::effects::ctx_workload::Unspecified>
    == std::numeric_limits<std::size_t>::max());

static_assert(workload_hint_byte_budget_v<
    ::crucible::effects::ctx_workload::ByteBudget<4096>> == 4096);

static_assert(workload_hint_byte_budget_v<
    ::crucible::effects::ctx_workload::ChannelBudget<8192, 1, 1, false>>
    == 8192);

static_assert(workload_hint_byte_budget_v<
    ::crucible::effects::ctx_workload::ProducerOnlyChannel<2048, 4, true>>
    == 2048);

static_assert(workload_hint_byte_budget_v<
    ::crucible::effects::ctx_workload::ConsumerOnlyChannel<1024, 2>>
    == 1024);

// ItemBudget is unconstrained (items, not bytes).
static_assert(workload_hint_byte_budget_v<
    ::crucible::effects::ctx_workload::ItemBudget<100>>
    == std::numeric_limits<std::size_t>::max());

// (b) alloc_class_max_working_set extractor — Stack has the only
//     ceiling; other classes are unconstrained.
static_assert(alloc_class_max_working_set_v<
    ::crucible::effects::ctx_alloc::Stack>
    == stack_alloc_max_working_set_bytes);

static_assert(alloc_class_max_working_set_v<
    ::crucible::effects::ctx_alloc::Arena>
    == std::numeric_limits<std::size_t>::max());

static_assert(alloc_class_max_working_set_v<
    ::crucible::effects::ctx_alloc::Pool>
    == std::numeric_limits<std::size_t>::max());

static_assert(alloc_class_max_working_set_v<
    ::crucible::effects::ctx_alloc::HugePage>
    == std::numeric_limits<std::size_t>::max());

static_assert(alloc_class_max_working_set_v<
    ::crucible::effects::ctx_alloc::Heap>
    == std::numeric_limits<std::size_t>::max());

static_assert(alloc_class_max_working_set_v<
    ::crucible::effects::ctx_alloc::Unbound>
    == std::numeric_limits<std::size_t>::max());

// (c) numa_policy_min_working_set extractor — Spread has the only
//     floor; other policies are unconstrained.
static_assert(numa_policy_min_working_set_v<
    ::crucible::effects::ctx_numa::Spread>
    == conservative_l3_total_bytes);

static_assert(numa_policy_min_working_set_v<
    ::crucible::effects::ctx_numa::Any> == 0);

static_assert(numa_policy_min_working_set_v<
    ::crucible::effects::ctx_numa::Local> == 0);

static_assert(numa_policy_min_working_set_v<
    ::crucible::effects::ctx_numa::Pinned<3>> == 0);

}  // namespace workload_budget_coherent_self_test

}  // namespace crucible::concurrent
