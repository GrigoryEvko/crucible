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
#include <crucible/concurrent/ParallelismRule.h>
#include <crucible/concurrent/Topology.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/permissions/Permission.h>

#include <array>
#include <cstddef>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// ── WorkBudget — workload size descriptor ───────────────────────────
//
// Used by the parallelism-rule gate to decide sequential vs parallel.
// Per 27_04 §5.7 cleanup: ONLY concrete hardware facts (bytes +
// item count for telemetry).  No abstract cost dimensions (no
// per-item nanoseconds, no FLOP intensity).  The cost-model rule
// is purely cache-driven — see concurrent/ParallelismRule.h.

struct WorkBudget {
    std::size_t read_bytes  = 0;
    std::size_t write_bytes = 0;
    std::size_t item_count  = 0;  // informational; not consulted by the rule

    // ── Convenience constructors for the 95% case ──────────────────
    //
    // for_span(span) — auto-derive byte counts from a typed span.
    // Assumes read+write (in-place mutation) by default; override
    // read_bytes / write_bytes after if access is read-only or
    // write-only.
    template <typename T>
    [[nodiscard]] static constexpr WorkBudget
    for_span(std::span<T const> data) noexcept {
        const std::size_t n = data.size();
        const std::size_t bytes = n * sizeof(T);
        return WorkBudget{
            .read_bytes  = bytes,
            .write_bytes = bytes,
            .item_count  = n,
        };
    }

    // Read-only variant — for when the body only reads (parallel_reduce).
    template <typename T>
    [[nodiscard]] static constexpr WorkBudget
    for_span_read_only(std::span<T const> data) noexcept {
        const std::size_t n = data.size();
        return WorkBudget{
            .read_bytes  = n * sizeof(T),
            .write_bytes = 0,
            .item_count  = n,
        };
    }
};

// Cost-model heuristic — picks parallelism per the cache-driven
// decision table in concurrent/ParallelismRule.h (purely cache-driven
// since the 27_04 §5.7 cleanup; no per-item compute hint).
//
// Returns true iff the recommendation is Parallel.  For finer-
// grained decisions (factor + NUMA policy), call
// concurrent::recommend_parallelism(budget) directly.
[[nodiscard]] inline bool
should_parallelize(WorkBudget budget) noexcept {
    const crucible::concurrent::WorkBudget cost_budget{
        .read_bytes  = budget.read_bytes,
        .write_bytes = budget.write_bytes,
        .item_count  = budget.item_count,
    };
    return crucible::concurrent::recommend_parallelism(cost_budget).is_parallel();
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
//
// ── API contract (FOUND-F01 audit, locked-in 2026-04-30) ────────
//
//   * N == 1 fast path: no jthread spawn; body invoked inline on
//                       the whole region (single-shard).
//   * N >= 2:           N jthreads spawn, each invoking body on its
//                       own sub-region; RAII join via std::array
//                       destructor before recombine.
//
// Body invocation count: exactly N for any N >= 1 (one per
// sub-region).  Workers see disjoint shards (compile-time-proved
// by Slice<Whole, I> tag distinction).
//
// Body must be noexcept-invocable per the static_assert; throwing
// across thread boundaries violates -fno-exceptions and triggers
// std::terminate inside the worker jthread.
//
// Body must be CopyConstructible: `spawn_workers_` captures body
// by value into each jthread's lambda (one copy per worker).  Most
// lambdas are CopyConstructible by default; capturing a move-only
// type (unique_ptr, Linear<T>) makes the body itself move-only
// and triggers an "use of deleted function" diagnostic deep inside
// std::jthread machinery.  Documented here so the diagnostic
// surface is predictable.
//
// DetSafe: workers write to disjoint shards; the recombined
// OwnedRegion's content is deterministic in the body's writes.
// Worker scheduling order does NOT affect the result because
// shards are disjoint (no inter-shard read-after-write).
//
// The recombined OwnedRegion's bytes equal the body's writes per
// shard, in canonical sub-region index order — ready for further
// parallel_for_views / parallel_reduce_views chaining.

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
        // sub-region.  No jthread spawn.  Body is consumed by direct
        // call; copy-constructibility is NOT required here.
        auto subs = std::move(region).template split_into<1>();
        body(std::move(std::get<0>(subs)));
    } else {
        // Parallel path: spawn_workers_ captures body BY VALUE into
        // each per-worker jthread lambda (one copy per worker, N total).
        // Lift the contract from the spawn_workers_ depths to the API
        // boundary so a move-only body (capturing unique_ptr, Linear<T>,
        // etc.) yields a clean diagnostic naming the contract instead
        // of a deep std::__invoke_impl / std::jthread error.
        static_assert(std::is_copy_constructible_v<Body>,
            "parallel_for_views<N> body must be CopyConstructible when "
            "N >= 2 — captured by value into each per-worker jthread "
            "lambda.  Move-only callables (capturing unique_ptr, "
            "Linear<T>, etc.) are rejected by this gate.");

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
//
// ── API contract (FOUND-F04 audit, locked-in 2026-04-30) ────────
//
// Result identity (independent of N):
//
//   result == reducer(... reducer(reducer(init, mapper(s_0)),
//                                          mapper(s_1)) ...,
//                                          mapper(s_{N-1}))
//
// where s_0, ..., s_{N-1} are the sub-regions produced by
// `region.split_into<N>()` in INDEX ORDER.
//
//   * N == 1 fast path: no jthread spawn; result is exactly
//                       reducer(init, mapper(whole_region)).
//   * N >= 2:           N jthreads spawn, each writes mapper(s_I)
//                       into partials[I]; main thread folds left-to-
//                       right starting from init after RAII join.
//
// DetSafe: the fold order is fixed by the index_sequence; reducer
// must be associative for correctness, but commutativity is NOT
// required — the API contract is "left-to-right fold, deterministic".
//
// The mapper is invoked EXACTLY N times (one per sub-region).  No
// shared accumulator; per-shard writes target disjoint slots in
// `partials`.
//
// The recombined OwnedRegion's bytes are bit-identical to the
// input — `parallel_reduce_views` does not mutate the buffer.  A
// follow-on consumer can read the same data.
//
// Mapper and Reducer must both be noexcept-invocable per the static
// asserts; bodies that throw violate Crucible's -fno-exceptions rule.
//
// Mapper must be CopyConstructible: `spawn_workers_with_partials_`
// captures mapper by value into each jthread's lambda (one copy per
// worker, N copies total when N >= 2).  A move-only mapper
// (capturing unique_ptr, Linear<T>, etc.) triggers an
// "use of deleted function" diagnostic inside std::jthread
// machinery.  Reducer is used by reference in the post-join fold
// loop, so it does NOT require copy-constructibility beyond the
// initial parameter binding (which itself can move).
//
// R must be CopyConstructible AND CopyAssignable.  The N>=2 path
// initialises std::array<R, N> partials with `for (auto& p : partials)
// p = init;` (copy-assignment) and the post-join fold writes
// `result = reducer(result, p)` — both require non-deleted copy
// operations on R.  Move-only R types are not supported by the
// current API; if a move-only R becomes desirable (e.g. R holding a
// unique_ptr or Linear<T>), the implementation will need a
// std::move-aware fold loop and a partials initialiser that
// move-constructs from a thunked `init`.  Documented as a future
// API evolution; the current contract is "R copyable".
//
// ── Relationship to FOUND-D14 Reduction concept ─────────────────
//
// The D14 Reduction concept (safety/Reduction.h) describes a
// SINGLE-WORKER function shape:
//
//   void f(OwnedRegion<T, Tag>&& input, reduce_into<R, Op>& acc);
//
// — a function that consumes one input region and folds into a
// borrowed reduce_into accumulator.  `parallel_reduce_views<N, R>`
// is the multi-worker dispatcher: it spawns N workers, each running
// what is morally a Reduction-shaped per-worker step (with the
// reducer baked into the per-worker mapper return), then folds the
// N partials on the main thread.  A future API evolution may add a
// `parallel_reduce_views_into(region, reduce_into<R, Op>&)` overload
// that takes the accumulator directly per D14, but the current
// (init, mapper, reducer) shape is sufficient and stable for
// existing call sites.

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
        // Mapper is consumed by direct call; copy-constructibility is
        // NOT required here.
        auto subs = std::move(region).template split_into<1>();
        result = reducer(result, mapper(std::move(std::get<0>(subs))));
    } else {
        // Parallel path: spawn_workers_with_partials_ captures mapper
        // BY VALUE into each per-worker jthread lambda (one copy per
        // worker, N copies total).  Lift the contract from the
        // spawn_workers_with_partials_ depths to the API boundary so a
        // move-only mapper yields a clean diagnostic naming the
        // contract instead of a deep std::__invoke_impl / std::jthread
        // error.  Reducer is used by reference in the post-join fold,
        // so it does NOT require copy-constructibility.
        static_assert(std::is_copy_constructible_v<Mapper>,
            "parallel_reduce_views<N, R> mapper must be CopyConstructible "
            "when N >= 2 — captured by value into each per-worker "
            "jthread lambda.  Move-only callables are rejected by this "
            "gate.");

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

// ── parallel_for_smart — the one-call dispatch ──────────────────────
//
// THE 95%-case API.  Auto-derives WorkBudget from the region's span,
// consults concurrent::recommend_parallelism for tier-aware decision
// (L1/L2 → sequential, L3 → ≤4 NUMA-local, DRAM → up to 16
// NUMA-spread), then dispatches to the matching parallel_for_views<N>.
// Caller passes only:
//
//   * the OwnedRegion to mutate
//   * the noexcept body lambda
//
// The factor ladder snap (1, 2, 4, 8, 16) lives in the cost model;
// this function dispatches via switch.  AdaptiveScheduler (SEPLOG-C3)
// will replace the switch with NUMA-local thread-pool dispatch using
// the same decision struct.
//
// Always returns the recombined OwnedRegion; never throws; preserves
// the no-regression guarantee.  Per 27_04 §5.7: the cost model is
// purely cache-driven; no per-item ns hint accepted.

template <typename T, typename Whole, typename Body>
[[nodiscard]] OwnedRegion<T, Whole>
parallel_for_smart(OwnedRegion<T, Whole>&& region,
                   Body body) noexcept
{
    const auto data = region.cspan();
    const crucible::concurrent::WorkBudget cost_budget{
        .read_bytes  = data.size() * sizeof(T),
        .write_bytes = data.size() * sizeof(T),
        .item_count  = data.size(),
    };

    const auto decision =
        crucible::concurrent::recommend_parallelism(cost_budget);

    // Dispatch on the snapped factor.  decision.kind == Sequential
    // iff factor == 1 (per CostModel rounding rule).
    switch (decision.factor) {
        case 16: return parallel_for_views<16>(std::move(region), std::move(body));
        case 8:  return parallel_for_views<8>(std::move(region), std::move(body));
        case 4:  return parallel_for_views<4>(std::move(region), std::move(body));
        case 2:  return parallel_for_views<2>(std::move(region), std::move(body));
        default: return parallel_for_views<1>(std::move(region), std::move(body));
    }
}

// ── Startup logging hook ────────────────────────────────────────────
//
// Forces the Topology::instance() probe AND emits a one-screen
// human-readable summary to stderr (or supplied FILE*).  Intended
// for Keeper / Vessel / test startup paths; the Topology probe is
// otherwise lazy (deferred to first should_parallelize call).
//
// Use:  crucible::safety::log_topology_at_startup();
//   or:  crucible::safety::log_topology_at_startup(my_log_fp);

inline void log_topology_at_startup(FILE* out = stderr) noexcept {
    crucible::concurrent::Topology::instance().log_summary(out);
}

}  // namespace crucible::safety
