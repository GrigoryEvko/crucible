#pragma once

// Translates an execution context's tags into the vocabulary the
// parallelism rule speaks.  Every mapping resolves at instantiation, so
// none of it survives into the runtime call graph.
//
// The bridges live on this side of the boundary because the effect
// layer does not depend on this one, and putting them there would
// invert that.  This layer already knows about the effect layer, so
// adding the surface here costs no new edge.

#include <crucible/concurrent/ParallelismRule.h>
#include <crucible/effects/_ExecCtx.h>

#include <cstddef>
#include <type_traits>

namespace crucible::concurrent {

template <class NumaT>
struct numa_to_policy;

template <>
struct numa_to_policy<::crucible::effects::ctx_numa::Any> {
    static constexpr NumaPolicy value = NumaPolicy::NumaIgnore;
};
template <>
struct numa_to_policy<::crucible::effects::ctx_numa::Local> {
    static constexpr NumaPolicy value = NumaPolicy::NumaLocal;
};
template <>
struct numa_to_policy<::crucible::effects::ctx_numa::Spread> {
    static constexpr NumaPolicy value = NumaPolicy::NumaSpread;
};
template <int Node>
struct numa_to_policy<::crucible::effects::ctx_numa::Pinned<Node>> {
    // Pinning to a node is a special case of staying on one node.  The
    // policy enum has no room for the node itself, which is why the
    // node lookup below exists alongside it.
    static constexpr NumaPolicy value = NumaPolicy::NumaLocal;
};

template <class NumaT>
inline constexpr NumaPolicy numa_to_policy_v = numa_to_policy<NumaT>::value;

// Node ids run from zero.  Minus one stands for the calling thread's
// own node, unnamed, and minus two for no node at all.

template <class NumaT>
struct numa_to_node;
template <>
struct numa_to_node<::crucible::effects::ctx_numa::Any> {
    static constexpr int value = -2;
};
template <>
struct numa_to_node<::crucible::effects::ctx_numa::Local> {
    static constexpr int value = -1;
};
template <>
struct numa_to_node<::crucible::effects::ctx_numa::Spread> {
    static constexpr int value = -2;
};
template <int Node>
struct numa_to_node<::crucible::effects::ctx_numa::Pinned<Node>> {
    static constexpr int value = Node;
};

template <class NumaT>
inline constexpr int numa_node_of_v = numa_to_node<NumaT>::value;

// One to one, since both sides resolve the hierarchy to the same four
// levels.  The residency-heat bridge in the effect layer is a different
// mapping that folds four levels into three.

template <class ResidT>
struct resid_to_tier;
template <>
struct resid_to_tier<::crucible::effects::ctx_resid::L1> {
    static constexpr Tier value = Tier::L1Resident;
};
template <>
struct resid_to_tier<::crucible::effects::ctx_resid::L2> {
    static constexpr Tier value = Tier::L2Resident;
};
template <>
struct resid_to_tier<::crucible::effects::ctx_resid::L3> {
    static constexpr Tier value = Tier::L3Resident;
};
template <>
struct resid_to_tier<::crucible::effects::ctx_resid::DRAM> {
    static constexpr Tier value = Tier::DRAMBound;
};

template <class ResidT>
inline constexpr Tier resid_to_tier_v = resid_to_tier<ResidT>::value;

// An unstated workload becomes a zero budget, which the rule reads as
// no information and answers sequentially.  A byte budget splits evenly
// between reading and writing, which is the common shape.  A caller that
// knows its own split builds the budget itself.  An item budget leaves
// the byte fields at zero, because the item count carries no weight in
// the rule.

template <class WlT>
struct workload_to_budget {
    static constexpr WorkBudget value{};
};

template <std::size_t N>
struct workload_to_budget<::crucible::effects::ctx_workload::ByteBudget<N>> {
    static constexpr WorkBudget value{N / 2, N - N / 2, 0};
};

template <std::size_t N>
struct workload_to_budget<::crucible::effects::ctx_workload::ItemBudget<N>> {
    static constexpr WorkBudget value{0, 0, N};
};

template <std::size_t Bytes, std::size_t Producers, std::size_t Consumers, bool LatestOnly>
struct workload_to_budget<::crucible::effects::ctx_workload::ChannelBudget<Bytes, Producers, Consumers, LatestOnly>> {
    static constexpr WorkBudget value{Bytes / 2, Bytes - Bytes / 2, 0};
};

template <class WlT>
inline constexpr WorkBudget workload_to_budget_v = workload_to_budget<WlT>::value;

template <::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] consteval WorkBudget ctx_workbudget() noexcept {
    return workload_to_budget_v<typename Ctx::workload_hint>;
}

template <::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] consteval NumaPolicy ctx_numa_policy() noexcept {
    return numa_to_policy_v<typename Ctx::numa_policy>;
}

template <::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] consteval int ctx_numa_node() noexcept {
    return numa_node_of_v<typename Ctx::numa_policy>;
}

template <::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] consteval Tier ctx_residency_tier() noexcept {
    return resid_to_tier_v<typename Ctx::residency>;
}

// The effect layer has its own per-axis discrimination concepts.  These
// answer the same questions on this side of the bridge, for code that
// branches on the enums directly, such as a scheduler choosing how to
// bind its workers.

template <class Ctx>
concept IsL1ResidentCtx = ::crucible::effects::IsExecCtx<Ctx> && ctx_residency_tier<Ctx>() == Tier::L1Resident;
template <class Ctx>
concept IsL2ResidentCtx = ::crucible::effects::IsExecCtx<Ctx> && ctx_residency_tier<Ctx>() == Tier::L2Resident;
template <class Ctx>
concept IsL3ResidentCtx = ::crucible::effects::IsExecCtx<Ctx> && ctx_residency_tier<Ctx>() == Tier::L3Resident;
template <class Ctx>
concept IsDRAMBoundCtx = ::crucible::effects::IsExecCtx<Ctx> && ctx_residency_tier<Ctx>() == Tier::DRAMBound;

template <class Ctx>
concept IsNumaIgnoreCtx = ::crucible::effects::IsExecCtx<Ctx> && ctx_numa_policy<Ctx>() == NumaPolicy::NumaIgnore;
template <class Ctx>
concept IsNumaLocalCtx = ::crucible::effects::IsExecCtx<Ctx> && ctx_numa_policy<Ctx>() == NumaPolicy::NumaLocal;
template <class Ctx>
concept IsNumaSpreadCtx = ::crucible::effects::IsExecCtx<Ctx> && ctx_numa_policy<Ctx>() == NumaPolicy::NumaSpread;

// Not consteval: extracting the budget is, but the recommendation
// reads the host's cache sizes and so waits for the first call.

template <::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] inline auto parallelism_decision_for() noexcept {
    return recommend_parallelism(ctx_workbudget<Ctx>());
}

namespace detail::exec_ctx_bridge_self_test {

namespace eff = ::crucible::effects;

static_assert(numa_to_policy_v<eff::ctx_numa::Any> == NumaPolicy::NumaIgnore);
static_assert(numa_to_policy_v<eff::ctx_numa::Local> == NumaPolicy::NumaLocal);
static_assert(numa_to_policy_v<eff::ctx_numa::Spread> == NumaPolicy::NumaSpread);
static_assert(numa_to_policy_v<eff::ctx_numa::Pinned<0>> == NumaPolicy::NumaLocal);
static_assert(numa_to_policy_v<eff::ctx_numa::Pinned<3>> == NumaPolicy::NumaLocal);

static_assert(numa_node_of_v<eff::ctx_numa::Any> == -2);
static_assert(numa_node_of_v<eff::ctx_numa::Local> == -1);
static_assert(numa_node_of_v<eff::ctx_numa::Spread> == -2);
static_assert(numa_node_of_v<eff::ctx_numa::Pinned<0>> == 0);
static_assert(numa_node_of_v<eff::ctx_numa::Pinned<3>> == 3);

static_assert(resid_to_tier_v<eff::ctx_resid::L1> == Tier::L1Resident);
static_assert(resid_to_tier_v<eff::ctx_resid::L2> == Tier::L2Resident);
static_assert(resid_to_tier_v<eff::ctx_resid::L3> == Tier::L3Resident);
static_assert(resid_to_tier_v<eff::ctx_resid::DRAM> == Tier::DRAMBound);

static_assert(workload_to_budget_v<eff::ctx_workload::Unspecified>.read_bytes == 0);
static_assert(workload_to_budget_v<eff::ctx_workload::Unspecified>.write_bytes == 0);
static_assert(workload_to_budget_v<eff::ctx_workload::Unspecified>.item_count == 0);

static_assert(workload_to_budget_v<eff::ctx_workload::ByteBudget<4096>>.read_bytes == 2048);
static_assert(workload_to_budget_v<eff::ctx_workload::ByteBudget<4096>>.write_bytes == 2048);
static_assert(workload_to_budget_v<eff::ctx_workload::ByteBudget<4096>>.item_count == 0);

// An odd byte count loses nothing: the write side takes the extra.
static_assert(workload_to_budget_v<eff::ctx_workload::ByteBudget<7>>.read_bytes == 3);
static_assert(workload_to_budget_v<eff::ctx_workload::ByteBudget<7>>.write_bytes == 4);

static_assert(workload_to_budget_v<eff::ctx_workload::ItemBudget<128>>.read_bytes == 0);
static_assert(workload_to_budget_v<eff::ctx_workload::ItemBudget<128>>.write_bytes == 0);
static_assert(workload_to_budget_v<eff::ctx_workload::ItemBudget<128>>.item_count == 128);
static_assert(workload_to_budget_v<eff::ctx_workload::ChannelBudget<4097, 4, 2, false>>.read_bytes == 2048);
static_assert(workload_to_budget_v<eff::ctx_workload::ChannelBudget<4097, 4, 2, false>>.write_bytes == 2049);

static_assert(ctx_numa_policy<eff::HotFgCtx>() == NumaPolicy::NumaLocal);
static_assert(ctx_numa_node<eff::HotFgCtx>() == -1);
static_assert(ctx_residency_tier<eff::HotFgCtx>() == Tier::L1Resident);
static_assert(ctx_workbudget<eff::HotFgCtx>().read_bytes == 0);

static_assert(ctx_numa_policy<eff::BgDrainCtx>() == NumaPolicy::NumaLocal);
static_assert(ctx_residency_tier<eff::BgDrainCtx>() == Tier::L2Resident);

static_assert(ctx_numa_policy<eff::ColdInitCtx>() == NumaPolicy::NumaSpread);
static_assert(ctx_numa_node<eff::ColdInitCtx>() == -2);
static_assert(ctx_residency_tier<eff::ColdInitCtx>() == Tier::DRAMBound);

// Every axis set to something other than its default.
using MaxCtx =
    eff::ExecCtx<eff::Bg, eff::ctx_numa::Pinned<3>, eff::ctx_alloc::HugePage, eff::ctx_heat::Hot, eff::ctx_resid::L1,
                 eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>,
                 eff::ctx_workload::ByteBudget<2 * 1024 * 1024>>;

static_assert(ctx_numa_policy<MaxCtx>() == NumaPolicy::NumaLocal);
static_assert(ctx_numa_node<MaxCtx>() == 3);
static_assert(ctx_residency_tier<MaxCtx>() == Tier::L1Resident);
static_assert(ctx_workbudget<MaxCtx>().read_bytes == 1024 * 1024);
static_assert(ctx_workbudget<MaxCtx>().write_bytes == 1024 * 1024);

static_assert(IsL1ResidentCtx<eff::HotFgCtx>);
static_assert(!IsL1ResidentCtx<eff::BgDrainCtx>);
static_assert(IsL2ResidentCtx<eff::BgDrainCtx>);
static_assert(IsL2ResidentCtx<eff::BgCompileCtx>);
static_assert(!IsL2ResidentCtx<eff::HotFgCtx>);
static_assert(IsDRAMBoundCtx<eff::ColdInitCtx>);
static_assert(IsDRAMBoundCtx<eff::TestRunnerCtx>);
static_assert(!IsDRAMBoundCtx<eff::HotFgCtx>);

static_assert(IsNumaLocalCtx<eff::HotFgCtx>);
static_assert(IsNumaLocalCtx<eff::BgDrainCtx>);
static_assert(!IsNumaLocalCtx<eff::ColdInitCtx>);
static_assert(IsNumaSpreadCtx<eff::ColdInitCtx>);
static_assert(IsNumaIgnoreCtx<eff::TestRunnerCtx>);
static_assert(!IsNumaIgnoreCtx<eff::HotFgCtx>);

// A pinned context reads as node-local at the policy level.
static_assert(IsNumaLocalCtx<MaxCtx>);

}  // namespace detail::exec_ctx_bridge_self_test

}  // namespace crucible::concurrent
