#pragma once

// ─── crucible::safety::Workload primitives ───────────────────────────
//
// THE user-facing concurrency layer.  Takes an OwnedRegion<T, Whole>,
// partitions it via OwnedRegion::split_into<N>(), spawns N stack-
// allocated jthread workers (each holding ITS sub-region's
// Permission via [[no_unique_address]]), joins via RAII array
// destructor, rebuilds the parent OwnedRegion, and returns it.
//
// ZERO allocation per submission.  ZERO user-level atomics.  ZERO
// pointer chasing per element (sub-regions are span subranges into
// the same arena buffer).  Compile-time disjointness via Slice<Whole, I>
// tag distinction.
//
// ─── The structural-sync win ────────────────────────────────────────
//
// The std::array<jthread, N> destructor at function epilogue joins
// every worker before parallel_for_views returns.  Per the C++
// memory model, jthread::join provides happens-before.  So:
//
//   * Each worker writes only into its own sub-region's bytes
//     (compile-time-proved disjoint via Permission tags)
//   * jthread destructor ⟹ join ⟹ happens-before
//   * Caller can read the parent OwnedRegion's bytes after return
//     with PLAIN NON-ATOMIC LOADS — well-defined per [intro.races]
//
// No user-level atomics in the body.  No spin loops on peer signals.
// No "is producer done?" exit conditions.  The structural argument
// from `permission_fork` carries over.  This is the primitive that
// SHARDED test's deadlocked exit condition would have prevented.
//
// ─── Cost-model integration ─────────────────────────────────────────
//
// WorkBudget describes the workload's data footprint and per-item
// compute estimate.  parallel_for_views_adaptive consults the
// (placeholder; SEPLOG-C2 fully implements) `should_parallelize`
// predicate.  When the heuristic returns false, we run the body
// SEQUENTIALLY (single-call, no jthread spawn) and skip the
// pthread_create/join overhead entirely.  No regression at small
// workloads.
//
// ─── API surface ────────────────────────────────────────────────────
//
//   parallel_for_views<N>(region, body) -> OwnedRegion<T, Whole>
//     Each worker receives OwnedRegion<T, Slice<Whole, I>>.  Body
//     mutates its sub-region in place.  Returns recombined parent.
//
//   parallel_reduce_views<N, R>(region, init, mapper, reducer)
//                              -> std::pair<R, OwnedRegion<T, Whole>>
//     Each worker computes a partial R via mapper(sub).  After join,
//     main thread reduces partials via reducer(R, R) starting from
//     init.  No shared atomic accumulator.  R returned by value.
//
//   parallel_for_views_adaptive<N>(region, body, budget)
//                              -> OwnedRegion<T, Whole>
//     Cost-model gated.  Same return type; sequential or parallel
//     decided per WorkBudget.
//
// All bodies must be noexcept (Crucible's -fno-exceptions rule).

#include <crucible/Platform.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/Permission.h>

#include <array>
#include <cstddef>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// ── WorkBudget — workload size descriptor ───────────────────────────
//
// Used by the cost-model gate to decide sequential vs parallel.
// SEPLOG-C2 will replace this placeholder with a Topology-aware
// decision; for now, a simple cache-tier heuristic.

struct WorkBudget {
    std::size_t read_bytes           = 0;
    std::size_t write_bytes          = 0;
    std::size_t item_count           = 0;
    std::size_t per_item_compute_ns  = 0;
};

// Placeholder cost-model heuristic — picks parallelism when working
// set exceeds a per-core L2 budget OR when total compute exceeds the
// pthread spawn-amortisation threshold.
//
// Returns true iff parallelism is recommended.  SEPLOG-C2 will
// extend with Topology probe + NUMA-aware decisions.
[[nodiscard, gnu::const]] inline bool
should_parallelize(WorkBudget budget) noexcept {
    // Default tier thresholds (Tiger Lake / typical desktop).
    // These will be replaced by Topology::instance() values in C2.
    constexpr std::size_t L2_per_core_bytes = 1 << 20;   // 1 MB
    constexpr std::size_t spawn_overhead_ns = 200'000;   // ~8 × 25 µs

    const std::size_t ws = budget.read_bytes + budget.write_bytes;
    const std::size_t total_compute_ns =
        budget.item_count * budget.per_item_compute_ns;

    // Cache-tier rule: sequential when L2-resident.
    if (ws < L2_per_core_bytes) return false;
    // Compute-bound rule: parallel only when total work amortises spawn.
    if (total_compute_ns < spawn_overhead_ns) return false;
    return true;
}

namespace detail {

// Spawn one jthread per shard, each capturing its sub-region by move
// and the body by copy.  std::array<jthread, N>'s destructor joins
// all workers in reverse order (which is fine for correctness — all
// must complete before return).
//
// Body is required to be noexcept-invocable with each shard's
// sub-region type.  jthread's stop_token argument is unused.
template <typename Tup, typename Body, std::size_t... Is>
void spawn_workers_(Tup&& subs, Body body, std::index_sequence<Is...>) noexcept
{
    [[maybe_unused]] std::array<std::jthread, sizeof...(Is)> threads = {
        std::jthread{
            [sub = std::move(std::get<Is>(std::forward<Tup>(subs))),
             body](std::stop_token) mutable noexcept {
                body(std::move(sub));
            }
        }...
    };
    // ~std::array runs at scope exit, joining every jthread.
}

// Map-reduce variant: each worker writes its partial result into the
// I-th slot of partials_array.  After join, caller folds the array.
template <typename Tup, typename Mapper, typename PartialArray, std::size_t... Is>
void spawn_workers_with_partials_(Tup&& subs, Mapper mapper,
                                    PartialArray& partials,
                                    std::index_sequence<Is...>) noexcept
{
    [[maybe_unused]] std::array<std::jthread, sizeof...(Is)> threads = {
        std::jthread{
            [sub = std::move(std::get<Is>(std::forward<Tup>(subs))),
             mapper, &slot = partials[Is]]
            (std::stop_token) mutable noexcept {
                slot = mapper(std::move(sub));
            }
        }...
    };
}

}  // namespace detail

// ── parallel_for_views<N> ────────────────────────────────────────────

template <std::size_t N, typename T, typename Whole, typename Body>
[[nodiscard]] OwnedRegion<T, Whole>
parallel_for_views(OwnedRegion<T, Whole>&& region, Body body) noexcept
{
    static_assert(N > 0, "parallel_for_views<N> requires N > 0");
    static_assert(
        std::is_nothrow_invocable_v<Body&,
                                    OwnedRegion<T, Slice<Whole, 0>>&&>,
        "parallel_for_views body must be noexcept-invocable as "
        "void(OwnedRegion<T, Slice<Whole, I>>&&) — typically a generic lambda."
        "  Required by Crucible's -fno-exceptions rule.");

    // Snapshot base+count BEFORE moving the region (split_into consumes it).
    T*                base  = region.data();
    const std::size_t count = region.size();

    if constexpr (N == 1) {
        // Sequential fast path: invoke body inline on the (only)
        // sub-region.  No jthread spawn.
        auto subs = std::move(region).template split_into<1>();
        body(std::move(std::get<0>(subs)));
    } else {
        // Parallel path: split, spawn, RAII-join.
        auto subs = std::move(region).template split_into<N>();
        detail::spawn_workers_(std::move(subs), body,
                                std::make_index_sequence<N>{});
        // ~std::array<jthread, N> joins all workers above; control
        // reaches here only after every worker has completed.
    }

    // Rebuild the parent OwnedRegion.  All sub-region Permissions
    // have been consumed (each worker's lambda destructed its sub-
    // region at body exit).  permission_fork_rebuild_ is sound here
    // by the same structural argument as permission_fork.
    return OwnedRegion<T, Whole>::template rebuild_parent_<Whole>(base, count);
}

// ── parallel_reduce_views<N, R> ──────────────────────────────────────
//
// Map-reduce: each worker computes a partial R via mapper(sub).
// After join, main thread folds the partials via reducer(R, R)
// starting from `init`.  No shared atomic accumulator; partials
// land in a stack-allocated std::array<R, N>.

template <std::size_t N, typename R, typename T, typename Whole,
          typename Mapper, typename Reducer>
[[nodiscard]] std::pair<R, OwnedRegion<T, Whole>>
parallel_reduce_views(OwnedRegion<T, Whole>&& region, R init,
                      Mapper mapper, Reducer reducer) noexcept
{
    static_assert(N > 0, "parallel_reduce_views<N, R> requires N > 0");
    static_assert(
        std::is_nothrow_invocable_r_v<R, Mapper&,
                                       OwnedRegion<T, Slice<Whole, 0>>&&>,
        "Mapper must be noexcept-invocable as R(OwnedRegion<T, Slice<Whole, I>>&&).");
    static_assert(
        std::is_nothrow_invocable_r_v<R, Reducer&, R, R>,
        "Reducer must be noexcept-invocable as R(R, R).");

    T*                base  = region.data();
    const std::size_t count = region.size();

    R result = init;

    if constexpr (N == 1) {
        // Sequential fast path: one mapper call, fold with init.
        auto subs = std::move(region).template split_into<1>();
        result = reducer(result, mapper(std::move(std::get<0>(subs))));
    } else {
        // Stack-allocated partials.  Each worker writes its slot;
        // no atomic needed (workers write disjoint slots).
        std::array<R, N> partials{};
        for (auto& p : partials) p = init;  // initialise in case mapper fails

        auto subs = std::move(region).template split_into<N>();
        detail::spawn_workers_with_partials_(std::move(subs), mapper, partials,
                                              std::make_index_sequence<N>{});
        // ~std::array<jthread, N> joins all workers.

        // Fold the partials on the main thread.  Plain reads —
        // synchronised with worker writes by jthread::join's
        // happens-before.  Order is left-to-right for determinism;
        // reducer should be associative for correctness.
        for (auto const& p : partials) {
            result = reducer(result, p);
        }
    }

    return std::pair<R, OwnedRegion<T, Whole>>{
        std::move(result),
        OwnedRegion<T, Whole>::template rebuild_parent_<Whole>(base, count)
    };
}

// ── parallel_for_views_adaptive<N> ───────────────────────────────────
//
// Cost-model gated.  When should_parallelize(budget) is false, runs
// the body sequentially with a single-shard split — no jthread spawn,
// no pthread overhead.  When true, dispatches to parallel_for_views<N>.
//
// This is the primary user-facing primitive for "I have a workload;
// parallelise it if it's worth parallelising; never regress."

template <std::size_t N, typename T, typename Whole, typename Body>
[[nodiscard]] OwnedRegion<T, Whole>
parallel_for_views_adaptive(OwnedRegion<T, Whole>&& region,
                            Body body,
                            WorkBudget budget) noexcept
{
    if (should_parallelize(budget)) {
        return parallel_for_views<N>(std::move(region), std::move(body));
    }
    // Sequential — split into 1 shard and invoke inline.
    return parallel_for_views<1>(std::move(region), std::move(body));
}

}  // namespace crucible::safety
