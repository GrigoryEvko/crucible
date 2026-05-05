#pragma once

// crucible::concurrent::AutoRouter
//
// Type-level routing policy for permissioned concurrency primitives.  The
// router turns four compile-time facts into an underlying Permissioned*
// substrate:
//
//   * semantic intent: ordered stream, latest-value broadcast, shardable
//     bulk work, or variable-cost work
//   * producer cardinality
//   * consumer cardinality
//   * byte footprint of the unit being routed
//
// The policy is intentionally conservative: ordered streams remain ordered
// even above the L2/core cliff; only callers that declare Shardable may be
// routed into the MxN sharded SPSC grid.
//
// Runtime cost: zero.  All routing is consteval / inline constexpr.  The
// selected route is an alias to an existing permissioned primitive, with no
// wrapper allocation, vtable, branch, heap allocation, or type erasure.

#include <crucible/concurrent/PermissionedShardedGrid.h>
#include <crucible/concurrent/Substrate.h>

#include <cstddef>
#include <cstdint>

namespace crucible::concurrent {

enum class RouteIntent : std::uint8_t {
    Stream,
    Latest,
    Shardable,
    VariableCost,
};

enum class RouteKind : std::uint8_t {
    Spsc,
    Mpsc,
    Snapshot,
    Mpmc,
    WorkStealing,
    ShardedGrid,
};

struct AutoRouteDecision {
    RouteKind       kind;
    RouteIntent     intent;
    ChannelTopology topology;
    std::size_t     producers;
    std::size_t     consumers;
    std::size_t     workload_bytes;
    std::size_t     shard_factor;
    bool            sharded;
    bool            latest_only;
};

namespace detail {

template <ChannelTopology Topology, typename T>
struct payload_fits_topology : std::false_type {};

template <typename T>
struct payload_fits_topology<ChannelTopology::OneToOne, T>
    : std::bool_constant<SpscValue<T>> {};

template <typename T>
struct payload_fits_topology<ChannelTopology::ManyToOne, T>
    : std::bool_constant<RingValue<T>> {};

template <typename T>
struct payload_fits_topology<ChannelTopology::OneToMany_Latest, T>
    : std::bool_constant<SnapshotValue<T>> {};

template <typename T>
struct payload_fits_topology<ChannelTopology::ManyToMany, T>
    : std::bool_constant<MpmcValue<T>> {};

template <typename T>
struct payload_fits_topology<ChannelTopology::WorkStealing, T>
    : std::bool_constant<DequeValue<T>> {};

template <ChannelTopology Topology, typename T>
inline constexpr bool payload_fits_topology_v =
    payload_fits_topology<Topology, T>::value;

[[nodiscard]] consteval RouteKind route_kind_from_topology(
    ChannelTopology topology) noexcept {
    switch (topology) {
    case ChannelTopology::OneToOne:         return RouteKind::Spsc;
    case ChannelTopology::ManyToOne:        return RouteKind::Mpsc;
    case ChannelTopology::OneToMany_Latest: return RouteKind::Snapshot;
    case ChannelTopology::ManyToMany:       return RouteKind::Mpmc;
    case ChannelTopology::WorkStealing:     return RouteKind::WorkStealing;
    default:                                return RouteKind::Spsc;
    }
}

template <std::size_t A, std::size_t B>
inline constexpr std::size_t min_size_v = A < B ? A : B;

[[nodiscard]] constexpr std::size_t min_size(std::size_t a,
                                             std::size_t b) noexcept {
    return a < b ? a : b;
}

[[nodiscard]] constexpr ChannelTopology recommend_topology_runtime(
    std::size_t num_producers,
    std::size_t num_consumers,
    bool        latest_only = false) noexcept {
    if (num_producers == 1 && num_consumers == 1) return ChannelTopology::OneToOne;
    if (num_producers >  1 && num_consumers == 1) return ChannelTopology::ManyToOne;
    if (num_producers == 1 && num_consumers >  1 && latest_only)
        return ChannelTopology::OneToMany_Latest;
    if (num_producers >  1 && num_consumers >  1) return ChannelTopology::ManyToMany;
    if (num_producers == 1 && num_consumers >  1) return ChannelTopology::ManyToMany;
    return ChannelTopology::OneToOne;
}

[[nodiscard]] constexpr RouteKind route_kind_from_topology_runtime(
    ChannelTopology topology) noexcept {
    switch (topology) {
    case ChannelTopology::OneToOne:         return RouteKind::Spsc;
    case ChannelTopology::ManyToOne:        return RouteKind::Mpsc;
    case ChannelTopology::OneToMany_Latest: return RouteKind::Snapshot;
    case ChannelTopology::ManyToMany:       return RouteKind::Mpmc;
    case ChannelTopology::WorkStealing:     return RouteKind::WorkStealing;
    default:                                return RouteKind::Spsc;
    }
}

}  // namespace detail

template <std::size_t WorkloadBytes, std::size_t MaxShards = 16>
[[nodiscard]] consteval std::size_t auto_shard_factor() noexcept {
    static_assert(MaxShards > 0, "AutoRouter requires MaxShards > 0");

    constexpr std::size_t kSixteenMiB = 16ULL * 1024ULL * 1024ULL;

    if constexpr (WorkloadBytes <= conservative_cliff_l2_per_core) {
        return 1;
    } else if constexpr (WorkloadBytes <= kSixteenMiB) {
        return detail::min_size_v<MaxShards, 4>;
    } else {
        return detail::min_size_v<MaxShards, 16>;
    }
}

template <RouteIntent Intent,
          std::size_t Producers,
          std::size_t Consumers,
          std::size_t WorkloadBytes,
          std::size_t MaxShards = 16>
[[nodiscard]] consteval AutoRouteDecision auto_route_decision() noexcept {
    static_assert(Producers > 0, "AutoRouter requires Producers > 0");
    static_assert(Consumers > 0, "AutoRouter requires Consumers > 0");
    static_assert(MaxShards > 0, "AutoRouter requires MaxShards > 0");

    if constexpr (Intent == RouteIntent::Latest) {
        return AutoRouteDecision{
            .kind           = RouteKind::Snapshot,
            .intent         = Intent,
            .topology       = ChannelTopology::OneToMany_Latest,
            .producers      = Producers,
            .consumers      = Consumers,
            .workload_bytes = WorkloadBytes,
            .shard_factor   = 1,
            .sharded        = false,
            .latest_only    = true,
        };
    } else if constexpr (Intent == RouteIntent::VariableCost) {
        return AutoRouteDecision{
            .kind           = RouteKind::WorkStealing,
            .intent         = Intent,
            .topology       = ChannelTopology::WorkStealing,
            .producers      = Producers,
            .consumers      = Consumers,
            .workload_bytes = WorkloadBytes,
            .shard_factor   = 1,
            .sharded        = false,
            .latest_only    = false,
        };
    } else if constexpr (Intent == RouteIntent::Shardable
                         && WorkloadBytes > conservative_cliff_l2_per_core) {
        return AutoRouteDecision{
            .kind           = RouteKind::ShardedGrid,
            .intent         = Intent,
            .topology       = ChannelTopology::ManyToMany,
            .producers      = Producers,
            .consumers      = Consumers,
            .workload_bytes = WorkloadBytes,
            .shard_factor   = auto_shard_factor<WorkloadBytes, MaxShards>(),
            .sharded        = true,
            .latest_only    = false,
        };
    } else {
        constexpr ChannelTopology topology =
            recommend_topology(Producers, Consumers, false);
        return AutoRouteDecision{
            .kind           = detail::route_kind_from_topology(topology),
            .intent         = Intent,
            .topology       = topology,
            .producers      = Producers,
            .consumers      = Consumers,
            .workload_bytes = WorkloadBytes,
            .shard_factor   = 1,
            .sharded        = false,
            .latest_only    = false,
        };
    }
}

template <RouteIntent Intent,
          std::size_t Producers,
          std::size_t Consumers,
          std::size_t WorkloadBytes,
          std::size_t MaxShards = 16>
inline constexpr AutoRouteDecision auto_route_v =
    auto_route_decision<Intent, Producers, Consumers, WorkloadBytes, MaxShards>();

struct AutoRouteRuntimeProfile {
    std::size_t l2_per_core_bytes = conservative_cliff_l2_per_core;
    std::size_t huge_bytes = 16ULL * 1024ULL * 1024ULL;
    std::size_t medium_shards = 4;
    std::size_t huge_shards = 16;
};

[[nodiscard]] constexpr std::size_t auto_shard_factor_runtime(
    std::size_t workload_bytes,
    std::size_t max_shards = 16,
    AutoRouteRuntimeProfile profile = {}) noexcept {
    if (max_shards == 0) return 1;

    const std::size_t l2_cliff =
        profile.l2_per_core_bytes != 0
            ? profile.l2_per_core_bytes
            : conservative_cliff_l2_per_core;
    const std::size_t huge_threshold =
        profile.huge_bytes != 0 ? profile.huge_bytes : 16ULL * 1024ULL * 1024ULL;
    const std::size_t medium_shards =
        profile.medium_shards != 0 ? profile.medium_shards : 4;
    const std::size_t huge_shards =
        profile.huge_shards != 0 ? profile.huge_shards : 16;

    if (workload_bytes <= l2_cliff) {
        return 1;
    }
    if (workload_bytes <= huge_threshold) {
        return detail::min_size(max_shards, medium_shards);
    }
    return detail::min_size(max_shards, huge_shards);
}

[[nodiscard]] constexpr AutoRouteDecision auto_route_decision_runtime(
    RouteIntent intent,
    std::size_t producers,
    std::size_t consumers,
    std::size_t workload_bytes,
    std::size_t max_shards = 16,
    AutoRouteRuntimeProfile profile = {}) noexcept {
    if (intent == RouteIntent::Latest) {
        return AutoRouteDecision{
            .kind           = RouteKind::Snapshot,
            .intent         = intent,
            .topology       = ChannelTopology::OneToMany_Latest,
            .producers      = producers,
            .consumers      = consumers,
            .workload_bytes = workload_bytes,
            .shard_factor   = 1,
            .sharded        = false,
            .latest_only    = true,
        };
    }
    if (intent == RouteIntent::VariableCost) {
        return AutoRouteDecision{
            .kind           = RouteKind::WorkStealing,
            .intent         = intent,
            .topology       = ChannelTopology::WorkStealing,
            .producers      = producers,
            .consumers      = consumers,
            .workload_bytes = workload_bytes,
            .shard_factor   = 1,
            .sharded        = false,
            .latest_only    = false,
        };
    }

    const std::size_t l2_cliff =
        profile.l2_per_core_bytes != 0
            ? profile.l2_per_core_bytes
            : conservative_cliff_l2_per_core;
    if (intent == RouteIntent::Shardable && workload_bytes > l2_cliff) {
        return AutoRouteDecision{
            .kind           = RouteKind::ShardedGrid,
            .intent         = intent,
            .topology       = ChannelTopology::ManyToMany,
            .producers      = producers,
            .consumers      = consumers,
            .workload_bytes = workload_bytes,
            .shard_factor   = auto_shard_factor_runtime(workload_bytes,
                                                        max_shards,
                                                        profile),
            .sharded        = true,
            .latest_only    = false,
        };
    }

    const ChannelTopology topology =
        detail::recommend_topology_runtime(producers, consumers, false);
    return AutoRouteDecision{
        .kind           = detail::route_kind_from_topology_runtime(topology),
        .intent         = intent,
        .topology       = topology,
        .producers      = producers,
        .consumers      = consumers,
        .workload_bytes = workload_bytes,
        .shard_factor   = 1,
        .sharded        = false,
        .latest_only    = false,
    };
}

namespace detail {

template <RouteIntent Intent,
          std::size_t Producers,
          std::size_t Consumers,
          std::size_t WorkloadBytes,
          std::size_t MaxShards>
struct StaticAutoRouteDecisionImpl {
    static constexpr ChannelTopology topology =
        recommend_topology(Producers, Consumers, false);
    static constexpr AutoRouteDecision value = AutoRouteDecision{
        .kind           = route_kind_from_topology(topology),
        .intent         = Intent,
        .topology       = topology,
        .producers      = Producers,
        .consumers      = Consumers,
        .workload_bytes = WorkloadBytes,
        .shard_factor   = 1,
        .sharded        = false,
        .latest_only    = false,
    };
};

template <std::size_t Producers,
          std::size_t Consumers,
          std::size_t WorkloadBytes,
          std::size_t MaxShards>
struct StaticAutoRouteDecisionImpl<RouteIntent::Latest,
                                   Producers,
                                   Consumers,
                                   WorkloadBytes,
                                   MaxShards> {
    static constexpr AutoRouteDecision value = AutoRouteDecision{
        .kind           = RouteKind::Snapshot,
        .intent         = RouteIntent::Latest,
        .topology       = ChannelTopology::OneToMany_Latest,
        .producers      = Producers,
        .consumers      = Consumers,
        .workload_bytes = WorkloadBytes,
        .shard_factor   = 1,
        .sharded        = false,
        .latest_only    = true,
    };
};

template <std::size_t Producers,
          std::size_t Consumers,
          std::size_t WorkloadBytes,
          std::size_t MaxShards>
struct StaticAutoRouteDecisionImpl<RouteIntent::VariableCost,
                                   Producers,
                                   Consumers,
                                   WorkloadBytes,
                                   MaxShards> {
    static constexpr AutoRouteDecision value = AutoRouteDecision{
        .kind           = RouteKind::WorkStealing,
        .intent         = RouteIntent::VariableCost,
        .topology       = ChannelTopology::WorkStealing,
        .producers      = Producers,
        .consumers      = Consumers,
        .workload_bytes = WorkloadBytes,
        .shard_factor   = 1,
        .sharded        = false,
        .latest_only    = false,
    };
};

template <bool Sharded,
          std::size_t Producers,
          std::size_t Consumers,
          std::size_t WorkloadBytes,
          std::size_t MaxShards>
struct StaticShardableDecisionImpl
    : StaticAutoRouteDecisionImpl<RouteIntent::Shardable,
                                  Producers,
                                  Consumers,
                                  WorkloadBytes,
                                  MaxShards> {};

template <std::size_t Producers,
          std::size_t Consumers,
          std::size_t WorkloadBytes,
          std::size_t MaxShards>
struct StaticShardableDecisionImpl<true,
                                   Producers,
                                   Consumers,
                                   WorkloadBytes,
                                   MaxShards> {
    static constexpr AutoRouteDecision value = AutoRouteDecision{
        .kind           = RouteKind::ShardedGrid,
        .intent         = RouteIntent::Shardable,
        .topology       = ChannelTopology::ManyToMany,
        .producers      = Producers,
        .consumers      = Consumers,
        .workload_bytes = WorkloadBytes,
        .shard_factor   = auto_shard_factor<WorkloadBytes, MaxShards>(),
        .sharded        = true,
        .latest_only    = false,
    };
};

template <std::size_t Producers,
          std::size_t Consumers,
          std::size_t WorkloadBytes,
          std::size_t MaxShards>
struct StaticAutoRouteDecisionImpl<RouteIntent::Shardable,
                                   Producers,
                                   Consumers,
                                   WorkloadBytes,
                                   MaxShards>
    : StaticShardableDecisionImpl<
          (WorkloadBytes > conservative_cliff_l2_per_core),
          Producers,
          Consumers,
          WorkloadBytes,
          MaxShards> {};

template <RouteIntent Intent,
          typename T,
          std::size_t Capacity,
          typename UserTag,
          std::size_t Producers,
          std::size_t Consumers,
          std::size_t WorkloadBytes,
          std::size_t MaxShards>
struct AutoRouteImpl;

template <bool Sharded,
          typename T,
          std::size_t Capacity,
          typename UserTag,
          std::size_t Producers,
          std::size_t Consumers,
          std::size_t WorkloadBytes,
          std::size_t MaxShards>
struct ShardableRouteImpl;

template <typename T,
          std::size_t Capacity,
          typename UserTag,
          std::size_t Producers,
          std::size_t Consumers,
          std::size_t WorkloadBytes,
          std::size_t MaxShards>
struct ShardableRouteImpl<false,
                          T,
                          Capacity,
                          UserTag,
                          Producers,
                          Consumers,
                          WorkloadBytes,
                          MaxShards> {
    static constexpr ChannelTopology topology =
        recommend_topology(Producers, Consumers, false);
    static_assert(payload_fits_topology_v<topology, T>,
                  "AutoRouter Shardable payload does not satisfy the "
                  "selected Permissioned* topology's value concept");

    using type = Substrate_t<topology, T, Capacity, UserTag>;
};

template <typename T,
          std::size_t Capacity,
          typename UserTag,
          std::size_t Producers,
          std::size_t Consumers,
          std::size_t WorkloadBytes,
          std::size_t MaxShards>
struct ShardableRouteImpl<true,
                          T,
                          Capacity,
                          UserTag,
                          Producers,
                          Consumers,
                          WorkloadBytes,
                          MaxShards> {
    static_assert(SpscValue<T>,
                  "AutoRouter Shardable grid payload must satisfy SpscValue<T>");

    static constexpr std::size_t factor =
        auto_shard_factor<WorkloadBytes, MaxShards>();

    using type = PermissionedShardedGrid<T, factor, factor, Capacity, UserTag>;
};

template <typename T,
          std::size_t Capacity,
          typename UserTag,
          std::size_t Producers,
          std::size_t Consumers,
          std::size_t WorkloadBytes,
          std::size_t MaxShards>
struct AutoRouteImpl<RouteIntent::Stream,
                     T,
                     Capacity,
                     UserTag,
                     Producers,
                     Consumers,
                     WorkloadBytes,
                     MaxShards> {
    static_assert(Capacity > 0, "AutoRouter Stream requires Capacity > 0");
    static_assert(Producers > 0, "AutoRouter Stream requires Producers > 0");
    static_assert(Consumers > 0, "AutoRouter Stream requires Consumers > 0");

    static constexpr ChannelTopology topology =
        recommend_topology(Producers, Consumers, false);
    static_assert(detail::payload_fits_topology_v<topology, T>,
                  "AutoRouter Stream payload does not satisfy the selected "
                  "Permissioned* topology's value concept");

    using type = Substrate_t<topology, T, Capacity, UserTag>;
};

template <typename T,
          std::size_t Capacity,
          typename UserTag,
          std::size_t Producers,
          std::size_t Consumers,
          std::size_t WorkloadBytes,
          std::size_t MaxShards>
struct AutoRouteImpl<RouteIntent::Latest,
                     T,
                     Capacity,
                     UserTag,
                     Producers,
                     Consumers,
                     WorkloadBytes,
                     MaxShards> {
    static_assert(SnapshotValue<T>,
                  "AutoRouter Latest requires SnapshotValue<T> payloads");
    static_assert(Producers > 0, "AutoRouter Latest requires Producers > 0");
    static_assert(Consumers > 0, "AutoRouter Latest requires Consumers > 0");

    using type = PermissionedSnapshot<T, UserTag>;
};

template <typename T,
          std::size_t Capacity,
          typename UserTag,
          std::size_t Producers,
          std::size_t Consumers,
          std::size_t WorkloadBytes,
          std::size_t MaxShards>
struct AutoRouteImpl<RouteIntent::VariableCost,
                     T,
                     Capacity,
                     UserTag,
                     Producers,
                     Consumers,
                     WorkloadBytes,
                     MaxShards> {
    static_assert(Capacity > 0,
                  "AutoRouter VariableCost requires Capacity > 0");
    static_assert(Producers > 0,
                  "AutoRouter VariableCost requires Producers > 0");
    static_assert(Consumers > 0,
                  "AutoRouter VariableCost requires Consumers > 0");
    static_assert(DequeValue<T>,
                  "AutoRouter VariableCost payload must satisfy DequeValue<T>");

    using type = PermissionedChaseLevDeque<T, Capacity, UserTag>;
};

template <typename T,
          std::size_t Capacity,
          typename UserTag,
          std::size_t Producers,
          std::size_t Consumers,
          std::size_t WorkloadBytes,
          std::size_t MaxShards>
struct AutoRouteImpl<RouteIntent::Shardable,
                     T,
                     Capacity,
                     UserTag,
                     Producers,
                     Consumers,
                     WorkloadBytes,
                     MaxShards> {
    static_assert(Capacity > 0, "AutoRouter Shardable requires Capacity > 0");
    static_assert(Producers > 0, "AutoRouter Shardable requires Producers > 0");
    static_assert(Consumers > 0, "AutoRouter Shardable requires Consumers > 0");
    static_assert(MaxShards > 0, "AutoRouter Shardable requires MaxShards > 0");

    using type = typename ShardableRouteImpl<
        (WorkloadBytes > conservative_cliff_l2_per_core),
        T,
        Capacity,
        UserTag,
        Producers,
        Consumers,
        WorkloadBytes,
        MaxShards>::type;
};

}  // namespace detail

template <RouteIntent Intent,
          typename T,
          std::size_t Capacity,
          typename UserTag,
          std::size_t Producers,
          std::size_t Consumers,
          std::size_t WorkloadBytes,
          std::size_t MaxShards = 16>
struct AutoRoute
    : detail::AutoRouteImpl<Intent,
                            T,
                            Capacity,
                            UserTag,
                            Producers,
                            Consumers,
                            WorkloadBytes,
                            MaxShards> {};

template <RouteIntent Intent,
          typename T,
          std::size_t Capacity,
          typename UserTag,
          std::size_t Producers,
          std::size_t Consumers,
          std::size_t WorkloadBytes,
          std::size_t MaxShards = 16>
using AutoRoute_t = typename AutoRoute<Intent,
                                       T,
                                       Capacity,
                                       UserTag,
                                       Producers,
                                       Consumers,
                                       WorkloadBytes,
                                       MaxShards>::type;

template <RouteIntent Intent,
          typename T,
          std::size_t Capacity,
          typename UserTag,
          std::size_t Producers,
          std::size_t Consumers,
          std::size_t WorkloadBytes,
          std::size_t MaxShards = 16>
struct StaticAutoRoute : AutoRoute<Intent,
                                   T,
                                   Capacity,
                                   UserTag,
                                   Producers,
                                   Consumers,
                                   WorkloadBytes,
                                   MaxShards> {
    static_assert(Producers > 0, "StaticAutoRoute requires Producers > 0");
    static_assert(Consumers > 0, "StaticAutoRoute requires Consumers > 0");
    static_assert(MaxShards > 0, "StaticAutoRoute requires MaxShards > 0");

    static constexpr AutoRouteDecision decision =
        detail::StaticAutoRouteDecisionImpl<Intent,
                                            Producers,
                                            Consumers,
                                            WorkloadBytes,
                                            MaxShards>::value;
};

template <RouteIntent Intent,
          typename T,
          std::size_t Capacity,
          typename UserTag,
          std::size_t Producers,
          std::size_t Consumers,
          std::size_t WorkloadBytes,
          std::size_t MaxShards = 16>
using StaticAutoRoute_t = typename StaticAutoRoute<Intent,
                                                  T,
                                                  Capacity,
                                                  UserTag,
                                                  Producers,
                                                  Consumers,
                                                  WorkloadBytes,
                                                  MaxShards>::type;

template <RouteIntent Intent,
          typename T,
          std::size_t Capacity,
          typename UserTag,
          std::size_t Producers,
          std::size_t Consumers,
          std::size_t WorkloadBytes,
          std::size_t MaxShards = 16>
inline constexpr AutoRouteDecision static_auto_route_v =
    StaticAutoRoute<Intent,
                    T,
                    Capacity,
                    UserTag,
                    Producers,
                    Consumers,
                    WorkloadBytes,
                    MaxShards>::decision;

}  // namespace crucible::concurrent
