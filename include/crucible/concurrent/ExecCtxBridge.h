#pragma once

// ── crucible::concurrent::ExecCtxBridge — ExecCtx ↔ concurrent/ ─────
//
// Composition glue between effects::ExecCtx (the universal context
// carrier) and concurrent::ParallelismRule's WorkBudget / NumaPolicy
// / Tier vocabulary.  Every metafunction in this header is a
// consteval lookup: zero runtime cost, cleanly composable.
//
//   Axiom coverage: TypeSafe — each bridge is a 1-1 / 4-1 mapping
//                   from a closed set of effects::ctx_* tags to the
//                   matching concurrent enum value.  Drift on either
//                   side fires a static_assert in the self-test.
//                   DetSafe — all consteval; the runtime call graph
//                   is unchanged.
//   Runtime cost:   zero.  Bridges resolve at template instantiation.
//
// ── Why a separate header ───────────────────────────────────────────
//
// effects/ has historically not depended on concurrent/.  Putting
// the bridges in effects/ would invert that — effects/ would
// suddenly need WorkBudget / NumaPolicy / Tier from concurrent/.
// Putting them in concurrent/ keeps the layering: concurrent/ has
// always known about effects/ (via the WorkBudget conversion
// helpers), and this header just adds the explicit ExecCtx surface.
// Production code that wants the bridge includes this header
// explicitly.
//
// ── Bridges shipped ─────────────────────────────────────────────────
//
//   numa_to_policy_v<NumaT>      → NumaPolicy enum
//   numa_node_of_v<NumaT>        → int (specific node, or -1, -2)
//   resid_to_tier_v<ResidT>      → Tier enum (4-1 mapping; see below)
//   workload_to_budget_v<WlT>    → WorkBudget value
//
// ── Ctx-driven extractors ───────────────────────────────────────────
//
//   ctx_workbudget<Ctx>()        → WorkBudget
//   ctx_numa_policy<Ctx>()       → NumaPolicy
//   ctx_numa_node<Ctx>()         → int
//   ctx_residency_tier<Ctx>()    → Tier
//
// The Ctx-driven helpers are the typical production entry points:
// a function holding a `IsExecCtx Ctx const&` calls
// `ctx_workbudget<Ctx>()` and feeds the result to
// `recommend_parallelism()`.

#include <crucible/concurrent/ParallelismRule.h>
#include <crucible/effects/ExecCtx.h>

#include <cstddef>
#include <type_traits>

namespace crucible::concurrent {

// ── ctx_numa::* → NumaPolicy ────────────────────────────────────────

template <class NumaT> struct numa_to_policy;

template <> struct numa_to_policy<::crucible::effects::ctx_numa::Any> {
    static constexpr NumaPolicy value = NumaPolicy::NumaIgnore;
};
template <> struct numa_to_policy<::crucible::effects::ctx_numa::Local> {
    static constexpr NumaPolicy value = NumaPolicy::NumaLocal;
};
template <> struct numa_to_policy<::crucible::effects::ctx_numa::Spread> {
    static constexpr NumaPolicy value = NumaPolicy::NumaSpread;
};
template <int Node>
struct numa_to_policy<::crucible::effects::ctx_numa::Pinned<Node>> {
    // Pinned<N> is local-to-N.  The NumaPolicy enum doesn't carry
    // the node id; the companion numa_node_of_v<...> exposes it.
    static constexpr NumaPolicy value = NumaPolicy::NumaLocal;
};

template <class NumaT>
inline constexpr NumaPolicy numa_to_policy_v = numa_to_policy<NumaT>::value;

// ── ctx_numa::* → numa node id ──────────────────────────────────────
//
// Conventions:
//   • -1 ⇒ "current thread's home" (Local, but no specific node)
//   • -2 ⇒ "unbound" (Any / Spread)
//   • >=0 ⇒ specific node (Pinned<N>)

template <class NumaT> struct numa_to_node;
template <> struct numa_to_node<::crucible::effects::ctx_numa::Any>    { static constexpr int value = -2; };
template <> struct numa_to_node<::crucible::effects::ctx_numa::Local>  { static constexpr int value = -1; };
template <> struct numa_to_node<::crucible::effects::ctx_numa::Spread> { static constexpr int value = -2; };
template <int Node>
struct numa_to_node<::crucible::effects::ctx_numa::Pinned<Node>> {
    static constexpr int value = Node;
};

template <class NumaT>
inline constexpr int numa_node_of_v = numa_to_node<NumaT>::value;

// ── ctx_resid::* → concurrent::Tier ─────────────────────────────────
//
// 1-1 mapping.  Both axes have identical 4-tier resolutions
// (L1 / L2 / L3 / DRAM).  Distinct from the algebra::lattices::
// ResidencyHeatTag bridge in ExecCtx.h — that one collapses 4→3 to
// match the wrapper's coarser enum.

template <class ResidT> struct resid_to_tier;
template <> struct resid_to_tier<::crucible::effects::ctx_resid::L1>   { static constexpr Tier value = Tier::L1Resident; };
template <> struct resid_to_tier<::crucible::effects::ctx_resid::L2>   { static constexpr Tier value = Tier::L2Resident; };
template <> struct resid_to_tier<::crucible::effects::ctx_resid::L3>   { static constexpr Tier value = Tier::L3Resident; };
template <> struct resid_to_tier<::crucible::effects::ctx_resid::DRAM> { static constexpr Tier value = Tier::DRAMBound;  };

template <class ResidT>
inline constexpr Tier resid_to_tier_v = resid_to_tier<ResidT>::value;

// ── ctx_workload::* → WorkBudget ───────────────────────────────────
//
// Default WorkBudget for Unspecified is all-zero (the cost model
// treats it as "no information; assume cache-resident, run
// sequentially").  ByteBudget<N> splits N bytes evenly between
// read and write — the most common workload shape.  Callers with
// specific R/W info should construct WorkBudget directly.
// ItemBudget<N> sets item_count; WorkBudget's size fields stay 0
// because item_count is informational at the cost-model level.

template <class WlT> struct workload_to_budget {
    static constexpr WorkBudget value{};  // unspecified → all zeros
};

template <std::size_t N>
struct workload_to_budget<::crucible::effects::ctx_workload::ByteBudget<N>> {
    // Split: half read, half write (most common shape).
    static constexpr WorkBudget value{N / 2, N - N / 2, 0};
};

template <std::size_t N>
struct workload_to_budget<::crucible::effects::ctx_workload::ItemBudget<N>> {
    static constexpr WorkBudget value{0, 0, N};
};

template <class WlT>
inline constexpr WorkBudget workload_to_budget_v = workload_to_budget<WlT>::value;

// ── Ctx-driven extractors ──────────────────────────────────────────
//
// The typical production entry points: hold an `IsExecCtx Ctx
// const&`, call `ctx_workbudget<Ctx>()` to get the matching
// WorkBudget, then feed it to recommend_parallelism().

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

// ── Discrimination concepts on the bridged enums ───────────────────
//
// Selective dispatch on a Ctx's residency or NUMA policy, projected
// through the consteval bridges.  These mirror the per-axis
// discrimination concepts in ExecCtx.h (IsHotCtx, IsArenaCtx, etc.)
// but operate on the concurrent::Tier / NumaPolicy enum side, useful
// when downstream code branches on those types directly (e.g.,
// AdaptiveScheduler picks worker-binding strategy from NumaPolicy).

template <class Ctx>
concept IsL1ResidentCtx = ::crucible::effects::IsExecCtx<Ctx>
                       && ctx_residency_tier<Ctx>() == Tier::L1Resident;
template <class Ctx>
concept IsL2ResidentCtx = ::crucible::effects::IsExecCtx<Ctx>
                       && ctx_residency_tier<Ctx>() == Tier::L2Resident;
template <class Ctx>
concept IsL3ResidentCtx = ::crucible::effects::IsExecCtx<Ctx>
                       && ctx_residency_tier<Ctx>() == Tier::L3Resident;
template <class Ctx>
concept IsDRAMBoundCtx  = ::crucible::effects::IsExecCtx<Ctx>
                       && ctx_residency_tier<Ctx>() == Tier::DRAMBound;

template <class Ctx>
concept IsNumaIgnoreCtx = ::crucible::effects::IsExecCtx<Ctx>
                       && ctx_numa_policy<Ctx>() == NumaPolicy::NumaIgnore;
template <class Ctx>
concept IsNumaLocalCtx  = ::crucible::effects::IsExecCtx<Ctx>
                       && ctx_numa_policy<Ctx>() == NumaPolicy::NumaLocal;
template <class Ctx>
concept IsNumaSpreadCtx = ::crucible::effects::IsExecCtx<Ctx>
                       && ctx_numa_policy<Ctx>() == NumaPolicy::NumaSpread;

// ── Parallelism decision from Ctx (one-call ergonomic) ─────────────
//
// The composition closure: given a Ctx, get the runtime parallelism
// decision in one call.  recommend_parallelism is RUNTIME (consults
// Topology singleton) so this wrapper is `inline`, not consteval —
// the WorkBudget extraction is consteval, the actual decision is
// resolved at the first call after Topology is initialized.

template <::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] inline auto parallelism_decision_for() noexcept {
    return recommend_parallelism(ctx_workbudget<Ctx>());
}

// ── Self-test block ─────────────────────────────────────────────────
namespace detail::exec_ctx_bridge_self_test {

namespace eff = ::crucible::effects;

// ── numa_to_policy / numa_node_of pinning ───────────────────────────
static_assert(numa_to_policy_v<eff::ctx_numa::Any>      == NumaPolicy::NumaIgnore);
static_assert(numa_to_policy_v<eff::ctx_numa::Local>    == NumaPolicy::NumaLocal);
static_assert(numa_to_policy_v<eff::ctx_numa::Spread>   == NumaPolicy::NumaSpread);
static_assert(numa_to_policy_v<eff::ctx_numa::Pinned<0>> == NumaPolicy::NumaLocal);
static_assert(numa_to_policy_v<eff::ctx_numa::Pinned<3>> == NumaPolicy::NumaLocal);

static_assert(numa_node_of_v<eff::ctx_numa::Any>       == -2);
static_assert(numa_node_of_v<eff::ctx_numa::Local>     == -1);
static_assert(numa_node_of_v<eff::ctx_numa::Spread>    == -2);
static_assert(numa_node_of_v<eff::ctx_numa::Pinned<0>>  == 0);
static_assert(numa_node_of_v<eff::ctx_numa::Pinned<3>>  == 3);

// ── resid_to_tier pinning ───────────────────────────────────────────
static_assert(resid_to_tier_v<eff::ctx_resid::L1>   == Tier::L1Resident);
static_assert(resid_to_tier_v<eff::ctx_resid::L2>   == Tier::L2Resident);
static_assert(resid_to_tier_v<eff::ctx_resid::L3>   == Tier::L3Resident);
static_assert(resid_to_tier_v<eff::ctx_resid::DRAM> == Tier::DRAMBound);

// ── workload_to_budget pinning ──────────────────────────────────────
static_assert(workload_to_budget_v<eff::ctx_workload::Unspecified>.read_bytes  == 0);
static_assert(workload_to_budget_v<eff::ctx_workload::Unspecified>.write_bytes == 0);
static_assert(workload_to_budget_v<eff::ctx_workload::Unspecified>.item_count  == 0);

static_assert(workload_to_budget_v<eff::ctx_workload::ByteBudget<4096>>.read_bytes  == 2048);
static_assert(workload_to_budget_v<eff::ctx_workload::ByteBudget<4096>>.write_bytes == 2048);
static_assert(workload_to_budget_v<eff::ctx_workload::ByteBudget<4096>>.item_count  ==    0);

// Odd N: split is N/2 read + (N - N/2) write (no rounding loss).
static_assert(workload_to_budget_v<eff::ctx_workload::ByteBudget<7>>.read_bytes  == 3);
static_assert(workload_to_budget_v<eff::ctx_workload::ByteBudget<7>>.write_bytes == 4);

static_assert(workload_to_budget_v<eff::ctx_workload::ItemBudget<128>>.read_bytes  ==   0);
static_assert(workload_to_budget_v<eff::ctx_workload::ItemBudget<128>>.write_bytes ==   0);
static_assert(workload_to_budget_v<eff::ctx_workload::ItemBudget<128>>.item_count  == 128);

// ── Ctx-driven extractors on canonical contexts ─────────────────────

// HotFgCtx is Local + L1, no workload budget.
static_assert(ctx_numa_policy<eff::HotFgCtx>()    == NumaPolicy::NumaLocal);
static_assert(ctx_numa_node<eff::HotFgCtx>()      == -1);
static_assert(ctx_residency_tier<eff::HotFgCtx>() == Tier::L1Resident);
static_assert(ctx_workbudget<eff::HotFgCtx>().read_bytes == 0);

// BgDrainCtx is Local + L2.
static_assert(ctx_numa_policy<eff::BgDrainCtx>()    == NumaPolicy::NumaLocal);
static_assert(ctx_residency_tier<eff::BgDrainCtx>() == Tier::L2Resident);

// ColdInitCtx is Spread + DRAM.
static_assert(ctx_numa_policy<eff::ColdInitCtx>()    == NumaPolicy::NumaSpread);
static_assert(ctx_numa_node<eff::ColdInitCtx>()      == -2);
static_assert(ctx_residency_tier<eff::ColdInitCtx>() == Tier::DRAMBound);

// MaxCtx is Pinned<3> + L1 + ByteBudget<2 MiB>.
using MaxCtx = eff::ExecCtx<
    eff::Bg,
    eff::ctx_numa::Pinned<3>,
    eff::ctx_alloc::HugePage,
    eff::ctx_heat::Hot,
    eff::ctx_resid::L1,
    eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>,
    eff::ctx_workload::ByteBudget<2 * 1024 * 1024>>;

static_assert(ctx_numa_policy<MaxCtx>()    == NumaPolicy::NumaLocal);
static_assert(ctx_numa_node<MaxCtx>()      == 3);
static_assert(ctx_residency_tier<MaxCtx>() == Tier::L1Resident);
static_assert(ctx_workbudget<MaxCtx>().read_bytes  == 1024 * 1024);
static_assert(ctx_workbudget<MaxCtx>().write_bytes == 1024 * 1024);

// ── Discrimination concepts ─────────────────────────────────────────

static_assert( IsL1ResidentCtx<eff::HotFgCtx>);
static_assert(!IsL1ResidentCtx<eff::BgDrainCtx>);
static_assert( IsL2ResidentCtx<eff::BgDrainCtx>);
static_assert( IsL2ResidentCtx<eff::BgCompileCtx>);
static_assert(!IsL2ResidentCtx<eff::HotFgCtx>);
static_assert( IsDRAMBoundCtx<eff::ColdInitCtx>);
static_assert( IsDRAMBoundCtx<eff::TestRunnerCtx>);
static_assert(!IsDRAMBoundCtx<eff::HotFgCtx>);

static_assert( IsNumaLocalCtx<eff::HotFgCtx>);     // Local
static_assert( IsNumaLocalCtx<eff::BgDrainCtx>);    // Local
static_assert(!IsNumaLocalCtx<eff::ColdInitCtx>);   // Spread
static_assert( IsNumaSpreadCtx<eff::ColdInitCtx>);
static_assert( IsNumaIgnoreCtx<eff::TestRunnerCtx>);
static_assert(!IsNumaIgnoreCtx<eff::HotFgCtx>);

// Pinned<N> reports as Local at the policy level (the node id is
// exposed separately via numa_node_of_v).
static_assert( IsNumaLocalCtx<MaxCtx>);

}  // namespace detail::exec_ctx_bridge_self_test

// ── Runtime smoke test ──────────────────────────────────────────────

[[gnu::cold]] inline void runtime_smoke_test_exec_ctx_bridge() noexcept {
    // Drive the Ctx-driven extractors at runtime against canonical
    // aliases.  Confirms the consteval results are usable as runtime
    // values feeding into recommend_parallelism's signature.
    namespace eff = ::crucible::effects;

    [[maybe_unused]] auto fg_budget = ctx_workbudget<eff::HotFgCtx>();
    [[maybe_unused]] auto fg_numa   = ctx_numa_policy<eff::HotFgCtx>();
    [[maybe_unused]] auto fg_node   = ctx_numa_node<eff::HotFgCtx>();
    [[maybe_unused]] auto fg_tier   = ctx_residency_tier<eff::HotFgCtx>();

    [[maybe_unused]] auto bg_budget = ctx_workbudget<eff::BgDrainCtx>();
    [[maybe_unused]] auto bg_numa   = ctx_numa_policy<eff::BgDrainCtx>();
    [[maybe_unused]] auto bg_tier   = ctx_residency_tier<eff::BgDrainCtx>();

    // Feed the WorkBudget into recommend_parallelism — closes the
    // composition loop: a Ctx → WorkBudget → ParallelismDecision.
    [[maybe_unused]] auto decision = recommend_parallelism(
        ctx_workbudget<eff::BgCompileCtx>());
    static_cast<void>(decision);

    // Same composition via the parallelism_decision_for ergonomic
    // wrapper — one-call shorthand.
    [[maybe_unused]] auto bg_decision   = parallelism_decision_for<eff::BgDrainCtx>();
    [[maybe_unused]] auto cold_decision = parallelism_decision_for<eff::ColdInitCtx>();
    static_cast<void>(bg_decision);
    static_cast<void>(cold_decision);

    // Discrimination concepts are usable as runtime gates too — the
    // result is a constexpr bool, so branches fold at -O3.
    static_assert(IsL1ResidentCtx<eff::HotFgCtx>);
    static_assert(IsNumaSpreadCtx<eff::ColdInitCtx>);
}

}  // namespace crucible::concurrent
