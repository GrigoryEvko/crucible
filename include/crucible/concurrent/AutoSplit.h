#pragma once

// crucible::concurrent::AutoSplit
//
// Runtime autosplitting for contiguous, permission-friendly jobs.  AutoRouter
// chooses whether a byte footprint is shardable; AutoSplit turns that decision
// into concrete [begin, end) item ranges and can dispatch those ranges through
// the existing AdaptiveScheduler pool.

#include <crucible/concurrent/AdaptiveScheduler.h>
#include <crucible/concurrent/AutoRouter.h>
#include <crucible/concurrent/Topology.h>
#include <crucible/effects/Computation.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/IsAllocClass.h>
#include <crucible/safety/IsHotPath.h>
#include <crucible/safety/IsNumericalTier.h>
#include <crucible/safety/IsResidencyHeat.h>
#include <crucible/safety/IsWait.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace crucible::concurrent {

namespace autosplit_detail {

[[nodiscard]] constexpr std::size_t saturating_mul(std::size_t a,
                                                   std::size_t b) noexcept {
    if (a == 0 || b == 0) return 0;
    constexpr std::size_t max = std::numeric_limits<std::size_t>::max();
    if (a > max / b) return max;
    return a * b;
}

[[nodiscard]] constexpr std::size_t ceil_div(std::size_t n,
                                             std::size_t d) noexcept {
    if (d == 0) return 0;
    return n / d + (n % d == 0 ? 0 : 1);
}

[[nodiscard]] constexpr std::size_t sanitized(std::size_t value,
                                              std::size_t fallback) noexcept {
    return value == 0 ? fallback : value;
}

[[nodiscard]] constexpr Tier classify_from_profile(
    std::size_t bytes,
    AutoRouteRuntimeProfile profile) noexcept {
    const std::size_t l2 = sanitized(profile.l2_per_core_bytes,
                                     conservative_cliff_l2_per_core);
    const std::size_t l1 = std::min<std::size_t>(32ULL * 1024ULL, l2);
    const std::size_t l3 = sanitized(profile.huge_bytes,
                                     16ULL * 1024ULL * 1024ULL);
    if (bytes < l1) return Tier::L1Resident;
    if (bytes < l2) return Tier::L2Resident;
    if (bytes < l3) return Tier::L3Resident;
    return Tier::DRAMBound;
}

[[nodiscard]] constexpr NumaPolicy numa_from_tier(Tier tier) noexcept {
    return tier == Tier::L3Resident ? NumaPolicy::NumaLocal
         : tier == Tier::DRAMBound  ? NumaPolicy::NumaSpread
                                    : NumaPolicy::NumaIgnore;
}

[[nodiscard]] constexpr std::uint64_t saturating_mul_u64(std::uint64_t a,
                                                          std::uint64_t b) noexcept {
    if (a == 0 || b == 0) return 0;
    constexpr std::uint64_t max = static_cast<std::uint64_t>(-1);
    if (a > max / b) return max;
    return a * b;
}

[[nodiscard]] constexpr std::uint64_t saturating_add_u64(std::uint64_t a,
                                                          std::uint64_t b) noexcept {
    constexpr std::uint64_t max = static_cast<std::uint64_t>(-1);
    return a > max - b ? max : a + b;
}

// Break-even model — whether the body's total compute justifies fanout
// of `shard_count` worker tasks against `dispatch_cost_ns` per shard.
//
// model:
//   sequential_ns = items * per_item_compute_ns
//   parallel_ns(F) = items / F * per_item_compute_ns + F * dispatch_cost_ns
// reject parallelism when parallel_ns >= sequential_ns * (1 - hysteresis).
//
// The hysteresis (10%) prevents flipping decisions across the cliff
// for marginal wins.  Returns true when the planner should override
// to shard_count = 1.
[[nodiscard]] constexpr bool break_even_prefers_sequential(
    std::size_t items,
    std::uint64_t per_item_compute_ns,
    std::uint64_t dispatch_cost_ns,
    std::size_t shard_count) noexcept {
    if (per_item_compute_ns == 0) return false;
    if (shard_count <= 1) return false;
    if (items == 0) return false;

    const std::uint64_t total_compute =
        saturating_mul_u64(items, per_item_compute_ns);
    const std::uint64_t par_overhead =
        saturating_mul_u64(shard_count, dispatch_cost_ns);
    const std::uint64_t par_compute = total_compute / shard_count;
    const std::uint64_t par_total =
        saturating_add_u64(par_compute, par_overhead);

    // 10% hysteresis — require parallel to save at least a tenth of seq.
    const std::uint64_t threshold = total_compute - total_compute / 10;
    return par_total >= threshold;
}

// Parallel efficiency model:
//
//   seq_wall      = items × per_item_ns
//   par_wall(F)   = items / F × per_item_ns + F × dispatch_cost_ns
//   par_cpu(F)    = par_wall(F) × F                  [F cores busy that long]
//   efficiency(F) = seq_wall / par_cpu(F)
//                 ∈ [0, 1]                            [1 = perfect linear scaling]
//
// Returned as integer percent ∈ [0, 100] to keep this constexpr-without-FP.
// Callers compare against a `min_efficiency_pct` threshold (default 70).
[[nodiscard]] constexpr std::uint32_t efficiency_pct(
    std::size_t items,
    std::uint64_t per_item_compute_ns,
    std::uint64_t dispatch_cost_ns,
    std::size_t shard_count) noexcept {
    if (shard_count <= 1) return 100;
    if (items == 0 || per_item_compute_ns == 0) return 0;

    const std::uint64_t seq_wall =
        saturating_mul_u64(items, per_item_compute_ns);
    const std::uint64_t par_compute = seq_wall / shard_count;
    const std::uint64_t par_overhead =
        saturating_mul_u64(shard_count, dispatch_cost_ns);
    const std::uint64_t par_wall =
        saturating_add_u64(par_compute, par_overhead);
    const std::uint64_t par_cpu =
        saturating_mul_u64(par_wall, shard_count);

    if (par_cpu == 0) return 100;
    // efficiency = seq_wall / par_cpu, scaled to percent.
    const std::uint64_t scaled = saturating_mul_u64(seq_wall, 100);
    const std::uint64_t pct = scaled / par_cpu;
    return pct > 100 ? 100 : static_cast<std::uint32_t>(pct);
}

}  // namespace autosplit_detail

// Caller's parallelism appetite — declared at the type level via
// `AutoSplitWorkloadHint::intent`.  This is the **dial that controls the loss
// function**.  Wall-time-only optimization is the wrong default:
// system throughput suffers when 16 cores burn 5× CPU to win 10%
// wall time on one task.
enum class SchedulingIntent : std::uint8_t {
    // w_wall=1.0  w_cpu=0  w_p99=0.5
    // "I'm on a deadline; burn cores to hit it."
    LatencyCritical,

    // w_wall=0.3  w_cpu=1.0  w_amort=0.5
    // "Maximize items/sec; only fanout if efficiency >= min_efficiency_pct."
    Throughput,

    // w_wall=0  w_cpu=1.0  w_pool=10.0
    // "Steal idle cores only; never if pool is busy."
    Background,

    // w_wall=0.5  w_cpu=0.5  w_overlap=1.0
    // "I have follow-up work; overlap helps."
    Overlapped,

    // F = 1 always; no router involvement.
    Sequential,

    // Default: weights determined by current pool load.
    Adaptive,
};

enum class AutoSplitPartitionStrategy : std::uint8_t {
    Inline,
    EvenContiguous,
};

enum class AutoSplitScheduleMode : std::uint8_t {
    Inline,
    SyncForkJoin,
};

enum class AutoSplitPlacementPolicy : std::uint8_t {
    Caller,
    PoolAny,
    PoolNumaLocal,
    PoolNumaSpread,
};

enum class AutoSplitCompletionMode : std::uint8_t {
    None,
    BlockingWait,
};

struct AutoSplitRoutingDecision {
    AutoSplitPartitionStrategy partition = AutoSplitPartitionStrategy::Inline;
    AutoSplitScheduleMode      schedule = AutoSplitScheduleMode::Inline;
    AutoSplitPlacementPolicy   placement = AutoSplitPlacementPolicy::Caller;
    AutoSplitCompletionMode    completion = AutoSplitCompletionMode::None;
};

struct AutoSplitShapeCache {
    static constexpr std::size_t kSlotCount = 64;
    static constexpr std::uint8_t kPromotedHits = 4;

    struct Slot {
        std::atomic<std::uint64_t> key{0};
        std::atomic<std::uint32_t> packed{0};
    };

    std::array<Slot, kSlotCount> slots{};

    [[nodiscard]] static constexpr std::uint64_t mix_key(
        std::uint64_t key) noexcept {
        key ^= key >> 30;
        key *= 0xbf58476d1ce4e5b9ULL;
        key ^= key >> 27;
        key *= 0x94d049bb133111ebULL;
        key ^= key >> 31;
        return key == 0 ? 1 : key;
    }

    [[nodiscard]] static constexpr std::uint32_t pack(
        std::size_t factor,
        std::uint8_t hits,
        SchedulingIntent intent) noexcept {
        const auto f = static_cast<std::uint32_t>(
            std::min<std::size_t>(factor, 0xffU));
        return f
             | (static_cast<std::uint32_t>(hits) << 8)
             | (static_cast<std::uint32_t>(intent) << 16);
    }

    [[nodiscard]] static constexpr std::size_t factor_from(
        std::uint32_t packed) noexcept {
        return static_cast<std::size_t>(packed & 0xffU);
    }

    [[nodiscard]] static constexpr std::uint8_t hits_from(
        std::uint32_t packed) noexcept {
        return static_cast<std::uint8_t>((packed >> 8) & 0xffU);
    }

    [[nodiscard]] static constexpr SchedulingIntent intent_from(
        std::uint32_t packed) noexcept {
        return static_cast<SchedulingIntent>((packed >> 16) & 0xffU);
    }

    [[nodiscard]] std::size_t lookup_or(std::uint64_t key,
                                        SchedulingIntent intent,
                                        std::size_t fallback) const noexcept {
        const std::uint64_t mixed = mix_key(key);
        const Slot& slot = slots[slot_index_(mixed)];
        const std::uint64_t before =
            slot.key.load(std::memory_order_acquire);
        if (before != mixed) {
            return fallback;
        }

        const std::uint32_t packed =
            slot.packed.load(std::memory_order_acquire);
        const std::uint64_t after =
            slot.key.load(std::memory_order_acquire);
        if (before != after || after != mixed) {
            return fallback;
        }

        if (hits_from(packed) < kPromotedHits) return fallback;
        if (intent_from(packed) != intent) return fallback;

        const std::size_t factor = factor_from(packed);
        return factor == 0 ? fallback : factor;
    }

    void record(std::uint64_t key,
                SchedulingIntent intent,
                std::size_t factor) noexcept {
        const std::uint64_t mixed = mix_key(key);
        Slot& slot = slots[slot_index_(mixed)];
        const bool same_key =
            slot.key.load(std::memory_order_acquire) == mixed;
        const std::uint32_t old =
            same_key ? slot.packed.load(std::memory_order_acquire) : 0;
        const std::uint8_t old_hits = same_key ? hits_from(old) : 0;
        const std::uint8_t next_hits =
            old_hits == 0xffU ? old_hits
                              : static_cast<std::uint8_t>(old_hits + 1);

        if (!same_key) {
            slot.key.store(0, std::memory_order_release);
        }
        slot.packed.store(pack(factor, next_hits, intent),
                          std::memory_order_release);
        slot.key.store(mixed, std::memory_order_release);
    }

private:
    [[nodiscard]] static constexpr std::size_t slot_index_(
        std::uint64_t mixed) noexcept {
        static_assert((kSlotCount & (kSlotCount - 1)) == 0);
        return (mixed >> 3) & (kSlotCount - 1);
    }
};

struct AutoSplitOnlineCalibrator {
    std::atomic<std::uint64_t> dispatch_cost_ewma_ns{10'000};
    std::atomic<std::uint64_t> per_item_ewma_ns_x1000{0};
    std::atomic<std::uint64_t> samples{0};

    void record_dispatch(std::uint64_t observed_ns) noexcept {
        mix_(dispatch_cost_ewma_ns, observed_ns);
        samples.fetch_add(1, std::memory_order_relaxed);
    }

    void record_shard(std::uint64_t observed_ns,
                      std::size_t shard_items) noexcept {
        const std::size_t denom = std::max<std::size_t>(1, shard_items);
        const std::uint64_t whole = observed_ns / denom;
        const std::uint64_t rem = observed_ns % denom;
        const std::uint64_t scaled =
            autosplit_detail::saturating_add_u64(
                autosplit_detail::saturating_mul_u64(whole, 1000),
                autosplit_detail::saturating_mul_u64(rem, 1000) / denom);
        mix_(per_item_ewma_ns_x1000, scaled);
        samples.fetch_add(1, std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint64_t dispatch_cost_ns() const noexcept {
        return dispatch_cost_ewma_ns.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint64_t per_item_ns() const noexcept {
        return per_item_ewma_ns_x1000.load(std::memory_order_relaxed) / 1000;
    }

    [[nodiscard]] std::uint64_t sample_count() const noexcept {
        return samples.load(std::memory_order_relaxed);
    }

private:
    static void mix_(std::atomic<std::uint64_t>& target,
                     std::uint64_t observed) noexcept {
        const std::uint64_t old = target.load(std::memory_order_relaxed);
        const std::uint64_t weighted =
            autosplit_detail::saturating_mul_u64(old, 15);
        const std::uint64_t next =
            old == 0
                ? observed
                : autosplit_detail::saturating_add_u64(weighted, observed) / 16;
        target.store(next, std::memory_order_relaxed);
    }
};

struct AutoSplitRouterState {
    AutoSplitShapeCache cache{};
    AutoSplitOnlineCalibrator calibrator{};
};

// Runtime profile.  `dispatch_cost_ns` is the empirical per-shard cost
// of submitting a Pool task and joining on `wait_idle()` — observed
// from bench_auto_split.cpp at ~5-15 µs/shard on 4-core Pool<Fifo>.
// The default is conservative (10 µs/shard) — tasks must save at least
// 10% of sequential wall time after paying this cost to be picked.
//
// `min_efficiency_pct` gates parallel decisions for Throughput intent:
// a fanout F is acceptable only if `efficiency(F) >= min_efficiency_pct`.
// Default 70% — i.e. workers must on average do at least 70% useful work
// (not waiting on dispatch overhead).  Below that, fanout is a system-
// throughput regression even when wall time wins.
struct AutoSplitRuntimeProfile {
    AutoRouteRuntimeProfile route{};
    std::size_t      available_workers   = 1;
    std::uint64_t    dispatch_cost_ns    = 10'000;  // 10 µs / shard fanout
    std::uint32_t    min_efficiency_pct  = 70;       // 70% efficiency floor
};

namespace autosplit_detail {

[[nodiscard]] inline AutoSplitRuntimeProfile apply_pool_pressure(
    SchedulingIntent intent,
    AutoSplitRuntimeProfile profile,
    std::size_t idle_workers) noexcept {
    if (intent != SchedulingIntent::Background &&
        intent != SchedulingIntent::Adaptive) {
        return profile;
    }

    if (idle_workers == 0) {
        profile.available_workers = 1;
        return profile;
    }

    profile.available_workers = std::min(
        sanitized(profile.available_workers, 1), idle_workers);
    return profile;
}

}  // namespace autosplit_detail

// Request.  `per_item_compute_ns` is OPTIONAL; when zero, the planner
// uses the byte-tier rule alone (current behavior).  When nonzero, the
// planner runs break-even against `dispatch_cost_ns` and may downgrade
// shard_count to 1 if fanout doesn't pay.  This is how callers escape
// the byte-tier trap for memory-shaped workloads where per-item compute
// is far smaller than the bytes-touched would suggest.
struct AutoSplitRequest {
    std::size_t      item_count          = 0;
    std::size_t      bytes_per_item      = 0;
    std::size_t      max_shards          = 16;
    std::size_t      producers           = 1;
    std::size_t      consumers           = 1;
    std::uint64_t    per_item_compute_ns = 0;            // 0 = byte-tier rule only
    SchedulingIntent intent              = SchedulingIntent::Throughput;
    bool             touches_memory      = false;        // bandwidth-shaped work
    bool             is_io_bound         = false;         // wait/IO dominated work
};

[[nodiscard]] constexpr std::uint64_t auto_split_shape_key(
    AutoSplitRequest request,
    AutoSplitRuntimeProfile profile,
    std::uint64_t body_key = 0) noexcept {
    auto mix = [](std::uint64_t acc, std::uint64_t value) constexpr noexcept {
        value = AutoSplitShapeCache::mix_key(value);
        return acc ^ (value + 0x9E3779B97F4A7C15ULL + (acc << 6) + (acc >> 2));
    };

    std::uint64_t key = AutoSplitShapeCache::mix_key(body_key);
    key = mix(key, request.item_count);
    key = mix(key, request.bytes_per_item);
    key = mix(key, request.max_shards);
    key = mix(key, request.producers);
    key = mix(key, request.consumers);
    key = mix(key, request.per_item_compute_ns);
    key = mix(key, static_cast<std::uint64_t>(request.intent));
    key = mix(key, request.touches_memory ? 1U : 0U);
    key = mix(key, request.is_io_bound ? 1U : 0U);
    key = mix(key, profile.route.l2_per_core_bytes);
    key = mix(key, profile.route.huge_bytes);
    key = mix(key, profile.route.medium_shards);
    key = mix(key, profile.route.huge_shards);
    key = mix(key, profile.available_workers);
    key = mix(key, profile.dispatch_cost_ns);
    key = mix(key, profile.min_efficiency_pct);
    return key;
}

// ── Type-level routing — the cheap signal layer ──────────────────────
//
// The byte-tier rule + break-even gate are both *runtime* analyses that
// need numbers (bytes, ns/item, dispatch cost).  This layer sits BELOW
// them: it asks the type system what it already knows.
//
// A body type can:
//   • Specialize `workload_traits<Body>` to advertise its shape.
//   • Inherit from `AutoSplitWorkloadTagged<Hint{...}>` to declare directly.
//   • Get auto-inferred properties (is_empty_v → stateless → suggest
//     sequential; sizeof > 256 → heavy capture → cap shards) for free.
//
// When `dispatch_auto_split_typed(...)` is called, the planner consults
// the merged hint at compile time and adjusts the request before
// running the byte-tier rule.  Zero runtime cost — the entire trait
// pipeline is consteval.

enum class HintDirective : std::uint8_t {
    None,                  // No opinion — defer to byte-tier + break-even
    PreferSequential,      // Body wants inline; planner forces shard=1
    PreferParallel,        // Body wants fanout; planner skips break-even
    ByteTierWithCompute,   // Run byte-tier, apply break-even at per_item_ns
};

struct AutoSplitWorkloadHint {
    HintDirective    directive          = HintDirective::None;
    // Per-item cost estimate the body advertises.  Used by break-even
    // when directive==ByteTierWithCompute (or as a fallback when the
    // request didn't supply a hint).
    std::uint64_t    per_item_ns        = 0;
    // The maximum number of shards this body can usefully consume.
    // 0 = no opinion.  Used to clamp request.max_shards from above —
    // e.g. a body with heavy captures (sizeof > 256) might prefer
    // ≤ 4 shards to avoid 16× lambda copies on the queue.
    std::size_t      max_natural_shards = 0;
    // Caller's appetite for parallelism.  When the body's hint differs
    // from the request's intent, the request wins (caller knows context
    // the body author can't); the body's hint applies only when caller
    // didn't override.  Default Throughput keeps the system-throughput-
    // friendly efficiency gate active.
    SchedulingIntent intent             = SchedulingIntent::Throughput;
    // Body declares it's free of side effects — fanout always safe.
    bool             is_pure            = false;
    // Body touches memory significantly (mem-bound).  Hints that
    // parallel mem fanout helps hide DRAM latency.
    bool             touches_memory     = false;
    // Body has IO/Block effects.  Hints that the workload is latency-
    // bound, not compute-bound; can fan out PAST core_count.
    bool             is_io_bound        = false;
};

// Default trait — no opinion.  The router falls through to byte-tier.
template <typename Body>
struct workload_traits {
    [[nodiscard]] static constexpr AutoSplitWorkloadHint hint() noexcept {
        return AutoSplitWorkloadHint{};
    }
};

namespace autosplit_detail {

[[nodiscard]] consteval AutoSplitWorkloadHint merge_hints(
    AutoSplitWorkloadHint lhs,
    AutoSplitWorkloadHint rhs) noexcept {
    if (rhs.directive == HintDirective::PreferSequential ||
        lhs.directive == HintDirective::None) {
        lhs.directive = rhs.directive;
    } else if (lhs.directive == HintDirective::ByteTierWithCompute &&
               rhs.directive == HintDirective::PreferParallel) {
        lhs.directive = rhs.directive;
    }

    if (lhs.per_item_ns == 0) lhs.per_item_ns = rhs.per_item_ns;
    if (rhs.max_natural_shards != 0) {
        lhs.max_natural_shards =
            lhs.max_natural_shards == 0
                ? rhs.max_natural_shards
                : std::min(lhs.max_natural_shards, rhs.max_natural_shards);
    }
    if (rhs.intent != SchedulingIntent::Throughput) lhs.intent = rhs.intent;
    lhs.is_pure        = lhs.is_pure || rhs.is_pure;
    lhs.touches_memory = lhs.touches_memory || rhs.touches_memory;
    lhs.is_io_bound    = lhs.is_io_bound || rhs.is_io_bound;
    return lhs;
}

[[nodiscard]] consteval AutoSplitWorkloadHint normalize_hint(
    AutoSplitWorkloadHint hint) noexcept {
    if (hint.directive == HintDirective::PreferSequential) {
        hint.intent = SchedulingIntent::Sequential;
        hint.max_natural_shards = 1;
    }
    if (hint.is_io_bound &&
        hint.directive != HintDirective::PreferSequential &&
        hint.directive == HintDirective::None) {
        hint.directive = HintDirective::PreferParallel;
    }
    return hint;
}

template <typename Row>
[[nodiscard]] consteval AutoSplitWorkloadHint hint_from_effect_row() noexcept {
    namespace eff = ::crucible::effects;
    AutoSplitWorkloadHint hint{};

    if constexpr (eff::row_size_v<Row> == 0) {
        hint.is_pure = true;
    }
    if constexpr (eff::row_contains_v<Row, eff::Effect::Block> ||
                  eff::row_contains_v<Row, eff::Effect::IO>) {
        hint.is_io_bound = true;
        hint.directive = HintDirective::PreferParallel;
        hint.intent = SchedulingIntent::Overlapped;
    }
    if constexpr (eff::row_contains_v<Row, eff::Effect::Bg>) {
        hint.intent = SchedulingIntent::Background;
    }

    return hint;
}

template <std::size_t Bytes>
[[nodiscard]] consteval AutoSplitWorkloadHint
hint_from_ctx_workload_bytes() noexcept {
    AutoSplitWorkloadHint hint{};
    if constexpr (Bytes <= conservative_cliff_l2_per_core) {
        hint.directive = HintDirective::PreferSequential;
        hint.intent = SchedulingIntent::Sequential;
    } else {
        hint.touches_memory = true;
        if constexpr (Bytes >= (8ULL * 1024ULL * 1024ULL)) {
            hint.directive = HintDirective::PreferParallel;
        }
    }
    return hint;
}

template <typename Workload>
struct ctx_workload_hint_impl {
    [[nodiscard]] static consteval AutoSplitWorkloadHint hint() noexcept {
        return AutoSplitWorkloadHint{};
    }
};

template <std::size_t Bytes>
struct ctx_workload_hint_impl<
    ::crucible::effects::ctx_workload::ByteBudget<Bytes>> {
    [[nodiscard]] static consteval AutoSplitWorkloadHint hint() noexcept {
        return hint_from_ctx_workload_bytes<Bytes>();
    }
};

template <std::size_t Bytes,
          std::size_t Producers,
          std::size_t Consumers,
          bool LatestOnly>
struct ctx_workload_hint_impl<
    ::crucible::effects::ctx_workload::ChannelBudget<
        Bytes, Producers, Consumers, LatestOnly>> {
    [[nodiscard]] static consteval AutoSplitWorkloadHint hint() noexcept {
        (void)Producers;
        (void)Consumers;
        (void)LatestOnly;
        return hint_from_ctx_workload_bytes<Bytes>();
    }
};

template <std::size_t Items>
struct ctx_workload_hint_impl<
    ::crucible::effects::ctx_workload::ItemBudget<Items>> {
    [[nodiscard]] static consteval AutoSplitWorkloadHint hint() noexcept {
        AutoSplitWorkloadHint hint{};
        if constexpr (Items <= 1) {
            hint.directive = HintDirective::PreferSequential;
            hint.intent = SchedulingIntent::Sequential;
        }
        return hint;
    }
};

template <typename Workload>
[[nodiscard]] consteval AutoSplitWorkloadHint
hint_from_ctx_workload_axis() noexcept {
    return ctx_workload_hint_impl<Workload>::hint();
}

template <typename Heat, typename Resid, typename Row, typename Workload>
[[nodiscard]] consteval AutoSplitWorkloadHint hint_from_exec_ctx_axes() noexcept {
    namespace eff = ::crucible::effects;
    AutoSplitWorkloadHint hint = hint_from_effect_row<Row>();

    if constexpr (std::is_same_v<Heat, eff::ctx_heat::Hot> ||
                  std::is_same_v<Resid, eff::ctx_resid::L1> ||
                  std::is_same_v<Resid, eff::ctx_resid::L2>) {
        hint.directive = HintDirective::PreferSequential;
        hint.intent = SchedulingIntent::Sequential;
        hint.max_natural_shards = 1;
    } else if constexpr (std::is_same_v<Heat, eff::ctx_heat::Warm> ||
                         std::is_same_v<Resid, eff::ctx_resid::L3>) {
        if (hint.max_natural_shards == 0) hint.max_natural_shards = 4;
    } else if constexpr (std::is_same_v<Resid, eff::ctx_resid::DRAM>) {
        hint.touches_memory = true;
    }

    return merge_hints(hint, hint_from_ctx_workload_axis<Workload>());
}

template <typename Body>
concept DeclaresExecCtxType = requires {
    typename std::decay_t<Body>::exec_ctx_type;
};

template <typename Body>
concept HasExecCtxType =
    DeclaresExecCtxType<Body> &&
    ::crucible::effects::IsExecCtx<typename std::decay_t<Body>::exec_ctx_type>;

template <typename Body>
concept HasAutoSplitValueType = requires {
    typename std::decay_t<Body>::value_type;
};

}  // namespace autosplit_detail

template <::crucible::safety::HotPathTier_v Tier, typename T>
struct workload_traits<::crucible::safety::HotPath<Tier, T>> {
    [[nodiscard]] static constexpr AutoSplitWorkloadHint hint() noexcept {
        AutoSplitWorkloadHint h{};
        if constexpr (Tier == ::crucible::safety::HotPathTier_v::Hot) {
            h.directive = HintDirective::PreferSequential;
            h.intent = SchedulingIntent::Sequential;
            h.max_natural_shards = 1;
        } else if constexpr (Tier == ::crucible::safety::HotPathTier_v::Warm) {
            h.max_natural_shards = 4;
        } else {
            h.intent = SchedulingIntent::Background;
        }
        return h;
    }
};

template <::crucible::safety::ResidencyHeatTag_v Tier, typename T>
struct workload_traits<::crucible::safety::ResidencyHeat<Tier, T>> {
    [[nodiscard]] static constexpr AutoSplitWorkloadHint hint() noexcept {
        AutoSplitWorkloadHint h{};
        if constexpr (Tier == ::crucible::safety::ResidencyHeatTag_v::Hot) {
            h.directive = HintDirective::PreferSequential;
            h.intent = SchedulingIntent::Sequential;
            h.max_natural_shards = 1;
        } else if constexpr (Tier == ::crucible::safety::ResidencyHeatTag_v::Warm) {
            h.max_natural_shards = 4;
            h.touches_memory = true;
        } else {
            h.touches_memory = true;
        }
        return h;
    }
};

template <::crucible::safety::Tolerance Tier, typename T>
struct workload_traits<::crucible::safety::NumericalTier<Tier, T>> {
    [[nodiscard]] static constexpr AutoSplitWorkloadHint hint() noexcept {
        AutoSplitWorkloadHint h{};
        if constexpr (Tier == ::crucible::safety::Tolerance::BITEXACT) {
            h.directive = HintDirective::PreferSequential;
            h.intent = SchedulingIntent::Sequential;
            h.max_natural_shards = 1;
        }
        return h;
    }
};

template <::crucible::safety::WaitStrategy_v Strategy, typename T>
struct workload_traits<::crucible::safety::Wait<Strategy, T>> {
    [[nodiscard]] static constexpr AutoSplitWorkloadHint hint() noexcept {
        AutoSplitWorkloadHint h{};
        if constexpr (Strategy == ::crucible::safety::WaitStrategy_v::Block ||
                      Strategy == ::crucible::safety::WaitStrategy_v::Park ||
                      Strategy == ::crucible::safety::WaitStrategy_v::AcquireWait) {
            h.directive = HintDirective::PreferParallel;
            h.intent = SchedulingIntent::Overlapped;
            h.is_io_bound = true;
        } else {
            h.directive = HintDirective::PreferSequential;
            h.intent = SchedulingIntent::Sequential;
            h.max_natural_shards = 1;
        }
        return h;
    }
};

template <::crucible::safety::AllocClassTag_v Tag, typename T>
struct workload_traits<::crucible::safety::AllocClass<Tag, T>> {
    [[nodiscard]] static constexpr AutoSplitWorkloadHint hint() noexcept {
        AutoSplitWorkloadHint h{};
        if constexpr (Tag == ::crucible::safety::AllocClassTag_v::Stack ||
                      Tag == ::crucible::safety::AllocClassTag_v::Pool) {
            h.max_natural_shards = 1;
        } else if constexpr (Tag == ::crucible::safety::AllocClassTag_v::HugePage ||
                             Tag == ::crucible::safety::AllocClassTag_v::Mmap) {
            h.touches_memory = true;
        }
        return h;
    }
};

template <typename Row, typename T>
struct workload_traits<::crucible::effects::Computation<Row, T>> {
    [[nodiscard]] static constexpr AutoSplitWorkloadHint hint() noexcept {
        return autosplit_detail::hint_from_effect_row<Row>();
    }
};

template <class Cap, class Numa, class Alloc, class Heat,
          class Resid, class Row, class Workload>
struct workload_traits<
    ::crucible::effects::ExecCtx<Cap, Numa, Alloc, Heat, Resid, Row, Workload>> {
    [[nodiscard]] static constexpr AutoSplitWorkloadHint hint() noexcept {
        return autosplit_detail::hint_from_exec_ctx_axes<Heat, Resid, Row, Workload>();
    }
};

// CRTP-style base for bodies that want to declare a hint inline:
//   struct MyBody : AutoSplitWorkloadTagged<{.directive = HintDirective::PreferParallel}> { ... };
template <AutoSplitWorkloadHint H>
struct AutoSplitWorkloadTagged {
    [[nodiscard]] static constexpr AutoSplitWorkloadHint workload_hint() noexcept {
        return H;
    }
};

namespace autosplit_detail {

// Concept that fires when a Body inherits AutoSplitWorkloadTagged.
template <typename Body>
concept HasInlineWorkloadHint = requires {
    { std::decay_t<Body>::workload_hint() } -> std::same_as<AutoSplitWorkloadHint>;
};

}  // namespace autosplit_detail

// Merge: explicit specialization > CRTP-inherited > auto-inferred.
//
// Auto-inference rules (cheap, no opt-in needed):
//   • std::is_empty_v<Body> → stateless lambda / functor — no captures
//     means there's no per-instance state to fan out, suggest sequential.
//   • sizeof(Body) > 256    → heavy captures; cap shards at 4 to avoid
//     ballooning task-queue bytes when the lambda is copied N times.
//   • is_trivially_copyable_v<Body> → cheap to fan out (just memcpy).
template <typename Body>
[[nodiscard]] consteval AutoSplitWorkloadHint infer_workload_hint() noexcept {
    using B = std::decay_t<Body>;

    // 1. Check for explicit specialization of workload_traits.
    AutoSplitWorkloadHint hint = workload_traits<B>::hint();

    // 2. CRTP-inherited inline hint takes precedence over auto-inference.
    if constexpr (autosplit_detail::HasInlineWorkloadHint<B>) {
        const AutoSplitWorkloadHint inline_hint = B::workload_hint();
        hint = autosplit_detail::merge_hints(hint, inline_hint);
    }

    // 2b. Router-friendly body metadata.  Bodies can surface a
    // call-site context or payload wrapper without specializing the
    // whole body type:
    //
    //   using exec_ctx_type = effects::BgCompileCtx;
    //   using value_type    = safety::ResidencyHeat<Cold, Payload>;
    //
    // Both are pure type-level declarations and fold before runtime.
    if constexpr (autosplit_detail::DeclaresExecCtxType<B>) {
        static_assert(::crucible::effects::IsExecCtx<typename B::exec_ctx_type>,
                      "AutoSplit body exec_ctx_type must satisfy "
                      "crucible::effects::IsExecCtx; malformed context "
                      "metadata is not ignored");
        hint = autosplit_detail::merge_hints(
            hint, workload_traits<typename B::exec_ctx_type>::hint());
    }
    if constexpr (autosplit_detail::HasAutoSplitValueType<B>) {
        hint = autosplit_detail::merge_hints(
            hint, workload_traits<typename B::value_type>::hint());
    }

    // 3. Auto-inference from type properties — zero opt-in cost.
    if constexpr (std::is_empty_v<B>) {
        // Stateless body — no captures means no per-shard state to
        // distribute.  Probably a marker / no-op.  Default to seq.
        if (hint.directive == HintDirective::None) {
            hint.directive = HintDirective::PreferSequential;
        }
    }
    if constexpr (sizeof(B) > 256) {
        // Heavy captures; copying the body 16 times into the task queue
        // is wasteful.  Cap natural shards at 4 unless the body said
        // otherwise.
        if (hint.max_natural_shards == 0) {
            hint.max_natural_shards = 4;
        }
    }

    return autosplit_detail::normalize_hint(hint);
}

struct AutoSplitShard {
    std::size_t index = 0;
    std::size_t count = 0;
    std::size_t begin = 0;
    std::size_t end = 0;
    std::size_t byte_offset = 0;
    std::size_t byte_count = 0;
    NumaPolicy  numa = NumaPolicy::NumaIgnore;
    Tier        tier = Tier::L1Resident;

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        return end >= begin ? end - begin : 0;
    }

    [[nodiscard]] constexpr bool empty() const noexcept {
        return size() == 0;
    }
};

struct AutoSplitPlan {
    AutoSplitRequest request{};
    AutoRouteDecision route{};
    ParallelismDecision decision{};
    AutoSplitRoutingDecision routing{};
    std::size_t shard_count = 0;
    std::size_t total_items = 0;
    std::size_t bytes_per_item = 0;
    std::size_t total_bytes = 0;
    std::size_t grain_items = 0;

    [[nodiscard]] constexpr bool empty() const noexcept {
        return shard_count == 0 || total_items == 0;
    }

    [[nodiscard]] constexpr bool runs_inline() const noexcept {
        return shard_count <= 1;
    }

    [[nodiscard]] constexpr AutoSplitShard
    shard(std::size_t index) const noexcept {
        if (index >= shard_count || shard_count == 0) return {};

        const std::size_t base = total_items / shard_count;
        const std::size_t rem = total_items % shard_count;
        const std::size_t extra = index < rem ? 1 : 0;
        const std::size_t begin = index * base + std::min(index, rem);
        const std::size_t size = base + extra;
        const std::size_t end = begin + size;

        return AutoSplitShard{
            .index = index,
            .count = shard_count,
            .begin = begin,
            .end = end,
            .byte_offset = autosplit_detail::saturating_mul(begin, bytes_per_item),
            .byte_count = autosplit_detail::saturating_mul(size, bytes_per_item),
            .numa = decision.numa,
            .tier = decision.tier,
        };
    }
};

struct AutoSplitDispatchResult {
    AutoSplitPlan plan{};
    DispatchWithWorkloadResult dispatch{};
};

template <typename Job>
concept AutoSplitShardBody =
    std::copy_constructible<std::decay_t<Job>> &&
    std::is_invocable_r_v<void, std::decay_t<Job>&, AutoSplitShard>;

[[nodiscard]] inline const AutoSplitRuntimeProfile&
auto_split_runtime_profile_once() noexcept {
    static const AutoSplitRuntimeProfile profile = [] {
        const Topology& topology = Topology::instance();
        return AutoSplitRuntimeProfile{
            .route = AutoRouteRuntimeProfile{
                .l2_per_core_bytes = topology.l2_per_core_bytes(),
                .huge_bytes = topology.l3_total_bytes(),
                .medium_shards = 4,
                .huge_shards = 16,
            },
            .available_workers = std::max<std::size_t>(
                1, topology.process_cpu_count()),
            .dispatch_cost_ns = 10'000,  // 10 µs/shard — empirical Pool fanout cost
        };
    }();
    return profile;
}

[[nodiscard]] constexpr AutoSplitPlan
auto_split_plan(AutoSplitRequest request,
                AutoSplitRuntimeProfile profile = {}) noexcept {
    const std::size_t total_bytes =
        autosplit_detail::saturating_mul(request.item_count,
                                         request.bytes_per_item);
    const std::size_t max_shards =
        autosplit_detail::sanitized(request.max_shards, 1);
    const std::size_t worker_limit =
        autosplit_detail::sanitized(profile.available_workers, 1);
    const std::size_t producers =
        autosplit_detail::sanitized(request.producers, 1);
    const std::size_t consumers =
        autosplit_detail::sanitized(request.consumers, 1);
    const std::size_t hard_cap = std::max<std::size_t>(
        1, std::min(max_shards, worker_limit));
    const std::size_t l2 = autosplit_detail::sanitized(
        profile.route.l2_per_core_bytes, conservative_cliff_l2_per_core);
    const Tier request_tier =
        autosplit_detail::classify_from_profile(total_bytes, profile.route);
    const std::size_t bandwidth_min_bytes = std::max<std::size_t>(
        8ULL * 1024ULL * 1024ULL,
        autosplit_detail::saturating_mul(l2, 16));
    const bool memory_bandwidth_candidate =
        request.touches_memory &&
        request.intent != SchedulingIntent::Sequential &&
        request_tier != Tier::L1Resident &&
        request_tier != Tier::L2Resident &&
        total_bytes >= bandwidth_min_bytes;

    const AutoRouteDecision route =
        auto_route_decision_runtime(RouteIntent::Shardable,
                                    producers,
                                    consumers,
                                    total_bytes,
                                    hard_cap,
                                    profile.route);
    const std::size_t l2_fit =
        total_bytes == 0 ? 1 : autosplit_detail::ceil_div(total_bytes, l2);
    const std::size_t route_factor =
        route.sharded ? std::max(route.shard_factor, l2_fit) : 1;
    const std::size_t item_cap =
        request.item_count == 0 ? 0 : std::max<std::size_t>(1, request.item_count);
    std::size_t shard_count =
        item_cap == 0 ? 0 : std::min({route_factor, hard_cap, item_cap});

    // Intent gate.  Sequential intent always collapses; that's the
    // contract callers rely on for hot-path-typed bodies.
    if (request.intent == SchedulingIntent::Sequential) {
        shard_count = std::min<std::size_t>(shard_count, 1);
    }

    // Break-even gate.  When the caller supplies per_item_compute_ns,
    // override the byte-tier choice with a wall-time model: prefer
    // sequential when fanout overhead dominates the parallel speedup.
    // LatencyCritical intent SKIPS this gate — it accepts CPU cost in
    // exchange for wall-time wins.
    if (request.intent != SchedulingIntent::LatencyCritical &&
        !request.is_io_bound &&
        autosplit_detail::break_even_prefers_sequential(
            request.item_count,
            request.per_item_compute_ns,
            profile.dispatch_cost_ns,
            shard_count)) {
        shard_count = 1;
    }

    // Efficiency gate.  When the caller's intent values system
    // throughput (Throughput / Background / Adaptive), the planner
    // refuses fanout where parallel CPU efficiency drops below the
    // profile's floor — i.e. where workers spend more time on
    // dispatch/sync than on real work.
    //
    // Skipped for LatencyCritical (caller accepts low-efficiency
    // fanout to hit deadlines) and Sequential (already collapsed).
    //
    // Implementation walks down from `shard_count` to find the largest
    // F that meets the efficiency floor.  When per_item_compute_ns is
    // 0 (no compute hint), efficiency_pct returns 0 for every F > 1
    // and the gate would always force sequential — so we only apply
    // it when the caller actually provided a hint.
    if (request.per_item_compute_ns > 0 &&
        !memory_bandwidth_candidate &&
        !request.is_io_bound &&
        request.intent != SchedulingIntent::LatencyCritical &&
        request.intent != SchedulingIntent::Sequential) {
        while (shard_count > 1 &&
               autosplit_detail::efficiency_pct(
                   request.item_count,
                   request.per_item_compute_ns,
                   profile.dispatch_cost_ns,
                   shard_count) < profile.min_efficiency_pct) {
            shard_count >>= 1;  // halve and retry; converges in log2(F) steps
            if (shard_count == 0) shard_count = 1;
        }
    }

    const std::size_t grain =
        shard_count == 0
            ? 0
            : autosplit_detail::ceil_div(request.item_count, shard_count);

    ParallelismDecision decision{};
    decision.kind = shard_count > 1 ? ParallelismDecision::Kind::Parallel
                                    : ParallelismDecision::Kind::Sequential;
    decision.factor = std::max<std::size_t>(1, shard_count);
    decision.tier = request_tier;
    decision.numa = autosplit_detail::numa_from_tier(decision.tier);

    AutoSplitRoutingDecision routing{};
    if (shard_count > 1) {
        routing.partition = AutoSplitPartitionStrategy::EvenContiguous;
        routing.schedule = AutoSplitScheduleMode::SyncForkJoin;
        routing.completion = AutoSplitCompletionMode::BlockingWait;
        routing.placement =
            decision.numa == NumaPolicy::NumaLocal  ? AutoSplitPlacementPolicy::PoolNumaLocal
          : decision.numa == NumaPolicy::NumaSpread ? AutoSplitPlacementPolicy::PoolNumaSpread
                                                    : AutoSplitPlacementPolicy::PoolAny;
    }

    return AutoSplitPlan{
        .request = request,
        .route = route,
        .decision = decision,
        .routing = routing,
        .shard_count = shard_count,
        .total_items = request.item_count,
        .bytes_per_item = request.bytes_per_item,
        .total_bytes = total_bytes,
        .grain_items = grain,
    };
}

[[nodiscard]] inline AutoSplitPlan
auto_split_plan_runtime(AutoSplitRequest request) noexcept {
    return auto_split_plan(request, auto_split_runtime_profile_once());
}

// Build a plan with an EXPLICIT shard count, bypassing the byte-tier
// rule and the break-even model entirely.  Use this when the caller has
// out-of-band knowledge that overrides the planner — for example, A/B
// experiments comparing fixed factors against the router's choice.
//
// `factor` is clamped to `[1, item_count]`.  factor=0 collapses to an
// empty plan (consistent with `auto_split_plan` when item_count == 0).
[[nodiscard]] constexpr AutoSplitPlan
auto_split_plan_at_factor(AutoSplitRequest request,
                          std::size_t factor,
                          AutoSplitRuntimeProfile profile = {}) noexcept {
    if (request.item_count == 0 || factor == 0) {
        return AutoSplitPlan{};
    }
    const std::size_t shard_count = std::min(factor, request.item_count);
    const std::size_t total_bytes =
        autosplit_detail::saturating_mul(request.item_count,
                                         request.bytes_per_item);

    AutoRouteDecision route{
        .kind           = shard_count > 1 ? RouteKind::ShardedGrid
                                           : RouteKind::Spsc,
        .intent         = RouteIntent::Shardable,
        .topology       = shard_count > 1 ? ChannelTopology::ManyToMany
                                           : ChannelTopology::OneToOne,
        .producers      = autosplit_detail::sanitized(request.producers, 1),
        .consumers      = autosplit_detail::sanitized(request.consumers, 1),
        .workload_bytes = total_bytes,
        .shard_factor   = shard_count,
        .sharded        = shard_count > 1,
        .latest_only    = false,
    };

    ParallelismDecision decision{};
    decision.kind = shard_count > 1 ? ParallelismDecision::Kind::Parallel
                                    : ParallelismDecision::Kind::Sequential;
    decision.factor = shard_count;
    decision.tier = autosplit_detail::classify_from_profile(total_bytes,
                                                            profile.route);
    decision.numa = autosplit_detail::numa_from_tier(decision.tier);

    AutoSplitRoutingDecision routing{};
    if (shard_count > 1) {
        routing.partition = AutoSplitPartitionStrategy::EvenContiguous;
        routing.schedule = AutoSplitScheduleMode::SyncForkJoin;
        routing.placement =
            decision.numa == NumaPolicy::NumaLocal  ? AutoSplitPlacementPolicy::PoolNumaLocal
          : decision.numa == NumaPolicy::NumaSpread ? AutoSplitPlacementPolicy::PoolNumaSpread
                                                    : AutoSplitPlacementPolicy::PoolAny;
        routing.completion = AutoSplitCompletionMode::BlockingWait;
    }

    return AutoSplitPlan{
        .request = request,
        .route = route,
        .decision = decision,
        .routing = routing,
        .shard_count = shard_count,
        .total_items = request.item_count,
        .bytes_per_item = request.bytes_per_item,
        .total_bytes = total_bytes,
        .grain_items = autosplit_detail::ceil_div(request.item_count, shard_count),
    };
}

[[nodiscard]] inline AutoSplitPlan
auto_split_plan_cached(AutoSplitRequest request,
                       AutoSplitRuntimeProfile profile,
                       AutoSplitRouterState& state,
                       std::uint64_t body_key = 0) noexcept {
    const std::uint64_t key = auto_split_shape_key(request, profile, body_key);
    const std::size_t cached =
        state.cache.lookup_or(key, request.intent, 0);
    if (cached != 0) {
        return auto_split_plan_at_factor(request, cached, profile);
    }

    AutoSplitPlan plan = auto_split_plan(request, profile);
    state.cache.record(key, request.intent, plan.shard_count);
    return plan;
}

template <typename Policy, typename Job>
    requires scheduler::SchedulerPolicy<Policy, adaptive_detail::ticket_type> &&
             AutoSplitShardBody<Job>
[[nodiscard]] AutoSplitDispatchResult
dispatch_auto_split(Pool<Policy>& pool,
                    AutoSplitRequest request,
                    AutoSplitRuntimeProfile runtime_profile,
                    Job&& job) {
    const AutoSplitRuntimeProfile pressured_profile =
        autosplit_detail::apply_pool_pressure(
            request.intent, runtime_profile, pool.idle_workers_approx());
    const AutoSplitPlan plan = auto_split_plan(request, pressured_profile);
    return dispatch_auto_split_plan_(pool, plan, std::forward<Job>(job));
}

template <typename Policy, typename Job>
    requires scheduler::SchedulerPolicy<Policy, adaptive_detail::ticket_type> &&
             AutoSplitShardBody<Job>
[[nodiscard]] AutoSplitDispatchResult
dispatch_auto_split(Pool<Policy>& pool,
                    AutoSplitRequest request,
                    AutoSplitRuntimeProfile runtime_profile,
                    AutoSplitRouterState& router_state,
                    std::uint64_t body_key,
                    Job&& job) {
    const AutoSplitRuntimeProfile pressured_profile =
        autosplit_detail::apply_pool_pressure(
            request.intent, runtime_profile, pool.idle_workers_approx());
    const AutoSplitPlan plan = auto_split_plan_cached(
        request, pressured_profile, router_state, body_key);
    return dispatch_auto_split_plan_(pool, plan, std::forward<Job>(job));
}

template <typename Policy, typename Job>
    requires scheduler::SchedulerPolicy<Policy, adaptive_detail::ticket_type> &&
             AutoSplitShardBody<Job>
[[nodiscard]] AutoSplitDispatchResult
dispatch_auto_split_plan_(Pool<Policy>& pool,
                          AutoSplitPlan plan,
                          Job&& job) {
    if (plan.empty()) {
        return AutoSplitDispatchResult{
            .plan = plan,
            .dispatch = DispatchWithWorkloadResult{
                .decision = plan.decision,
                .ran_inline = true,
                .queued = false,
                .worker_limit = 0,
                .tasks_submitted = 0,
            },
        };
    }

    WorkloadProfile profile = WorkloadProfile::from_budget(
        WorkBudget{
            .read_bytes = plan.total_bytes,
            .write_bytes = plan.total_bytes,
            .item_count = plan.total_items,
        },
        plan.shard_count,
        plan.decision.numa);

    auto split_job = [plan, fn = std::decay_t<Job>{std::forward<Job>(job)}](
                         WorkShard worker) mutable {
        const std::size_t worker_count = std::max<std::size_t>(1, worker.count);
        for (std::size_t i = worker.index; i < plan.shard_count;
             i += worker_count) {
            fn(plan.shard(i));
        }
    };

    return AutoSplitDispatchResult{
        .plan = plan,
        .dispatch = dispatch_with_workload(pool, profile, std::move(split_job)),
    };
}

template <typename Policy, typename Job>
    requires scheduler::SchedulerPolicy<Policy, adaptive_detail::ticket_type> &&
             AutoSplitShardBody<Job>
[[nodiscard]] AutoSplitDispatchResult
dispatch_auto_split(Pool<Policy>& pool,
                    AutoSplitRequest request,
                    Job&& job) {
    return dispatch_auto_split(pool,
                               request,
                               auto_split_runtime_profile_once(),
                               std::forward<Job>(job));
}

// Dispatch a request through an explicit fixed `factor`, bypassing the
// router.  Used by A/B harnesses that compare router-chosen factors
// against handpicked factors.  factor=1 runs the body inline on the
// caller; factor>1 fans out to the pool with strided shard coverage.
template <typename Policy, typename Job>
    requires scheduler::SchedulerPolicy<Policy, adaptive_detail::ticket_type> &&
             AutoSplitShardBody<Job>
[[nodiscard]] AutoSplitDispatchResult
dispatch_at_factor(Pool<Policy>& pool,
                   AutoSplitRequest request,
                   std::size_t factor,
                   Job&& job) {
    const AutoSplitPlan plan = auto_split_plan_at_factor(request, factor);
    if (plan.empty()) {
        return AutoSplitDispatchResult{
            .plan = plan,
            .dispatch = DispatchWithWorkloadResult{
                .decision = plan.decision,
                .ran_inline = true,
                .queued = false,
                .worker_limit = 0,
                .tasks_submitted = 0,
            },
        };
    }
    if (plan.shard_count == 1) {
        std::decay_t<Job> body{std::forward<Job>(job)};
        body(plan.shard(0));
        return AutoSplitDispatchResult{
            .plan = plan,
            .dispatch = DispatchWithWorkloadResult{
                .decision = plan.decision,
                .ran_inline = true,
                .queued = false,
                .worker_limit = 1,
                .tasks_submitted = 1,
            },
        };
    }

    // Force exactly `plan.shard_count` workers via WorkloadProfile —
    // ParallelismRule::recommend may pick less, but we cap explicitly.
    WorkloadProfile profile = WorkloadProfile::from_budget(
        WorkBudget{
            .read_bytes = plan.total_bytes,
            .write_bytes = plan.total_bytes,
            .item_count = plan.total_items,
        },
        plan.shard_count,
        plan.decision.numa);

    // The split_job is self-balancing: when only ONE worker runs it
    // (because ParallelismRule::recommend collapses to Sequential
    // despite our cap), the strided loop still walks every shard
    // index.  So the body always sees every shard exactly once,
    // regardless of how many workers are active.
    auto split_job = [plan, fn = std::decay_t<Job>{std::forward<Job>(job)}](
                         WorkShard worker) mutable {
        const std::size_t worker_count = std::max<std::size_t>(1, worker.count);
        for (std::size_t i = worker.index; i < plan.shard_count;
             i += worker_count) {
            fn(plan.shard(i));
        }
    };

    return AutoSplitDispatchResult{
        .plan = plan,
        .dispatch = dispatch_with_workload(pool, profile, std::move(split_job)),
    };
}

// ── Type-level dispatch — the "router-friendly by construction" entry ──
//
// `dispatch_auto_split_typed<Body>(pool, request, body)` consults the
// body type's `AutoSplitWorkloadHint` AT COMPILE TIME and folds it into the
// request before running the planner.  The router becomes a stack of
// `if constexpr` checks the optimizer collapses; for bodies with a
// declared hint, the runtime cost approaches zero.
//
// Caller's request fields ALWAYS WIN over body hints — the body author
// can't know the calling context.  Body hint provides defaults when the
// caller didn't override.
//
// Tier 0 short-circuits land here:
//   • `is_empty_v<Body>`    → infer_workload_hint sets PreferSequential
//   • inline `AutoSplitWorkloadTagged<{...}>` → caller intent / per_item_ns
//   • `workload_traits<B>::hint()` → explicit specialization
//
// All three combine in `infer_workload_hint`; this function just
// applies the merged result to the request before calling the cost
// model.  Zero runtime cost beyond the existing `auto_split_plan`.

[[nodiscard]] constexpr AutoSplitRequest
merge_request_with_hint(AutoSplitRequest req,
                        AutoSplitWorkloadHint hint) noexcept {
    // Hint's directive forces the strongest signal it can:
    if (hint.directive == HintDirective::PreferSequential) {
        req.intent = SchedulingIntent::Sequential;
    }
    // Explicit Sequential intent always sticks; PreferParallel only
    // upgrades when the caller didn't already declare LatencyCritical
    // or Sequential (those carry user-meaningful semantics we don't
    // override).
    if (hint.directive == HintDirective::PreferParallel &&
        req.intent != SchedulingIntent::Sequential &&
        req.intent != SchedulingIntent::LatencyCritical) {
        req.intent = SchedulingIntent::LatencyCritical;
    }
    // Body's per-item ns fills in only when caller didn't supply one.
    if (req.per_item_compute_ns == 0 && hint.per_item_ns > 0) {
        req.per_item_compute_ns = hint.per_item_ns;
    }
    // Body's natural shard ceiling clamps the request from above.
    if (hint.max_natural_shards > 0) {
        req.max_shards = std::min(req.max_shards, hint.max_natural_shards);
    }
    req.touches_memory = req.touches_memory || hint.touches_memory;
    req.is_io_bound = req.is_io_bound || hint.is_io_bound;
    if (req.is_io_bound && req.intent != SchedulingIntent::Sequential) {
        req.intent = SchedulingIntent::Overlapped;
    }
    return req;
}

template <typename Policy, typename Job>
    requires scheduler::SchedulerPolicy<Policy, adaptive_detail::ticket_type> &&
             AutoSplitShardBody<Job>
[[nodiscard]] AutoSplitDispatchResult
dispatch_auto_split_typed(Pool<Policy>& pool,
                          AutoSplitRequest request,
                          AutoSplitRuntimeProfile runtime_profile,
                          Job&& job) {
    using Body = std::decay_t<Job>;
    constexpr AutoSplitWorkloadHint hint = infer_workload_hint<Body>();
    const AutoSplitRequest merged = merge_request_with_hint(request, hint);
    return dispatch_auto_split(pool, merged, runtime_profile,
                               std::forward<Job>(job));
}

template <typename Policy, typename Job>
    requires scheduler::SchedulerPolicy<Policy, adaptive_detail::ticket_type> &&
             AutoSplitShardBody<Job>
[[nodiscard]] AutoSplitDispatchResult
dispatch_auto_split_typed(Pool<Policy>& pool,
                          AutoSplitRequest request,
                          Job&& job) {
    return dispatch_auto_split_typed(pool, request,
                                     auto_split_runtime_profile_once(),
                                     std::forward<Job>(job));
}

// ── Plan-only typed query (no dispatch) ────────────────────────────
//
// Useful for benches and tests that need to inspect the plan without
// running it.  Same compile-time fold as dispatch_auto_split_typed.
template <typename Body>
[[nodiscard]] constexpr AutoSplitPlan
auto_split_plan_typed(AutoSplitRequest request,
                      AutoSplitRuntimeProfile profile = {}) noexcept {
    constexpr AutoSplitWorkloadHint hint = infer_workload_hint<Body>();
    return auto_split_plan(merge_request_with_hint(request, hint), profile);
}

}  // namespace crucible::concurrent
