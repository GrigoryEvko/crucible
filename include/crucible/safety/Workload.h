#pragma once

// Each worker mutates only its own sub-region, and the tag carried by
// that sub-region proves the shards disjoint at compile time.  The
// worker joins establish happens-before.  A caller may therefore read
// the recombined region with plain non-atomic loads, and nothing in
// this file needs a user-level atomic or a spin loop.

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

struct WorkBudget {
    std::size_t read_bytes = 0;
    std::size_t write_bytes = 0;
    std::size_t item_count = 0;  // telemetry only; the parallelism rule reads bytes

    template <typename T>
    [[nodiscard]] static constexpr WorkBudget for_span(std::span<T const> data) noexcept {
        const std::size_t n = data.size();
        const std::size_t bytes = n * sizeof(T);
        return WorkBudget{
            .read_bytes = bytes,
            .write_bytes = bytes,
            .item_count = n,
        };
    }

    template <typename T>
    [[nodiscard]] static constexpr WorkBudget for_span_read_only(std::span<T const> data) noexcept {
        const std::size_t n = data.size();
        return WorkBudget{
            .read_bytes = n * sizeof(T),
            .write_bytes = 0,
            .item_count = n,
        };
    }
};

[[nodiscard]] inline bool should_parallelize(WorkBudget budget) noexcept {
    const crucible::concurrent::WorkBudget cost_budget{
        .read_bytes = budget.read_bytes,
        .write_bytes = budget.write_bytes,
        .item_count = budget.item_count,
    };
    return crucible::concurrent::recommend_parallelism(cost_budget).is_parallel();
}

namespace detail {

template <typename Tup, typename Body, std::size_t... Is>
void spawn_workers_(Tup&& subs, Body body, std::index_sequence<Is...>) noexcept {
    [[maybe_unused]] std::array<std::jthread, sizeof...(Is)> threads = {
        std::jthread{[sub = std::move(std::get<Is>(std::forward<Tup>(subs))), body](std::stop_token) mutable noexcept {
            body(std::move(sub));
        }}...};
    // The array is never read, but its destructor is the join.  Removing
    // it would let the function return with workers still running.
}

template <typename Tup, typename Mapper, typename PartialArray, std::size_t... Is>
void spawn_workers_with_partials_(Tup&& subs, Mapper mapper, PartialArray& partials,
                                  std::index_sequence<Is...>) noexcept {
    [[maybe_unused]] std::array<std::jthread, sizeof...(Is)> threads = {
        std::jthread{[sub = std::move(std::get<Is>(std::forward<Tup>(subs))), mapper,
                      &slot = partials[Is]](std::stop_token) mutable noexcept { slot = mapper(std::move(sub)); }}...};
}

template <typename TupA, typename TupB, typename Body, std::size_t... Is>
void spawn_workers_pair_(TupA&& subs_a, TupB&& subs_b, Body body, std::index_sequence<Is...>) noexcept {
    [[maybe_unused]] std::array<std::jthread, sizeof...(Is)> threads = {
        std::jthread{[sub_a = std::move(std::get<Is>(std::forward<TupA>(subs_a))),
                      sub_b = std::move(std::get<Is>(std::forward<TupB>(subs_b))),
                      body](std::stop_token) mutable noexcept { body(std::move(sub_a), std::move(sub_b)); }}...};
}

}  // namespace detail

template <std::size_t N, typename T, typename Whole, typename Body>
[[nodiscard]] OwnedRegion<T, Whole> parallel_for_views(OwnedRegion<T, Whole>&& region, Body body) noexcept {
    static_assert(N > 0, "parallel_for_views<N> requires N > 0");
    static_assert(std::is_nothrow_invocable_v<Body&, OwnedRegion<T, Slice<Whole, 0>>&&>,
                  "parallel_for_views body must be noexcept-invocable as "
                  "void(OwnedRegion<T, Slice<Whole, I>>&&) — typically a generic lambda."
                  "  Required by Crucible's -fno-exceptions rule.");

    // Snapshot base and count before the move; split_into consumes the region.
    T* base = region.data();
    const std::size_t count = region.size();

    if constexpr (N == 1) {
        auto subs = std::move(region).template split_into<1>();
        body(std::move(std::get<0>(subs)));
    } else {
        static_assert(std::is_copy_constructible_v<Body>, "parallel_for_views<N> body must be CopyConstructible when "
                                                          "N >= 2 — captured by value into each per-worker jthread "
                                                          "lambda.  Move-only callables (capturing unique_ptr, "
                                                          "Linear<T>, etc.) are rejected by this gate.");

        auto subs = std::move(region).template split_into<N>();
        detail::spawn_workers_(std::move(subs), body, std::make_index_sequence<N>{});
        // spawn_workers_ joins before it returns.
    }

    // Every sub-region permission is consumed by the time the workers
    // join, so no live child claim survives the rebuilt parent.
    return OwnedRegion<T, Whole>::template rebuild_parent_<Whole>(base, count);
}

// The fold runs left to right over shard index, so the result is
// deterministic for a fixed N.  It only agrees across different N when
// the reducer is associative over the mapper's outputs.

template <std::size_t N, typename R, typename T, typename Whole, typename Mapper, typename Reducer>
[[nodiscard]] std::pair<R, OwnedRegion<T, Whole>> parallel_reduce_views(OwnedRegion<T, Whole>&& region, R init,
                                                                        Mapper mapper, Reducer reducer) noexcept {
    static_assert(N > 0, "parallel_reduce_views<N, R> requires N > 0");
    static_assert(std::is_nothrow_invocable_r_v<R, Mapper&, OwnedRegion<T, Slice<Whole, 0>>&&>,
                  "Mapper must be noexcept-invocable as R(OwnedRegion<T, Slice<Whole, I>>&&).");
    static_assert(std::is_nothrow_invocable_r_v<R, Reducer&, R, R>, "Reducer must be noexcept-invocable as R(R, R).");

    T* base = region.data();
    const std::size_t count = region.size();

    R result = init;

    if constexpr (N == 1) {
        auto subs = std::move(region).template split_into<1>();
        result = reducer(result, mapper(std::move(std::get<0>(subs))));
    } else {
        static_assert(std::is_copy_constructible_v<Mapper>,
                      "parallel_reduce_views<N, R> mapper must be CopyConstructible "
                      "when N >= 2 — captured by value into each per-worker "
                      "jthread lambda.  Move-only callables are rejected by this "
                      "gate.");

        // Each worker owns one slot, so the writes need no atomic.
        std::array<R, N> partials{};
        for (auto& p : partials)
            p = init;

        auto subs = std::move(region).template split_into<N>();
        detail::spawn_workers_with_partials_(std::move(subs), mapper, partials, std::make_index_sequence<N>{});

        // Plain reads.  The joins inside the call above give
        // happens-before against every worker write.
        for (auto const& p : partials) {
            result = reducer(result, p);
        }
    }

    return std::pair<R, OwnedRegion<T, Whole>>{std::move(result),
                                               OwnedRegion<T, Whole>::template rebuild_parent_<Whole>(base, count)};
}

// W1 and W2 may be the same tag.  Each region carries its own
// permission chain, so shard I of one is still disjoint from shard I of
// the other and the split stays sound.

template <std::size_t N, typename T1, typename W1, typename T2, typename W2, typename Body>
[[nodiscard]] std::pair<OwnedRegion<T1, W1>, OwnedRegion<T2, W2>>
parallel_apply_pair(OwnedRegion<T1, W1>&& region_a, OwnedRegion<T2, W2>&& region_b, Body body) noexcept {
    static_assert(N > 0, "parallel_apply_pair<N> requires N > 0");
    static_assert(std::is_nothrow_invocable_v<Body&, OwnedRegion<T1, Slice<W1, 0>>&&, OwnedRegion<T2, Slice<W2, 0>>&&>,
                  "parallel_apply_pair body must be noexcept-invocable as "
                  "void(OwnedRegion<T1, Slice<W1, I>>&&, OwnedRegion<T2, Slice<W2, I>>&&) "
                  "— typically a generic lambda.  Required by Crucible's "
                  "-fno-exceptions rule.");

    // Equal sizes make the chunk arithmetic identical for both regions,
    // so shard I of each holds the same element count.  Unequal sizes
    // would let the two shard sequences walk past each other.
    CRUCIBLE_ASSERT(region_a.size() == region_b.size());

    // Snapshot base and count before the moves; split_into consumes each.
    T1* base_a = region_a.data();
    const std::size_t count_a = region_a.size();
    T2* base_b = region_b.data();
    const std::size_t count_b = region_b.size();

    if constexpr (N == 1) {
        auto subs_a = std::move(region_a).template split_into<1>();
        auto subs_b = std::move(region_b).template split_into<1>();
        body(std::move(std::get<0>(subs_a)), std::move(std::get<0>(subs_b)));
    } else {
        static_assert(std::is_copy_constructible_v<Body>, "parallel_apply_pair<N> body must be CopyConstructible "
                                                          "when N >= 2 — captured by value into each per-worker "
                                                          "jthread lambda.  Move-only callables are rejected by "
                                                          "this gate.");

        auto subs_a = std::move(region_a).template split_into<N>();
        auto subs_b = std::move(region_b).template split_into<N>();
        detail::spawn_workers_pair_(std::move(subs_a), std::move(subs_b), body, std::make_index_sequence<N>{});
        // spawn_workers_pair_ joins before it returns.
    }

    return std::pair<OwnedRegion<T1, W1>, OwnedRegion<T2, W2>>{
        OwnedRegion<T1, W1>::template rebuild_parent_<W1>(base_a, count_a),
        OwnedRegion<T2, W2>::template rebuild_parent_<W2>(base_b, count_b)};
}

template <std::size_t N, typename T, typename Whole, typename Body>
[[nodiscard]] OwnedRegion<T, Whole> parallel_for_views_adaptive(OwnedRegion<T, Whole>&& region, Body body,
                                                                WorkBudget budget) noexcept {
    if (should_parallelize(budget)) {
        return parallel_for_views<N>(std::move(region), std::move(body));
    }
    return parallel_for_views<1>(std::move(region), std::move(body));
}

template <typename T, typename Whole, typename Body>
[[nodiscard]] OwnedRegion<T, Whole> parallel_for_smart(OwnedRegion<T, Whole>&& region, Body body) noexcept {
    const auto data = region.cspan();
    const crucible::concurrent::WorkBudget cost_budget{
        .read_bytes = data.size() * sizeof(T),
        .write_bytes = data.size() * sizeof(T),
        .item_count = data.size(),
    };

    const auto decision = crucible::concurrent::recommend_parallelism(cost_budget);

    // The cost model snaps the factor to 1, 2, 4, 8 or 16, and reports a
    // sequential decision as factor 1.  The default arm covers factor 1
    // and any value outside the ladder.
    switch (decision.factor) {
        case 16:
            return parallel_for_views<16>(std::move(region), std::move(body));
        case 8:
            return parallel_for_views<8>(std::move(region), std::move(body));
        case 4:
            return parallel_for_views<4>(std::move(region), std::move(body));
        case 2:
            return parallel_for_views<2>(std::move(region), std::move(body));
        default:
            return parallel_for_views<1>(std::move(region), std::move(body));
    }
}

// The topology probe is otherwise lazy, deferred to the first
// parallelism decision.  Calling this at startup forces it.

inline void log_topology_at_startup(FILE* out = stderr) noexcept {
    crucible::concurrent::Topology::instance().log_summary(out);
}

}  // namespace crucible::safety
