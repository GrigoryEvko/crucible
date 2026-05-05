// test_auto_router — compile-time MVP coverage for AutoRouter.h
//
// The router must remain a zero-runtime-cost type-level policy:
// decisions are constexpr values and AutoRoute_t aliases directly to
// existing permissioned primitives.

#include <crucible/concurrent/AutoRouter.h>

#include <type_traits>

namespace cc = crucible::concurrent;

namespace {

struct StreamTag {};
struct LatestTag {};
struct ShardTag {};
struct WorkTag {};

constexpr std::size_t KiB = 1024;
constexpr std::size_t MiB = 1024 * KiB;
constexpr std::size_t Small = 64 * KiB;
constexpr std::size_t Large = 4 * MiB;
constexpr std::size_t Huge = 64 * MiB;

// Ordered streams preserve FIFO topology even above the L2 cliff.
static_assert(cc::auto_route_v<cc::RouteIntent::Stream, 1, 1, Large>.kind
              == cc::RouteKind::Spsc);
static_assert(cc::auto_route_v<cc::RouteIntent::Stream, 1, 1, Large>.topology
              == cc::ChannelTopology::OneToOne);
static_assert(!cc::auto_route_v<cc::RouteIntent::Stream, 1, 1, Large>.sharded);
static_assert(std::is_same_v<
              cc::AutoRoute_t<cc::RouteIntent::Stream,
                              int,
                              1024,
                              StreamTag,
                              1,
                              1,
                              Large>,
              cc::PermissionedSpscChannel<int, 1024, StreamTag>>);

// Producer/consumer cardinality maps to the existing permissioned zoo.
static_assert(cc::auto_route_v<cc::RouteIntent::Stream, 4, 1, Small>.kind
              == cc::RouteKind::Mpsc);
static_assert(std::is_same_v<
              cc::AutoRoute_t<cc::RouteIntent::Stream,
                              int,
                              1024,
                              StreamTag,
                              4,
                              1,
                              Small>,
              cc::PermissionedMpscChannel<int, 1024, StreamTag>>);

static_assert(cc::auto_route_v<cc::RouteIntent::Stream, 4, 3, Small>.kind
              == cc::RouteKind::Mpmc);
static_assert(std::is_same_v<
              cc::AutoRoute_t<cc::RouteIntent::Stream,
                              int,
                              1024,
                              StreamTag,
                              4,
                              3,
                              Small>,
              cc::PermissionedMpmcChannel<int, 1024, StreamTag>>);

// Latest-value intent is snapshot broadcast, independent of payload size.
static_assert(cc::auto_route_v<cc::RouteIntent::Latest, 1, 8, Small>.kind
              == cc::RouteKind::Snapshot);
static_assert(cc::auto_route_v<cc::RouteIntent::Latest, 1, 8, Small>.latest_only);
static_assert(std::is_same_v<
              cc::AutoRoute_t<cc::RouteIntent::Latest,
                              int,
                              1,
                              LatestTag,
                              1,
                              8,
                              Small>,
              cc::PermissionedSnapshot<int, LatestTag>>);

// Variable-cost work routes to the Chase-Lev work-stealing substrate.
static_assert(cc::auto_route_v<cc::RouteIntent::VariableCost, 1, 8, Small>.kind
              == cc::RouteKind::WorkStealing);
static_assert(std::is_same_v<
              cc::AutoRoute_t<cc::RouteIntent::VariableCost,
                              int,
                              1024,
                              WorkTag,
                              1,
                              8,
                              Small>,
              cc::PermissionedChaseLevDeque<int, 1024, WorkTag>>);

// Shardable below the cliff keeps the cardinality-derived stream route.
static_assert(cc::auto_route_v<cc::RouteIntent::Shardable, 1, 1, Small>.kind
              == cc::RouteKind::Spsc);
static_assert(cc::auto_route_v<cc::RouteIntent::Shardable, 1, 1, Small>.shard_factor
              == 1);
static_assert(std::is_same_v<
              cc::AutoRoute_t<cc::RouteIntent::Shardable,
                              int,
                              1024,
                              ShardTag,
                              1,
                              1,
                              Small>,
              cc::PermissionedSpscChannel<int, 1024, ShardTag>>);

// Shardable above the cliff opts into an MxN permissioned sharded grid.
static_assert(cc::auto_route_v<cc::RouteIntent::Shardable, 1, 1, Large>.kind
              == cc::RouteKind::ShardedGrid);
static_assert(cc::auto_route_v<cc::RouteIntent::Shardable, 1, 1, Large>.sharded);
static_assert(cc::auto_route_v<cc::RouteIntent::Shardable, 1, 1, Large>.shard_factor
              == 4);
static_assert(std::is_same_v<
              cc::AutoRoute_t<cc::RouteIntent::Shardable,
                              int,
                              1024,
                              ShardTag,
                              1,
                              1,
                              Large>,
              cc::PermissionedShardedGrid<int, 4, 4, 1024, ShardTag>>);

static_assert(cc::auto_route_v<cc::RouteIntent::Shardable, 1, 1, Huge>.shard_factor
              == 16);
static_assert(std::is_same_v<
              cc::AutoRoute_t<cc::RouteIntent::Shardable,
                              int,
                              1024,
                              ShardTag,
                              1,
                              1,
                              Huge>,
              cc::PermissionedShardedGrid<int, 16, 16, 1024, ShardTag>>);

// MaxShards is a hard compile-time cap.
static_assert(cc::auto_route_v<cc::RouteIntent::Shardable, 1, 1, Huge, 8>.shard_factor
              == 8);
static_assert(std::is_same_v<
              cc::AutoRoute_t<cc::RouteIntent::Shardable,
                              int,
                              1024,
                              ShardTag,
                              1,
                              1,
                              Huge,
                              8>,
              cc::PermissionedShardedGrid<int, 8, 8, 1024, ShardTag>>);

// Alternate static implementation must match the current consteval route.
static_assert(cc::static_auto_route_v<cc::RouteIntent::Shardable,
                                      int,
                                      1024,
                                      ShardTag,
                                      1,
                                      1,
                                      Large>.kind
              == cc::auto_route_v<cc::RouteIntent::Shardable,
                                  1,
                                  1,
                                  Large>.kind);
static_assert(cc::static_auto_route_v<cc::RouteIntent::Shardable,
                                      int,
                                      1024,
                                      ShardTag,
                                      1,
                                      1,
                                      Large>.shard_factor
              == cc::auto_route_v<cc::RouteIntent::Shardable,
                                  1,
                                  1,
                                  Large>.shard_factor);
static_assert(std::is_same_v<
              cc::StaticAutoRoute_t<cc::RouteIntent::Shardable,
                                    int,
                                    1024,
                                    ShardTag,
                                    1,
                                    1,
                                    Large>,
              cc::AutoRoute_t<cc::RouteIntent::Shardable,
                              int,
                              1024,
                              ShardTag,
                              1,
                              1,
                              Large>>);

// Runtime implementation is for size/cardinality known after startup.
constexpr auto runtime_large =
    cc::auto_route_decision_runtime(cc::RouteIntent::Shardable, 1, 1, Large);
static_assert(runtime_large.kind == cc::RouteKind::ShardedGrid);
static_assert(runtime_large.shard_factor == 4);

constexpr auto runtime_profiled_small_l2 =
    cc::auto_route_decision_runtime(
        cc::RouteIntent::Shardable,
        1,
        1,
        128 * KiB,
        16,
        cc::AutoRouteRuntimeProfile{.l2_per_core_bytes = 64 * KiB});
static_assert(runtime_profiled_small_l2.kind == cc::RouteKind::ShardedGrid);

}  // namespace

int main() {
    return 0;
}
