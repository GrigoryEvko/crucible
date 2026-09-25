#pragma once

// Catches a context whose cost-model claims its pipeline contradicts.
// The admission check elsewhere asks whether the pipeline engages a
// capability the context forbids.  This asks a different question: are
// the context's declared numbers consistent with what the pipeline
// measurably does?
//
// Three claims a context can make can be contradicted structurally.
//
// A byte budget says the context runs over at most so much data.  A
// pipeline whose per-call working set exceeds it makes every decision
// the budget authorized, from placement to batch size, rest on a figure
// that is wrong.
//
// An allocation class of stack says the frames live on the call stack.
// A pipeline of several megabytes there overflows it.  The pthread
// default stack is eight megabytes and this reserves one for the
// pipeline, leaving the rest as margin for the call tree beneath it.
//
// A NUMA policy of spread says to distribute workers across nodes,
// which pays only once the working set is DRAM-bound.  Below that the
// cross-socket cost swamps the parallel gain, so spread over a small
// working set is a regression by construction.
//
// A pipeline that exposes no static working set admits every context:
// no measurement means no contradiction, not a hidden one.  The same
// goes for a context that declares no budget, allocates anywhere but
// the stack, or places workers by any policy but spread.
//
// The concept is opt-in and gates nothing on its own.  Making it a
// precondition of building a pipeline would reject every context that
// has not yet declared a budget.  A site that has declared one asserts
// on the concept itself.

#include <crucible/effects/_ExecCtx.h>
#include <crucible/concurrent/_WorkingSet.h>

#include <cstddef>
#include <limits>
#include <type_traits>

namespace crucible::concurrent {

// A hint that names no byte count yields the maximum, since a budget
// the context never declared cannot be enforced.

template <class WorkloadHint>
struct workload_hint_byte_budget {
    static constexpr std::size_t value = std::numeric_limits<std::size_t>::max();
};

template <>
struct workload_hint_byte_budget<::crucible::effects::ctx_workload::Unspecified> {
    static constexpr std::size_t value = std::numeric_limits<std::size_t>::max();
};

template <std::size_t N>
struct workload_hint_byte_budget<::crucible::effects::ctx_workload::ByteBudget<N>> {
    static constexpr std::size_t value = N;
};

// Items are not bytes, and nothing here knows how large an item is.
template <std::size_t N>
struct workload_hint_byte_budget<::crucible::effects::ctx_workload::ItemBudget<N>> {
    static constexpr std::size_t value = std::numeric_limits<std::size_t>::max();
};

template <std::size_t Bytes, std::size_t Producers, std::size_t Consumers, bool LatestOnly>
struct workload_hint_byte_budget<
    ::crucible::effects::ctx_workload::ChannelBudget<Bytes, Producers, Consumers, LatestOnly>> {
    static constexpr std::size_t value = Bytes;
};

template <std::size_t Bytes, std::size_t Producers, bool LatestOnly>
struct workload_hint_byte_budget<::crucible::effects::ctx_workload::ProducerOnlyChannel<Bytes, Producers, LatestOnly>> {
    static constexpr std::size_t value = Bytes;
};

template <std::size_t Bytes, std::size_t Consumers>
struct workload_hint_byte_budget<::crucible::effects::ctx_workload::ConsumerOnlyChannel<Bytes, Consumers>> {
    static constexpr std::size_t value = Bytes;
};

template <class WorkloadHint>
inline constexpr std::size_t workload_hint_byte_budget_v = workload_hint_byte_budget<WorkloadHint>::value;

// One eighth of the default pthread stack, leaving the rest for the
// call tree below the pipeline.  Every other allocation class sizes
// itself at allocation time and has no ceiling to state here.

inline constexpr std::size_t stack_alloc_max_working_set_bytes = 1 * 1024 * 1024;

template <class AllocClass>
struct alloc_class_max_working_set {
    static constexpr std::size_t value = std::numeric_limits<std::size_t>::max();
};

template <>
struct alloc_class_max_working_set<::crucible::effects::ctx_alloc::Stack> {
    static constexpr std::size_t value = stack_alloc_max_working_set_bytes;
};

template <class AllocClass>
inline constexpr std::size_t alloc_class_max_working_set_v = alloc_class_max_working_set<AllocClass>::value;

// A working set below the shared-cache floor is certainly not
// DRAM-bound, so spreading it certainly does not pay.  The other
// placement policies say where a thread runs rather than how work
// divides, so they carry no floor.
//
// conservative_l3_total comes from WorkingSet.h, which this header
// already reaches.  It used to be redeclared here under the name
// conservative_l3_total_bytes, holding a different number from the
// definition the residency-tier classifier read, so the same question
// answered differently depending on which spelling a caller reached
// for.

template <class NumaPolicy>
struct numa_policy_min_working_set {
    static constexpr std::size_t value = 0;
};

template <>
struct numa_policy_min_working_set<::crucible::effects::ctx_numa::Spread> {
    static constexpr std::size_t value = conservative_l3_total;
};

template <class NumaPolicy>
inline constexpr std::size_t numa_policy_min_working_set_v = numa_policy_min_working_set<NumaPolicy>::value;

template <class Ctx, class Pipeline>
concept WorkloadBudgetCoherent =
    !Pipeline::aggregate_working_set_known
    || (Pipeline::aggregate_per_call_working_set <= workload_hint_byte_budget_v<typename Ctx::workload_hint>
        && Pipeline::aggregate_per_call_working_set <= alloc_class_max_working_set_v<typename Ctx::alloc_class>
        && Pipeline::aggregate_per_call_working_set >= numa_policy_min_working_set_v<typename Ctx::numa_policy>);

// Enough of a check to catch a regression wherever this header is
// included.  The behavioural cases live in the test tree.

namespace workload_budget_coherent_self_test {

static_assert(workload_hint_byte_budget_v<::crucible::effects::ctx_workload::Unspecified>
              == std::numeric_limits<std::size_t>::max());

static_assert(workload_hint_byte_budget_v<::crucible::effects::ctx_workload::ByteBudget<4096>> == 4096);

static_assert(workload_hint_byte_budget_v<::crucible::effects::ctx_workload::ChannelBudget<8192, 1, 1, false>> == 8192);

static_assert(workload_hint_byte_budget_v<::crucible::effects::ctx_workload::ProducerOnlyChannel<2048, 4, true>>
              == 2048);

static_assert(workload_hint_byte_budget_v<::crucible::effects::ctx_workload::ConsumerOnlyChannel<1024, 2>> == 1024);

static_assert(workload_hint_byte_budget_v<::crucible::effects::ctx_workload::ItemBudget<100>>
              == std::numeric_limits<std::size_t>::max());

// Stack carries the only ceiling.
static_assert(alloc_class_max_working_set_v<::crucible::effects::ctx_alloc::Stack>
              == stack_alloc_max_working_set_bytes);

static_assert(alloc_class_max_working_set_v<::crucible::effects::ctx_alloc::Arena>
              == std::numeric_limits<std::size_t>::max());

static_assert(alloc_class_max_working_set_v<::crucible::effects::ctx_alloc::Pool>
              == std::numeric_limits<std::size_t>::max());

static_assert(alloc_class_max_working_set_v<::crucible::effects::ctx_alloc::HugePage>
              == std::numeric_limits<std::size_t>::max());

static_assert(alloc_class_max_working_set_v<::crucible::effects::ctx_alloc::Heap>
              == std::numeric_limits<std::size_t>::max());

static_assert(alloc_class_max_working_set_v<::crucible::effects::ctx_alloc::Unbound>
              == std::numeric_limits<std::size_t>::max());

// Spread carries the only floor.
// The spread floor and the residency-tier floor are one figure.  This
// is the cell that fails if a second definition is reintroduced.
static_assert(numa_policy_min_working_set_v<::crucible::effects::ctx_numa::Spread> == conservative_l3_total);

static_assert(numa_policy_min_working_set_v<::crucible::effects::ctx_numa::Any> == 0);

static_assert(numa_policy_min_working_set_v<::crucible::effects::ctx_numa::Local> == 0);

static_assert(numa_policy_min_working_set_v<::crucible::effects::ctx_numa::Pinned<3>> == 0);

}  // namespace workload_budget_coherent_self_test

}  // namespace crucible::concurrent
