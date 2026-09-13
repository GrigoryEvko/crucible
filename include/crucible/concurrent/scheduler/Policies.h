#pragma once

// Scheduler policies, each a tag type that picks at compile time which
// channel the pool's queue resolves to.
//
// Two things are called scheduling and only one of them is here.  This
// header is the queue topology: how a job is stored and how it comes
// back out.  The scheduling arithmetic sits above it, deciding which
// job is next from per-task accumulators, and reaching this layer only
// through the key a caller's extractor computes.
//
// The priority-keyed policies share one topology and differ by
// intention.  Keeping them distinct types is the point: a pool built
// for deadlines will not accept jobs keyed by virtual runtime, and a
// mix is a compile error rather than a subtly wrong order.  What the
// key means is the caller's choice, and the caller keeps whatever state
// produces it outside the queue.
//
//   Deadline   the job's absolute deadline, soonest served first.
//   Cfs        the task's accumulated virtual runtime, so the task that
//              has run least is served first.  The caller advances it
//              on each yield and enqueues again.
//   Eevdf      the task's virtual deadline, which is its virtual
//              runtime plus its request scaled by its weight, so the
//              task most overdue is served first.  The caller keeps
//              both the runtime and the weight.
//
// Each has a per-shard counterpart, and the choice between the two is
// not the obvious one.  The per-shard form was expected to win on tail
// latency, since a producer there never reads a bucket pointer another
// thread writes.  Under sustained load it loses on tail and on
// throughput both, because its per-shard buckets hold far less than the
// single grid's and its producers wait for space.  Take the per-shard
// form when the producers genuinely partition, when one cannot spill
// into another's shard, when the load arrives in short bursts, and when
// an approximate order across shards is acceptable.  Otherwise take the
// single grid.
//
// A policy's needs_topology says the dispatcher has to consult the
// topology probe before it places producers.

#include <crucible/concurrent/PermissionedCalendarGrid.h>
#include <crucible/concurrent/PermissionedChaseLevDeque.h>
#include <crucible/concurrent/PermissionedMpmcChannel.h>
#include <crucible/concurrent/PermissionedMpscChannel.h>
#include <crucible/concurrent/PermissionedShardedCalendarGrid.h>
#include <crucible/concurrent/PermissionedShardedGrid.h>
#include <crucible/concurrent/traits/Concepts.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace crucible::concurrent::scheduler {

// Lets the dispatcher route a policy's scheduling arithmetic without a
// specialization per policy.

enum class PriorityKind : std::uint8_t {
    None,  // queue order alone determines next job
    Deadline,  // key = absolute deadline; smaller = sooner due
    VirtualRuntime,  // key = accumulated vruntime; smaller = ran least
    VirtualDeadline,  // key = vruntime + lag/weight; smaller = overdue
};

// One per policy.  Distinct tags give distinct queue instantiations for
// the same job type, which is what makes a mix a compile error.

namespace tag {
struct Fifo {};
struct Lifo {};
struct RoundRobin {};
struct LocalityAware {};
struct Deadline {};
struct Cfs {};
struct Eevdf {};
struct DeadlinePerShard {};
struct CfsPerShard {};
struct EevdfPerShard {};
}  // namespace tag

// A deployment that wants different sizes specializes this rather than
// touching the policy structs.

template <typename Policy>
struct policy_defaults {
    static constexpr std::size_t capacity = 1024;
    static constexpr std::size_t num_shards = 4;
    static constexpr std::size_t num_consumers = 4;
    static constexpr std::size_t num_buckets = 1024;
    static constexpr std::uint64_t quantum = 100000;  // nanoseconds, or virtual-runtime ticks
};

// The one shared index becomes a cache cliff as the worker count
// grows.  Right where a strict global order, or the legibility of one,
// is worth more than throughput.

struct Fifo {
    template <typename Job>
    using queue_template = PermissionedMpmcChannel<Job, policy_defaults<Fifo>::capacity, tag::Fifo>;

    using policy_tag = tag::Fifo;
    static constexpr PriorityKind priority_kind = PriorityKind::None;
    static constexpr bool needs_topology = false;
    static constexpr std::string_view name() noexcept { return "Fifo"; }
};

// The owner's end is uncontended and stays cache-hot, which suits
// recursive fork-join where nested tasks reuse the same data.

struct Lifo {
    template <typename Job>
    using queue_template = PermissionedChaseLevDeque<Job, policy_defaults<Lifo>::capacity, tag::Lifo>;

    using policy_tag = tag::Lifo;
    static constexpr PriorityKind priority_kind = PriorityKind::None;
    static constexpr bool needs_topology = false;
    static constexpr std::string_view name() noexcept { return "Lifo"; }
};

// One shard, with the rotation across shards living in the pool.
// Nothing balances the load, so tasks of uneven cost pile up on
// whichever worker drew them.  Right where a predictable per-worker
// depth is worth more than balance.

struct RoundRobin {
    template <typename Job>
    using queue_template = PermissionedMpscChannel<Job, policy_defaults<RoundRobin>::capacity, tag::RoundRobin>;

    using policy_tag = tag::RoundRobin;
    static constexpr PriorityKind priority_kind = PriorityKind::None;
    static constexpr bool needs_topology = false;
    static constexpr std::string_view name() noexcept { return "RoundRobin"; }
};

// The default.  Every producer and consumer pair gets a cell of its
// own, so nothing ping-pongs on a shared index.  A worker drains the
// shard sharing its own last-level cache first, then steals within its
// NUMA node, then across nodes, in order of what each miss costs.

struct LocalityAware {
    template <typename Job>
    using queue_template = PermissionedShardedGrid<Job, policy_defaults<LocalityAware>::num_shards,
                                                   policy_defaults<LocalityAware>::num_consumers,
                                                   policy_defaults<LocalityAware>::capacity, tag::LocalityAware>;

    using policy_tag = tag::LocalityAware;
    static constexpr PriorityKind priority_kind = PriorityKind::None;
    static constexpr bool needs_topology = true;
    static constexpr std::string_view name() noexcept { return "LocalityAware"; }
};

// The three priority-keyed policies below share one calendar grid.  Its
// smallest key comes out first, jobs from one producer keep their order
// among themselves, and a key already in the past lands in the current
// bucket rather than reordering backwards.
//
// The bucket width is in whatever unit the key is, which for a deadline
// is the caller's time unit.

template <typename KeyExtractor, std::size_t NumProducers = policy_defaults<tag::Deadline>::num_shards,
          std::size_t NumBuckets = policy_defaults<tag::Deadline>::num_buckets,
          std::size_t BucketCap = policy_defaults<tag::Deadline>::capacity,
          std::uint64_t QuantumNs = policy_defaults<tag::Deadline>::quantum>
struct Deadline {
    template <typename Job>
    using queue_template =
        PermissionedCalendarGrid<Job, NumProducers, NumBuckets, BucketCap, KeyExtractor, QuantumNs, tag::Deadline>;

    using policy_tag = tag::Deadline;
    static constexpr PriorityKind priority_kind = PriorityKind::Deadline;
    static constexpr bool needs_topology = false;
    static constexpr std::string_view name() noexcept { return "Deadline"; }
};

// The bucket width here is in virtual-runtime units and not in
// nanoseconds, chosen to trade bucket count against priority
// resolution.  The default is about one timeslice's accumulation at a
// middling weight.

template <typename KeyExtractor, std::size_t NumProducers = policy_defaults<tag::Cfs>::num_shards,
          std::size_t NumBuckets = policy_defaults<tag::Cfs>::num_buckets,
          std::size_t BucketCap = policy_defaults<tag::Cfs>::capacity,
          std::uint64_t Quantum = policy_defaults<tag::Cfs>::quantum>
struct Cfs {
    template <typename Job>
    using queue_template =
        PermissionedCalendarGrid<Job, NumProducers, NumBuckets, BucketCap, KeyExtractor, Quantum, tag::Cfs>;

    using policy_tag = tag::Cfs;
    static constexpr PriorityKind priority_kind = PriorityKind::VirtualRuntime;
    static constexpr bool needs_topology = false;
    static constexpr std::string_view name() noexcept { return "Cfs"; }
};

// Adds a latency bound on top of the fair share, at the cost of the
// caller computing the virtual deadline at enqueue time from both the
// virtual runtime and the weight.

template <typename KeyExtractor, std::size_t NumProducers = policy_defaults<tag::Eevdf>::num_shards,
          std::size_t NumBuckets = policy_defaults<tag::Eevdf>::num_buckets,
          std::size_t BucketCap = policy_defaults<tag::Eevdf>::capacity,
          std::uint64_t Quantum = policy_defaults<tag::Eevdf>::quantum>
struct Eevdf {
    template <typename Job>
    using queue_template =
        PermissionedCalendarGrid<Job, NumProducers, NumBuckets, BucketCap, KeyExtractor, Quantum, tag::Eevdf>;

    using policy_tag = tag::Eevdf;
    static constexpr PriorityKind priority_kind = PriorityKind::VirtualDeadline;
    static constexpr bool needs_topology = false;
    static constexpr std::string_view name() noexcept { return "Eevdf"; }
};

// The per-shard counterparts of the three above.  Each shard carries
// its own bucket pointer, so a producer's push reads nothing another
// thread writes, and the order within a shard is exact while the order
// across shards is not.  The header doc-block says when that trade is
// the right one and when it is not.

template <typename Policy>
struct per_shard_defaults {
    static constexpr std::size_t num_shards = 4;
    static constexpr std::size_t num_buckets = 64;
    static constexpr std::size_t bucket_cap = 16;
    static constexpr std::uint64_t quantum = 100000;
};

template <typename KeyExtractor, std::size_t NumShards = per_shard_defaults<tag::DeadlinePerShard>::num_shards,
          std::size_t NumBuckets = per_shard_defaults<tag::DeadlinePerShard>::num_buckets,
          std::size_t BucketCap = per_shard_defaults<tag::DeadlinePerShard>::bucket_cap,
          std::uint64_t QuantumNs = per_shard_defaults<tag::DeadlinePerShard>::quantum>
struct DeadlinePerShard {
    template <typename Job>
    using queue_template = PermissionedShardedCalendarGrid<Job, NumShards, NumBuckets, BucketCap, KeyExtractor,
                                                           QuantumNs, tag::DeadlinePerShard>;

    using policy_tag = tag::DeadlinePerShard;
    static constexpr PriorityKind priority_kind = PriorityKind::Deadline;
    static constexpr bool needs_topology = true;
    static constexpr std::string_view name() noexcept { return "DeadlinePerShard"; }
};

template <typename KeyExtractor, std::size_t NumShards = per_shard_defaults<tag::CfsPerShard>::num_shards,
          std::size_t NumBuckets = per_shard_defaults<tag::CfsPerShard>::num_buckets,
          std::size_t BucketCap = per_shard_defaults<tag::CfsPerShard>::bucket_cap,
          std::uint64_t Quantum = per_shard_defaults<tag::CfsPerShard>::quantum>
struct CfsPerShard {
    template <typename Job>
    using queue_template =
        PermissionedShardedCalendarGrid<Job, NumShards, NumBuckets, BucketCap, KeyExtractor, Quantum, tag::CfsPerShard>;

    using policy_tag = tag::CfsPerShard;
    static constexpr PriorityKind priority_kind = PriorityKind::VirtualRuntime;
    static constexpr bool needs_topology = true;
    static constexpr std::string_view name() noexcept { return "CfsPerShard"; }
};

template <typename KeyExtractor, std::size_t NumShards = per_shard_defaults<tag::EevdfPerShard>::num_shards,
          std::size_t NumBuckets = per_shard_defaults<tag::EevdfPerShard>::num_buckets,
          std::size_t BucketCap = per_shard_defaults<tag::EevdfPerShard>::bucket_cap,
          std::uint64_t Quantum = per_shard_defaults<tag::EevdfPerShard>::quantum>
struct EevdfPerShard {
    template <typename Job>
    using queue_template = PermissionedShardedCalendarGrid<Job, NumShards, NumBuckets, BucketCap, KeyExtractor, Quantum,
                                                           tag::EevdfPerShard>;

    using policy_tag = tag::EevdfPerShard;
    static constexpr PriorityKind priority_kind = PriorityKind::VirtualDeadline;
    static constexpr bool needs_topology = true;
    static constexpr std::string_view name() noexcept { return "EevdfPerShard"; }
};

// Instantiating the queue inside the check is deliberate: it validates
// the policy's own type aliases and proves the channel they name really
// is one.

template <typename P, typename Job = int>
concept SchedulerPolicy = requires {
    typename P::policy_tag;
    typename P::template queue_template<Job>;
    { P::priority_kind } -> std::convertible_to<PriorityKind>;
    { P::needs_topology } -> std::convertible_to<bool>;
    { P::name() } -> std::convertible_to<std::string_view>;
} && traits::PermissionedChannel<typename P::template queue_template<Job>>;

template <typename P>
inline constexpr bool needs_priority_key_v = P::priority_kind != PriorityKind::None;

using DefaultPolicy = LocalityAware;

}  // namespace crucible::concurrent::scheduler
