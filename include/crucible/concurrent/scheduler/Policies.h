#pragma once

// ═══════════════════════════════════════════════════════════════════
// concurrent/scheduler/Policies.h — compile-time scheduler policy
// types for the future NumaThreadPool / AdaptiveScheduler dispatch.
//
// THREADING.md §5.5.2 + §8.7 enumerate seven scheduler flavours;
// each is a zero-cost tag type that selects which Permissioned*
// primitive the pool's queue resolves to AT COMPILE TIME.
//
// ─── Two layers of "scheduling" ───────────────────────────────────
//
//   QUEUE TOPOLOGY (this header)        : how jobs are stored and
//                                          retrieved — FIFO ring, LIFO
//                                          deque, sharded grid, calendar
//                                          grid keyed on a priority.
//
//   SCHEDULING MATH (above this layer)  : which job is "next" given
//                                          per-task accumulators —
//                                          vruntime, eligibility,
//                                          deadline.  The user's
//                                          KeyExtractor encodes the
//                                          meaning of "priority"; the
//                                          AdaptiveScheduler maintains
//                                          per-task state above the
//                                          queue.
//
// Deadline / Cfs / Eevdf all share PermissionedCalendarGrid topology
// (lowest-key-first FIFO with per-priority-bucket sharding).  They are
// DISTINCT types — distinct UserTag, distinct queue_template<Job>
// instantiation — so the type system prevents accidentally feeding
// EDF jobs to a CFS pool or vice versa.  The user's KeyExtractor
// determines what "priority" means:
//
//   Deadline<K>: K::key(job) returns absolute deadline (e.g. ns since
//                epoch).  Lowest deadline = highest priority.
//
//   Cfs<K>:      K::key(job) returns the task's accumulated virtual
//                runtime (vruntime), monotonically non-decreasing per
//                task.  Lowest vruntime = task that has run least.
//                Caller maintains per-task vruntime accumulator
//                outside the queue and advances it on dequeue+yield.
//
//   Eevdf<K>:    K::key(job) returns the earliest eligible virtual
//                deadline (= vruntime + request / weight).  Lowest
//                virtual deadline = task most overdue for service.
//                Caller maintains both vruntime and per-task weight
//                outside the queue.
//
// All three guarantee "smaller key first" via the calendar grid's
// per-row FIFO + bucket-clamping invariant.  The semantic difference
// is documented intent — ENFORCED BY TYPE IDENTITY at the policy
// level so production code that mixes them is a compile error.
//
// ─── Policy contract ──────────────────────────────────────────────
//
// A SchedulerPolicy P exposes:
//
//   * template <typename Job> using queue_template = …
//       The Permissioned* wrapper instantiation that holds Jobs.
//       Must satisfy traits::PermissionedChannel<queue_template<Job>>.
//
//   * using policy_tag = …
//       Phantom tag identifying the policy's region tree.  Distinct
//       per policy so user code mixing policies in adjacent containers
//       cannot cross-contaminate at the type level.
//
//   * static constexpr PriorityKind priority_kind
//       Discriminator for the scheduling math the dispatcher should
//       run above the queue.  None / Deadline / VirtualRuntime /
//       VirtualDeadline.
//
//   * static constexpr bool needs_topology
//       True for LocalityAware — the dispatcher must consult the
//       Topology probe (L3 grouping, NUMA distances) before placing
//       producers.  Other policies do not depend on topology.
//
//   * static constexpr std::string_view name() noexcept
//       Human-readable for diagnostics.  Reflection-derived later
//       (FOUND-E02), hand-written for now.
//
// References:
//   THREADING.md §5.5.2 (the seven scheduler flavours)
//   THREADING.md §8.7   (per-policy cost shape table)
//   misc/27_04_2026.md §1.4 (CSL discipline everywhere)
//   misc/27_04_2026.md §3   (parameter-shape protocol the dispatcher reads)
//   Tracking: SEPLOG-H3 (#329)
// ═══════════════════════════════════════════════════════════════════

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

// ── PriorityKind discriminator ─────────────────────────────────────
//
// Distinguishes Deadline / Cfs / Eevdf at the type level so the
// AdaptiveScheduler (#313) can route per-policy scheduling math
// correctly without per-policy specialisations.  None means the queue
// is order-only (Fifo / Lifo / RoundRobin / LocalityAware).

enum class PriorityKind : std::uint8_t {
    None,             // queue order alone determines next job
    Deadline,         // key = absolute deadline; smaller = sooner due
    VirtualRuntime,   // key = accumulated vruntime; smaller = ran least
    VirtualDeadline,  // key = vruntime + lag/weight; smaller = overdue
};

// ── Phantom region tags ─────────────────────────────────────────────
//
// One per policy.  Distinct UserTag values inside the Permissioned
// wrapper produce distinct queue_template<Job> instantiations even
// for the same Job, so cross-contamination is a compile error.

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

// ── Policy-tunable defaults ─────────────────────────────────────────
//
// Each policy reads its capacity / shard counts from a partial
// specialisation here.  Deployments wanting different defaults
// specialise this without disturbing the policy struct itself.

template <typename Policy>
struct policy_defaults {
    static constexpr std::size_t   capacity      = 1024;
    static constexpr std::size_t   num_shards    = 4;
    static constexpr std::size_t   num_consumers = 4;
    static constexpr std::size_t   num_buckets   = 1024;
    static constexpr std::uint64_t quantum       = 100'000;  // 100 µs / 100k vrun-ticks
};

// ═══════════════════════════════════════════════════════════════════
// Fifo — single shared MPMC queue, strict global FIFO.
//
// Per THREADING.md §5.5.2: simplest model, useful for ordered
// processing and debug.  Bottleneck under 16+ workers — the single
// MpmcRing head cache line ping-pongs.  At lower contention or when
// debug visibility outweighs raw throughput, Fifo is correct default.
// ═══════════════════════════════════════════════════════════════════

struct Fifo {
    template <typename Job>
    using queue_template =
        PermissionedMpmcChannel<Job,
                                policy_defaults<Fifo>::capacity,
                                tag::Fifo>;

    using policy_tag = tag::Fifo;
    static constexpr PriorityKind priority_kind = PriorityKind::None;
    static constexpr bool needs_topology        = false;
    static constexpr std::string_view name() noexcept { return "Fifo"; }
};

// ═══════════════════════════════════════════════════════════════════
// Lifo — owner-LIFO via Chase-Lev deque (Rayon / TBB pattern).
//
// Owner pushes + pops at bottom (cache-hot, single-thread fast path).
// Thieves steal FIFO from top — the slow path.  Per THREADING.md
// §5.5.2: best for recursive fork-join where the owner re-uses hot
// L1 data across nested tasks.
// ═══════════════════════════════════════════════════════════════════

struct Lifo {
    template <typename Job>
    using queue_template =
        PermissionedChaseLevDeque<Job,
                                  policy_defaults<Lifo>::capacity,
                                  tag::Lifo>;

    using policy_tag = tag::Lifo;
    static constexpr PriorityKind priority_kind = PriorityKind::None;
    static constexpr bool needs_topology        = false;
    static constexpr std::string_view name() noexcept { return "Lifo"; }
};

// ═══════════════════════════════════════════════════════════════════
// RoundRobin — N per-worker MPSC shards, submitter rotates across
// them via an atomic counter (the rotation lives in the pool, not
// here).  Each shard is a single-consumer MPSC ring; queue_template
// resolves to that one shard.
//
// No load balancing: tasks of uneven cost stack on whichever worker
// drew them.  Use when balance matters less than predictable per-
// worker queue depth.
// ═══════════════════════════════════════════════════════════════════

struct RoundRobin {
    template <typename Job>
    using queue_template =
        PermissionedMpscChannel<Job,
                                policy_defaults<RoundRobin>::capacity,
                                tag::RoundRobin>;

    using policy_tag = tag::RoundRobin;
    static constexpr PriorityKind priority_kind = PriorityKind::None;
    static constexpr bool needs_topology        = false;
    static constexpr std::string_view name() noexcept { return "RoundRobin"; }
};

// ═══════════════════════════════════════════════════════════════════
// LocalityAware ★ — DEFAULT POLICY for fork-join of short-lived tasks
// on contiguous arenas.  M producers (typically one per L3 group) ×
// N consumers (typically one per worker), each cell its own SpscRing.
//
// Per THREADING.md §5.5.2: workers drain their own L3 shard first
// (L3 cache hit), then steal within NUMA (L3 miss + DRAM-local hit),
// then cross-NUMA (DRAM + QPI hop).  The grid layout means any
// producer-consumer pair operates on its own cache line; no global
// head ping-pong.
// ═══════════════════════════════════════════════════════════════════

struct LocalityAware {
    template <typename Job>
    using queue_template =
        PermissionedShardedGrid<Job,
                                policy_defaults<LocalityAware>::num_shards,
                                policy_defaults<LocalityAware>::num_consumers,
                                policy_defaults<LocalityAware>::capacity,
                                tag::LocalityAware>;

    using policy_tag = tag::LocalityAware;
    static constexpr PriorityKind priority_kind = PriorityKind::None;
    static constexpr bool needs_topology        = true;
    static constexpr std::string_view name() noexcept { return "LocalityAware"; }
};

// ═══════════════════════════════════════════════════════════════════
// Priority-keyed family — shared PermissionedCalendarGrid topology;
// distinguished by intent (PriorityKind + UserTag).
//
// Calendar-grid invariants (see PermissionedCalendarGrid.h):
//   * Per-row FIFO: items pushed by the same producer pop in the
//     order they were pushed.
//   * Bucket clamp: items with a key in the past land in the current
//     bucket — the queue never reorders backwards in time.
//   * Per-bucket SpscRing: O(1) push, O(1) pop within a bucket.
//
// The KeyExtractor type must satisfy:
//   static std::uint64_t key(const Job&) noexcept;
// returning the priority value.  Lowest key pops first.
// ═══════════════════════════════════════════════════════════════════

// ── Deadline (EDF) ────────────────────────────────────────────────
//
// Earliest-Deadline-First.  KeyExtractor returns the absolute
// deadline (caller's choice of unit; QuantumNs is the bucket width
// in the same unit).
//
// Use when: each job has a hard or soft deadline and miss penalty
// dominates other scheduling concerns.

template <typename KeyExtractor,
          std::size_t   NumProducers = policy_defaults<tag::Deadline>::num_shards,
          std::size_t   NumBuckets   = policy_defaults<tag::Deadline>::num_buckets,
          std::size_t   BucketCap    = policy_defaults<tag::Deadline>::capacity,
          std::uint64_t QuantumNs    = policy_defaults<tag::Deadline>::quantum>
struct Deadline {
    template <typename Job>
    using queue_template =
        PermissionedCalendarGrid<Job,
                                 NumProducers,
                                 NumBuckets,
                                 BucketCap,
                                 KeyExtractor,
                                 QuantumNs,
                                 tag::Deadline>;

    using policy_tag = tag::Deadline;
    static constexpr PriorityKind priority_kind = PriorityKind::Deadline;
    static constexpr bool needs_topology        = false;
    static constexpr std::string_view name() noexcept { return "Deadline"; }
};

// ── Cfs (Linux Completely Fair Scheduler analogue) ─────────────────
//
// Proportional-share via virtual runtime.  KeyExtractor returns the
// task's accumulated vruntime.  Lowest vruntime = task that has run
// least.  Caller advances each task's vruntime by (real_time / weight)
// after each dequeue+yield, then re-enqueues with the new key.
//
// Bucket width here is in *vruntime units*, not nanoseconds — picked
// to balance bucket count vs priority resolution.  Default 100k
// vruntime ticks = roughly one timeslice's worth of accumulation at
// a typical weight.
//
// Use when: long-lived tasks need fair-share guarantees; per-task
// weight provides priority differentiation.  Pair with the
// AdaptiveScheduler's per-task vruntime accumulator (#313).

template <typename KeyExtractor,
          std::size_t   NumProducers = policy_defaults<tag::Cfs>::num_shards,
          std::size_t   NumBuckets   = policy_defaults<tag::Cfs>::num_buckets,
          std::size_t   BucketCap    = policy_defaults<tag::Cfs>::capacity,
          std::uint64_t Quantum      = policy_defaults<tag::Cfs>::quantum>
struct Cfs {
    template <typename Job>
    using queue_template =
        PermissionedCalendarGrid<Job,
                                 NumProducers,
                                 NumBuckets,
                                 BucketCap,
                                 KeyExtractor,
                                 Quantum,
                                 tag::Cfs>;

    using policy_tag = tag::Cfs;
    static constexpr PriorityKind priority_kind = PriorityKind::VirtualRuntime;
    static constexpr bool needs_topology        = false;
    static constexpr std::string_view name() noexcept { return "Cfs"; }
};

// ── Eevdf (Linux 6.6+ default) ────────────────────────────────────
//
// Earliest Eligible Virtual Deadline First.  KeyExtractor returns the
// task's virtual deadline (= vruntime + request_size / weight).
// Provides EEVDF's latency bound on top of CFS's fair share.  Caller
// maintains both per-task vruntime AND per-task weight outside the
// queue and computes the virtual deadline at enqueue time.
//
// Same calendar-grid topology as Cfs/Deadline; the EEVDF math lives
// in the user's KeyExtractor and the AdaptiveScheduler's per-task
// state.

template <typename KeyExtractor,
          std::size_t   NumProducers = policy_defaults<tag::Eevdf>::num_shards,
          std::size_t   NumBuckets   = policy_defaults<tag::Eevdf>::num_buckets,
          std::size_t   BucketCap    = policy_defaults<tag::Eevdf>::capacity,
          std::uint64_t Quantum      = policy_defaults<tag::Eevdf>::quantum>
struct Eevdf {
    template <typename Job>
    using queue_template =
        PermissionedCalendarGrid<Job,
                                 NumProducers,
                                 NumBuckets,
                                 BucketCap,
                                 KeyExtractor,
                                 Quantum,
                                 tag::Eevdf>;

    using policy_tag = tag::Eevdf;
    static constexpr PriorityKind priority_kind = PriorityKind::VirtualDeadline;
    static constexpr bool needs_topology        = false;
    static constexpr std::string_view name() noexcept { return "Eevdf"; }
};

// ═══════════════════════════════════════════════════════════════════
// PerShard priority-keyed family — N independent calendar grids.
//
// Topologically a `PermissionedShardedCalendarGrid`: NumShards
// independent per-shard calendars, each with its OWN current_bucket
// atomic.  The producer's try_push reads only its shard's
// current_bucket — same-core in well-pinned production code, no
// cross-thread atomic on the push path.  Eliminates the 100-200μs
// p99.9 tail observed on the single-grid Cfs/Eevdf/Deadline at
// 4-producer contention.
//
// Trade-off (matches Linux CFS/EEVDF per-CPU red-black trees):
//   * Per-shard priority is EXACT.
//   * Cross-shard priority is APPROXIMATE — shard A may be at
//     bucket 100 draining while shard B is at bucket 200; they
//     are NOT globally ordered.
//
// Use when: tail latency matters more than global priority
// correctness AND the workload can be partitioned across shards
// (e.g., per-NUMA-node, per-coordinator-thread, per-collective-
// bucket).  Pin producer P → shard S(P) typically S = P %
// NumShards or NUMA-node-of(P).
//
// If your workload requires global priority correctness, use
// the single-grid Deadline / Cfs / Eevdf above and accept the
// p99.9 tail under contention.
// ═══════════════════════════════════════════════════════════════════

template <typename Policy>
struct per_shard_defaults {
    static constexpr std::size_t   num_shards   = 4;
    static constexpr std::size_t   num_buckets  = 64;
    static constexpr std::size_t   bucket_cap   = 16;
    static constexpr std::uint64_t quantum      = 100'000;
};

// ── DeadlinePerShard ──────────────────────────────────────────────

template <typename KeyExtractor,
          std::size_t   NumShards   = per_shard_defaults<tag::DeadlinePerShard>::num_shards,
          std::size_t   NumBuckets  = per_shard_defaults<tag::DeadlinePerShard>::num_buckets,
          std::size_t   BucketCap   = per_shard_defaults<tag::DeadlinePerShard>::bucket_cap,
          std::uint64_t QuantumNs   = per_shard_defaults<tag::DeadlinePerShard>::quantum>
struct DeadlinePerShard {
    template <typename Job>
    using queue_template =
        PermissionedShardedCalendarGrid<Job,
                                        NumShards,
                                        NumBuckets,
                                        BucketCap,
                                        KeyExtractor,
                                        QuantumNs,
                                        tag::DeadlinePerShard>;

    using policy_tag = tag::DeadlinePerShard;
    static constexpr PriorityKind priority_kind = PriorityKind::Deadline;
    static constexpr bool needs_topology        = true;
    static constexpr std::string_view name() noexcept { return "DeadlinePerShard"; }
};

// ── CfsPerShard ──────────────────────────────────────────────────

template <typename KeyExtractor,
          std::size_t   NumShards   = per_shard_defaults<tag::CfsPerShard>::num_shards,
          std::size_t   NumBuckets  = per_shard_defaults<tag::CfsPerShard>::num_buckets,
          std::size_t   BucketCap   = per_shard_defaults<tag::CfsPerShard>::bucket_cap,
          std::uint64_t Quantum     = per_shard_defaults<tag::CfsPerShard>::quantum>
struct CfsPerShard {
    template <typename Job>
    using queue_template =
        PermissionedShardedCalendarGrid<Job,
                                        NumShards,
                                        NumBuckets,
                                        BucketCap,
                                        KeyExtractor,
                                        Quantum,
                                        tag::CfsPerShard>;

    using policy_tag = tag::CfsPerShard;
    static constexpr PriorityKind priority_kind = PriorityKind::VirtualRuntime;
    static constexpr bool needs_topology        = true;
    static constexpr std::string_view name() noexcept { return "CfsPerShard"; }
};

// ── EevdfPerShard ────────────────────────────────────────────────

template <typename KeyExtractor,
          std::size_t   NumShards   = per_shard_defaults<tag::EevdfPerShard>::num_shards,
          std::size_t   NumBuckets  = per_shard_defaults<tag::EevdfPerShard>::num_buckets,
          std::size_t   BucketCap   = per_shard_defaults<tag::EevdfPerShard>::bucket_cap,
          std::uint64_t Quantum     = per_shard_defaults<tag::EevdfPerShard>::quantum>
struct EevdfPerShard {
    template <typename Job>
    using queue_template =
        PermissionedShardedCalendarGrid<Job,
                                        NumShards,
                                        NumBuckets,
                                        BucketCap,
                                        KeyExtractor,
                                        Quantum,
                                        tag::EevdfPerShard>;

    using policy_tag = tag::EevdfPerShard;
    static constexpr PriorityKind priority_kind = PriorityKind::VirtualDeadline;
    static constexpr bool needs_topology        = true;
    static constexpr std::string_view name() noexcept { return "EevdfPerShard"; }
};

// ═══════════════════════════════════════════════════════════════════
// SchedulerPolicy concept — every shipped policy satisfies this for
// every Job type.  Dispatchers / pools constrain on it.
//
// We instantiate queue_template<Job> for the concept check; this both
// validates the policy's typedefs and structurally proves that the
// resulting wrapper is a PermissionedChannel.
// ═══════════════════════════════════════════════════════════════════

template <typename P, typename Job = int>
concept SchedulerPolicy =
    requires {
        typename P::policy_tag;
        typename P::template queue_template<Job>;
        { P::priority_kind } -> std::convertible_to<PriorityKind>;
        { P::needs_topology } -> std::convertible_to<bool>;
        { P::name()         } -> std::convertible_to<std::string_view>;
    }
    && traits::PermissionedChannel<typename P::template queue_template<Job>>;

// ── Detection traits — convenient for dispatcher concept overloads ──

template <typename P>
inline constexpr bool needs_priority_key_v =
    P::priority_kind != PriorityKind::None;

// ── DefaultPolicy — what the pool selects when the user omits one ──

using DefaultPolicy = LocalityAware;

}  // namespace crucible::concurrent::scheduler
