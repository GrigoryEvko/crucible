#pragma once

// ═══════════════════════════════════════════════════════════════════
// concurrent/scheduler/Policies.h — compile-time scheduler policy
// types for the future NumaThreadPool / AdaptiveScheduler dispatch.
//
// THREADING.md §5.5.2 + §8.7 enumerate seven scheduler flavours;
// each is a zero-cost tag type that selects which Permissioned*
// primitive the pool's queue resolves to AT COMPILE TIME.
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
//   * static constexpr bool needs_priority_key
//       True for Deadline / Cfs / Eevdf — the dispatcher must extract
//       a per-job key (deadline-ns, virtual runtime, virtual deadline)
//       before submission.  Fifo / Lifo / RoundRobin / LocalityAware
//       are key-free — submission order alone picks the slot.
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
// Usage (at the call site of the future pool):
//
//   using Pool = NumaThreadPool<scheduler::LocalityAware, MyTag>;
//   //          ^^^^^^^^^^^^^^^^ pool template parameterised on policy
//   //                            queue type derives from policy::
//   //                            template queue_template<Job>.
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
#include <crucible/concurrent/PermissionedShardedGrid.h>
#include <crucible/concurrent/traits/Concepts.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace crucible::concurrent::scheduler {

// ── Phantom region tags ─────────────────────────────────────────────
//
// One per policy.  The Permissioned wrapper's user_tag is built atop
// these; mixing tags between policies is a compile error because the
// underlying Permission tags do not unify.

namespace tag {
struct Fifo {};
struct Lifo {};
struct RoundRobin {};
struct LocalityAware {};
struct Deadline {};
struct Cfs {};
struct Eevdf {};
}  // namespace tag

// ── Policy-tunable defaults ─────────────────────────────────────────
//
// Each policy reads its capacity / shard counts from a partial
// specialisation here.  Deployments wanting different defaults
// specialise this without disturbing the policy struct itself.

template <typename Policy>
struct policy_defaults {
    static constexpr std::size_t capacity      = 1024;
    static constexpr std::size_t num_shards    = 4;
    static constexpr std::size_t num_consumers = 4;
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
    static constexpr bool needs_priority_key = false;
    static constexpr bool needs_topology     = false;
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
    static constexpr bool needs_priority_key = false;
    static constexpr bool needs_topology     = false;
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
    static constexpr bool needs_priority_key = false;
    static constexpr bool needs_topology     = false;
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
    static constexpr bool needs_priority_key = false;
    static constexpr bool needs_topology     = true;
    static constexpr std::string_view name() noexcept { return "LocalityAware"; }
};

// ═══════════════════════════════════════════════════════════════════
// Deadline — Earliest-Deadline-First via the calendar queue.  Each
// job's key is its deadline (any monotonic priority-ns value); the
// calendar grid's bucket-clamping invariant guarantees per-row FIFO
// across producers within the same time quantum.
//
// User must supply:
//   KeyExtractor — type with `static uint64_t key(const Job&) noexcept`
//                  returning the priority-ns value.
//   QuantumNs    — bucket width; smaller = finer priority resolution
//                  but shorter time horizon (NumBuckets * QuantumNs).
//
// This is a class template, not a struct, because the KeyExtractor
// is per-job.  Template instantiations satisfy SchedulerPolicy.
// ═══════════════════════════════════════════════════════════════════

template <typename KeyExtractor,
          std::size_t   NumProducers = policy_defaults<tag::Deadline>::num_shards,
          std::size_t   NumBuckets   = 1024,
          std::size_t   BucketCap    = policy_defaults<tag::Deadline>::capacity,
          std::uint64_t QuantumNs    = 100'000>  // 100 µs default bucket width
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
    static constexpr bool needs_priority_key = true;
    static constexpr bool needs_topology     = false;
    static constexpr std::string_view name() noexcept { return "Deadline"; }
};

// ═══════════════════════════════════════════════════════════════════
// Cfs — Linux CFS-style proportional-share scheduling via a sorted
// red-black tree of virtual runtimes.
//
// SCAFFOLDING ONLY.  A real CFS implementation needs a lock-free
// sorted tree (no Permissioned wrapper provides this yet).  For now
// the queue resolves to MPMC + the policy is tagged needs_priority_key
// so callers can detect that the priority extraction is ignored.
// SEPLOG-H3 tracks the real implementation.
// ═══════════════════════════════════════════════════════════════════

struct Cfs {
    template <typename Job>
    using queue_template =
        PermissionedMpmcChannel<Job,
                                policy_defaults<Cfs>::capacity,
                                tag::Cfs>;

    using policy_tag = tag::Cfs;
    static constexpr bool needs_priority_key = true;
    static constexpr bool needs_topology     = false;
    static constexpr bool is_scaffolding     = true;
    static constexpr std::string_view name() noexcept { return "Cfs"; }
};

// ═══════════════════════════════════════════════════════════════════
// Eevdf — Linux 6.6+ default — Earliest Eligible Virtual Deadline
// First.  Proportional share + per-task latency bound.
//
// SCAFFOLDING ONLY (same caveat as Cfs).  An EEVDF tree is more
// involved than CFS's RB-tree because it tracks both virtual runtime
// AND eligibility timestamps.  Resolves to MPMC for now; SEPLOG-H3
// tracks the real implementation.
// ═══════════════════════════════════════════════════════════════════

struct Eevdf {
    template <typename Job>
    using queue_template =
        PermissionedMpmcChannel<Job,
                                policy_defaults<Eevdf>::capacity,
                                tag::Eevdf>;

    using policy_tag = tag::Eevdf;
    static constexpr bool needs_priority_key = true;
    static constexpr bool needs_topology     = false;
    static constexpr bool is_scaffolding     = true;
    static constexpr std::string_view name() noexcept { return "Eevdf"; }
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
        { P::needs_priority_key } -> std::convertible_to<bool>;
        { P::needs_topology     } -> std::convertible_to<bool>;
        { P::name()             } -> std::convertible_to<std::string_view>;
    }
    && traits::PermissionedChannel<typename P::template queue_template<Job>>;

// ── Detection traits — convenient for dispatcher concept overloads ──

template <typename P>
inline constexpr bool is_scaffolding_v = false;

template <>
inline constexpr bool is_scaffolding_v<Cfs>   = true;

template <>
inline constexpr bool is_scaffolding_v<Eevdf> = true;

// ── DefaultPolicy — what the pool selects when the user omits one ──

using DefaultPolicy = LocalityAware;

}  // namespace crucible::concurrent::scheduler
