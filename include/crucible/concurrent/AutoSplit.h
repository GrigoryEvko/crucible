#pragma once

#include <crucible/concurrent/AdaptiveScheduler.h>
#include <crucible/concurrent/AutoRouter.h>
#include <crucible/concurrent/Topology.h>
#include <crucible/effects/_Computation.h>
#include <crucible/effects/_ExecCtx.h>
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

[[nodiscard]] constexpr std::size_t saturating_mul(std::size_t a, std::size_t b) noexcept {
    if (a == 0 || b == 0) return 0;
    constexpr std::size_t max = std::numeric_limits<std::size_t>::max();
    if (a > max / b) return max;
    return a * b;
}

[[nodiscard]] constexpr std::size_t ceil_div(std::size_t n, std::size_t d) noexcept {
    if (d == 0) return 0;
    return n / d + (n % d == 0 ? 0 : 1);
}

[[nodiscard]] constexpr std::size_t sanitized(std::size_t value, std::size_t fallback) noexcept {
    return value == 0 ? fallback : value;
}

[[nodiscard]] constexpr Tier classify_from_profile(std::size_t bytes, AutoRouteRuntimeProfile profile) noexcept {
    const std::size_t l2 = sanitized(profile.l2_per_core_bytes, conservative_cliff_l2_per_core);
    const std::size_t l1 = std::min<std::size_t>(32ULL * 1024ULL, l2);
    const std::size_t l3 = sanitized(profile.huge_bytes, 16ULL * 1024ULL * 1024ULL);
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

[[nodiscard]] constexpr std::uint64_t saturating_mul_u64(std::uint64_t a, std::uint64_t b) noexcept {
    if (a == 0 || b == 0) return 0;
    constexpr std::uint64_t max = static_cast<std::uint64_t>(-1);
    if (a > max / b) return max;
    return a * b;
}

[[nodiscard]] constexpr std::uint64_t saturating_add_u64(std::uint64_t a, std::uint64_t b) noexcept {
    constexpr std::uint64_t max = static_cast<std::uint64_t>(-1);
    return a > max - b ? max : a + b;
}

// Weighs the whole body's compute against the same work divided among the
// shards plus one dispatch per shard.  Parallelism has to win by a margin
// rather than merely win: without one, a workload sitting on the boundary
// flips between the two plans from one call to the next.
[[nodiscard]] constexpr bool break_even_prefers_sequential(std::size_t items, std::uint64_t per_item_compute_ns,
                                                           std::uint64_t dispatch_cost_ns,
                                                           std::size_t shard_count) noexcept {
    if (per_item_compute_ns == 0) return false;
    if (shard_count <= 1) return false;
    if (items == 0) return false;

    const std::uint64_t total_compute = saturating_mul_u64(items, per_item_compute_ns);
    const std::uint64_t par_overhead = saturating_mul_u64(shard_count, dispatch_cost_ns);
    const std::uint64_t par_compute = total_compute / shard_count;
    const std::uint64_t par_total = saturating_add_u64(par_compute, par_overhead);

    const std::uint64_t threshold = total_compute - total_compute / 10;
    return par_total >= threshold;
}

// The work the body actually has to do, over the CPU time the shards occupy
// between them.  It reaches one only when splitting the work costs nothing.
// Whole percent rather than a fraction, so the whole computation stays in
// integers and away from floating point.
[[nodiscard]] constexpr std::uint32_t efficiency_pct(std::size_t items, std::uint64_t per_item_compute_ns,
                                                     std::uint64_t dispatch_cost_ns, std::size_t shard_count) noexcept {
    if (shard_count <= 1) return 100;
    if (items == 0 || per_item_compute_ns == 0) return 0;

    const std::uint64_t seq_wall = saturating_mul_u64(items, per_item_compute_ns);
    const std::uint64_t par_compute = seq_wall / shard_count;
    const std::uint64_t par_overhead = saturating_mul_u64(shard_count, dispatch_cost_ns);
    const std::uint64_t par_wall = saturating_add_u64(par_compute, par_overhead);
    const std::uint64_t par_cpu = saturating_mul_u64(par_wall, shard_count);

    if (par_cpu == 0) return 100;
    const std::uint64_t scaled = saturating_mul_u64(seq_wall, 100);
    const std::uint64_t pct = scaled / par_cpu;
    return pct > 100 ? 100 : static_cast<std::uint32_t>(pct);
}

}  // namespace autosplit_detail

// What the caller wants optimized.  Wall time alone is the wrong default: a
// task that spends every core to shave a little off its own finish time takes
// that capacity from everything else running.
enum class SchedulingIntent : std::uint8_t {
    // Spend cores to meet a deadline.
    LatencyCritical,

    // Maximise items per second, and split only where the shards stay busy.
    Throughput,

    // Take idle cores and nothing else.
    Background,

    // The caller has work to overlap with, so splitting helps it too.
    Overlapped,

    // Never split.
    Sequential,

    // Decide from how loaded the pool is at the time.
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
    AutoSplitScheduleMode schedule = AutoSplitScheduleMode::Inline;
    AutoSplitPlacementPolicy placement = AutoSplitPlacementPolicy::Caller;
    AutoSplitCompletionMode completion = AutoSplitCompletionMode::None;
};

struct AutoSplitShapeCache {
    static constexpr std::size_t kSlotCount = 64;
    static constexpr std::uint8_t kPromotedHits = 4;

    struct Slot {
        std::atomic<std::uint64_t> key{0};
        std::atomic<std::uint32_t> packed{0};
    };

    std::array<Slot, kSlotCount> slots{};

    [[nodiscard]] static constexpr std::uint64_t mix_key(std::uint64_t key) noexcept {
        key ^= key >> 30;
        key *= 0xbf58476d1ce4e5b9ULL;
        key ^= key >> 27;
        key *= 0x94d049bb133111ebULL;
        key ^= key >> 31;
        return key == 0 ? 1 : key;
    }

    [[nodiscard]] static constexpr std::uint32_t pack(std::size_t factor, std::uint8_t hits,
                                                      SchedulingIntent intent) noexcept {
        const auto f = static_cast<std::uint32_t>(std::min<std::size_t>(factor, 0xffU));
        return f | (static_cast<std::uint32_t>(hits) << 8) | (static_cast<std::uint32_t>(intent) << 16);
    }

    [[nodiscard]] static constexpr std::size_t factor_from(std::uint32_t packed) noexcept {
        return static_cast<std::size_t>(packed & 0xffU);
    }

    [[nodiscard]] static constexpr std::uint8_t hits_from(std::uint32_t packed) noexcept {
        return static_cast<std::uint8_t>((packed >> 8) & 0xffU);
    }

    [[nodiscard]] static constexpr SchedulingIntent intent_from(std::uint32_t packed) noexcept {
        return static_cast<SchedulingIntent>((packed >> 16) & 0xffU);
    }

    [[nodiscard]] std::size_t lookup_or(std::uint64_t key, SchedulingIntent intent,
                                        std::size_t fallback) const noexcept {
        const std::uint64_t mixed = mix_key(key);
        const Slot& slot = slots[slot_index_(mixed)];
        const std::uint64_t before = slot.key.load(std::memory_order_acquire);
        if (before != mixed) {
            return fallback;
        }

        const std::uint32_t packed = slot.packed.load(std::memory_order_acquire);
        const std::uint64_t after = slot.key.load(std::memory_order_acquire);
        if (before != after || after != mixed) {
            return fallback;
        }

        if (hits_from(packed) < kPromotedHits) return fallback;
        if (intent_from(packed) != intent) return fallback;

        const std::size_t factor = factor_from(packed);
        return factor == 0 ? fallback : factor;
    }

    void record(std::uint64_t key, SchedulingIntent intent, std::size_t factor) noexcept {
        const std::uint64_t mixed = mix_key(key);
        Slot& slot = slots[slot_index_(mixed)];
        const bool same_key = slot.key.load(std::memory_order_acquire) == mixed;
        const std::uint32_t old = same_key ? slot.packed.load(std::memory_order_acquire) : 0;
        const std::uint8_t old_hits = same_key ? hits_from(old) : 0;
        const std::uint8_t next_hits = old_hits == 0xffU ? old_hits : static_cast<std::uint8_t>(old_hits + 1);

        if (!same_key) {
            slot.key.store(0, std::memory_order_release);
        }
        slot.packed.store(pack(factor, next_hits, intent), std::memory_order_release);
        slot.key.store(mixed, std::memory_order_release);
    }

private:
    [[nodiscard]] static constexpr std::size_t slot_index_(std::uint64_t mixed) noexcept {
        static_assert((kSlotCount & (kSlotCount - 1)) == 0);
        return (mixed >> 3) & (kSlotCount - 1);
    }
};

struct AutoSplitOnlineCalibrator {
    std::atomic<std::uint64_t> dispatch_cost_ewma_ns{10000};
    std::atomic<std::uint64_t> per_item_ewma_ns_x1000{0};
    std::atomic<std::uint64_t> samples{0};

    void record_dispatch(std::uint64_t observed_ns) noexcept {
        mix_(dispatch_cost_ewma_ns, observed_ns);
        samples.fetch_add(1, std::memory_order_relaxed);
    }

    void record_shard(std::uint64_t observed_ns, std::size_t shard_items) noexcept {
        const std::size_t denom = std::max<std::size_t>(1, shard_items);
        const std::uint64_t whole = observed_ns / denom;
        const std::uint64_t rem = observed_ns % denom;
        const std::uint64_t scaled = autosplit_detail::saturating_add_u64(
            autosplit_detail::saturating_mul_u64(whole, 1000), autosplit_detail::saturating_mul_u64(rem, 1000) / denom);
        mix_(per_item_ewma_ns_x1000, scaled);
        samples.fetch_add(1, std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint64_t dispatch_cost_ns() const noexcept {
        return dispatch_cost_ewma_ns.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint64_t per_item_ns() const noexcept {
        return per_item_ewma_ns_x1000.load(std::memory_order_relaxed) / 1000;
    }

    [[nodiscard]] std::uint64_t sample_count() const noexcept { return samples.load(std::memory_order_relaxed); }

private:
    // A plain load, compute and store would drop a sample whenever two
    // threads record at once.  That is tolerable for a moving average but not
    // reproducible, and the split-or-not decision reads this value.  The
    // compare-exchange retries instead, so every sample lands.  Release on
    // success publishes the new average, acquire on failure re-reads the
    // value the other writer just committed before recomputing from it.
    static void mix_(std::atomic<std::uint64_t>& target, std::uint64_t observed) noexcept {
        std::uint64_t old = target.load(std::memory_order_acquire);
        std::uint64_t next;
        do {
            // The arithmetic saturates: a long run of very large samples
            // pins the average at the maximum instead of wrapping to a small
            // number, which would flip the split-or-not decision mid-run.
            const std::uint64_t weighted = autosplit_detail::saturating_mul_u64(old, 15);
            next = (old == 0) ? observed : autosplit_detail::saturating_add_u64(weighted, observed) >> 4;
        } while (!target.compare_exchange_weak(old, next, std::memory_order_acq_rel, std::memory_order_acquire));
    }
};

struct AutoSplitRouterState {
    AutoSplitShapeCache cache{};
    AutoSplitOnlineCalibrator calibrator{};
};

// The dispatch cost is what it takes to hand one shard to the pool and later
// join it, measured rather than assumed.  Its default is deliberately on the
// high side, so a workload has to clear a real margin before it splits.
//
// The efficiency floor is the share of a worker's time that must go into the
// body rather than into being dispatched.  Below it, splitting costs the
// system more throughput than it returns to this caller, even where it does
// shorten this caller's wall time.
struct AutoSplitRuntimeProfile {
    AutoRouteRuntimeProfile route{};
    std::size_t available_workers = 1;
    std::uint64_t dispatch_cost_ns = 10000;
    std::uint32_t min_efficiency_pct = 70;
};

namespace autosplit_detail {

[[nodiscard]] inline AutoSplitRuntimeProfile
apply_pool_pressure(SchedulingIntent intent, AutoSplitRuntimeProfile profile, std::size_t idle_workers) noexcept {
    if (intent != SchedulingIntent::Background && intent != SchedulingIntent::Adaptive) {
        return profile;
    }

    if (idle_workers == 0) {
        profile.available_workers = 1;
        return profile;
    }

    profile.available_workers = std::min(sanitized(profile.available_workers, 1), idle_workers);
    return profile;
}

}  // namespace autosplit_detail

// A per-item compute estimate is optional.  Left at zero, the plan follows
// from the footprint alone.  Supplied, it also has to clear the break-even
// against dispatch cost.  That is the escape for work whose bytes touched
// suggest far more compute than it actually performs.
struct AutoSplitRequest {
    std::size_t item_count = 0;
    std::size_t bytes_per_item = 0;
    std::size_t max_shards = 16;
    std::size_t producers = 1;
    std::size_t consumers = 1;
    std::uint64_t per_item_compute_ns = 0;
    SchedulingIntent intent = SchedulingIntent::Throughput;
    bool touches_memory = false;
    bool is_io_bound = false;
};

[[nodiscard]] constexpr std::uint64_t auto_split_shape_key(AutoSplitRequest request, AutoSplitRuntimeProfile profile,
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

// The footprint rule and the break-even both need numbers only the call site
// has.  What follows sits underneath them and asks the type system what it
// already knows, before any of those numbers are needed.

enum class HintDirective : std::uint8_t {
    None,  // decide from the numbers alone
    PreferSequential,  // never split
    PreferParallel,  // split without consulting break-even
    ByteTierWithCompute,  // decide from the footprint, then check break-even
};

struct AutoSplitWorkloadHint {
    HintDirective directive = HintDirective::None;
    // What the body says one item costs, used when the call site says nothing.
    std::uint64_t per_item_ns = 0;
    // A ceiling the body puts on its own fanout, zero meaning no opinion.  A
    // body with large captures wants a low one, since each shard copies it.
    std::size_t max_natural_shards = 0;
    // Applies only where the call site left its own intent at the default.
    // The caller knows the context the body's author cannot.
    SchedulingIntent intent = SchedulingIntent::Throughput;
    bool is_pure = false;
    bool touches_memory = false;
    // Waiting rather than computing, so it can spread wider than the core
    // count.
    bool is_io_bound = false;
};

template <typename Body>
struct workload_traits {
    [[nodiscard]] static constexpr AutoSplitWorkloadHint hint() noexcept { return AutoSplitWorkloadHint{}; }
};

namespace autosplit_detail {

[[nodiscard]] consteval AutoSplitWorkloadHint merge_hints(AutoSplitWorkloadHint lhs,
                                                          AutoSplitWorkloadHint rhs) noexcept {
    if (rhs.directive == HintDirective::PreferSequential || lhs.directive == HintDirective::None) {
        lhs.directive = rhs.directive;
    } else if (lhs.directive == HintDirective::ByteTierWithCompute && rhs.directive == HintDirective::PreferParallel) {
        lhs.directive = rhs.directive;
    }

    if (lhs.per_item_ns == 0) lhs.per_item_ns = rhs.per_item_ns;
    if (rhs.max_natural_shards != 0) {
        lhs.max_natural_shards = lhs.max_natural_shards == 0 ? rhs.max_natural_shards
                                                             : std::min(lhs.max_natural_shards, rhs.max_natural_shards);
    }
    if (rhs.intent != SchedulingIntent::Throughput) lhs.intent = rhs.intent;
    lhs.is_pure = lhs.is_pure || rhs.is_pure;
    lhs.touches_memory = lhs.touches_memory || rhs.touches_memory;
    lhs.is_io_bound = lhs.is_io_bound || rhs.is_io_bound;
    return lhs;
}

[[nodiscard]] consteval AutoSplitWorkloadHint normalize_hint(AutoSplitWorkloadHint hint) noexcept {
    if (hint.directive == HintDirective::PreferSequential) {
        hint.intent = SchedulingIntent::Sequential;
        hint.max_natural_shards = 1;
    }
    if (hint.is_io_bound && hint.directive != HintDirective::PreferSequential
        && hint.directive == HintDirective::None) {
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
    if constexpr (eff::row_contains_v<Row, eff::Effect::Block> || eff::row_contains_v<Row, eff::Effect::IO>) {
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
[[nodiscard]] consteval AutoSplitWorkloadHint hint_from_ctx_workload_bytes() noexcept {
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
    [[nodiscard]] static consteval AutoSplitWorkloadHint hint() noexcept { return AutoSplitWorkloadHint{}; }
};

template <std::size_t Bytes>
struct ctx_workload_hint_impl<::crucible::effects::ctx_workload::ByteBudget<Bytes>> {
    [[nodiscard]] static consteval AutoSplitWorkloadHint hint() noexcept {
        return hint_from_ctx_workload_bytes<Bytes>();
    }
};

template <std::size_t Bytes, std::size_t Producers, std::size_t Consumers, bool LatestOnly>
struct ctx_workload_hint_impl<
    ::crucible::effects::ctx_workload::ChannelBudget<Bytes, Producers, Consumers, LatestOnly>> {
    [[nodiscard]] static consteval AutoSplitWorkloadHint hint() noexcept {
        (void)Producers;
        (void)Consumers;
        (void)LatestOnly;
        return hint_from_ctx_workload_bytes<Bytes>();
    }
};

template <std::size_t Items>
struct ctx_workload_hint_impl<::crucible::effects::ctx_workload::ItemBudget<Items>> {
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
[[nodiscard]] consteval AutoSplitWorkloadHint hint_from_ctx_workload_axis() noexcept {
    return ctx_workload_hint_impl<Workload>::hint();
}

template <typename Heat, typename Resid, typename Row, typename Workload>
[[nodiscard]] consteval AutoSplitWorkloadHint hint_from_exec_ctx_axes() noexcept {
    namespace eff = ::crucible::effects;
    AutoSplitWorkloadHint hint = hint_from_effect_row<Row>();

    if constexpr (std::is_same_v<Heat, eff::ctx_heat::Hot> || std::is_same_v<Resid, eff::ctx_resid::L1>
                  || std::is_same_v<Resid, eff::ctx_resid::L2>) {
        hint.directive = HintDirective::PreferSequential;
        hint.intent = SchedulingIntent::Sequential;
        hint.max_natural_shards = 1;
    } else if constexpr (std::is_same_v<Heat, eff::ctx_heat::Warm> || std::is_same_v<Resid, eff::ctx_resid::L3>) {
        if (hint.max_natural_shards == 0) hint.max_natural_shards = 4;
    } else if constexpr (std::is_same_v<Resid, eff::ctx_resid::DRAM>) {
        hint.touches_memory = true;
    }

    return merge_hints(hint, hint_from_ctx_workload_axis<Workload>());
}

template <typename Body>
concept DeclaresExecCtxType = requires { typename std::decay_t<Body>::exec_ctx_type; };

template <typename Body>
concept HasExecCtxType =
    DeclaresExecCtxType<Body> && ::crucible::effects::IsExecCtx<typename std::decay_t<Body>::exec_ctx_type>;

template <typename Body>
concept HasAutoSplitValueType = requires { typename std::decay_t<Body>::value_type; };

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
        if constexpr (Strategy == ::crucible::safety::WaitStrategy_v::Block
                      || Strategy == ::crucible::safety::WaitStrategy_v::Park
                      || Strategy == ::crucible::safety::WaitStrategy_v::AcquireWait) {
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
        if constexpr (Tag == ::crucible::safety::AllocClassTag_v::Stack
                      || Tag == ::crucible::safety::AllocClassTag_v::Pool) {
            h.max_natural_shards = 1;
        } else if constexpr (Tag == ::crucible::safety::AllocClassTag_v::HugePage
                             || Tag == ::crucible::safety::AllocClassTag_v::Mmap) {
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

template <class Cap, class Numa, class Alloc, class Heat, class Resid, class Row, class Workload, class Progress>
struct workload_traits<::crucible::effects::ExecCtx<Cap, Numa, Alloc, Heat, Resid, Row, Workload, Progress>> {
    [[nodiscard]] static constexpr AutoSplitWorkloadHint hint() noexcept {
        // The progress axis is deliberately not read here.  Whether a body
        // terminates says nothing about whether splitting it pays, and that
        // axis is answered against the context itself.
        return autosplit_detail::hint_from_exec_ctx_axes<Heat, Resid, Row, Workload>();
    }
};

template <AutoSplitWorkloadHint H>
struct AutoSplitWorkloadTagged {
    [[nodiscard]] static constexpr AutoSplitWorkloadHint workload_hint() noexcept { return H; }
};

namespace autosplit_detail {

template <typename Body>
concept HasInlineWorkloadHint = requires {
    { std::decay_t<Body>::workload_hint() } -> std::same_as<AutoSplitWorkloadHint>;
};

}  // namespace autosplit_detail

// An explicit trait beats an inherited hint, which beats what the body's own
// type gives away.
template <typename Body>
[[nodiscard]] consteval AutoSplitWorkloadHint infer_workload_hint() noexcept {
    using B = std::decay_t<Body>;

    AutoSplitWorkloadHint hint = workload_traits<B>::hint();

    if constexpr (autosplit_detail::HasInlineWorkloadHint<B>) {
        const AutoSplitWorkloadHint inline_hint = B::workload_hint();
        hint = autosplit_detail::merge_hints(hint, inline_hint);
    }

    // A body can name its context or its payload wrapper instead of
    // specializing the trait for its whole type.
    if constexpr (autosplit_detail::DeclaresExecCtxType<B>) {
        static_assert(::crucible::effects::IsExecCtx<typename B::exec_ctx_type>,
                      "AutoSplit body exec_ctx_type must satisfy "
                      "crucible::effects::IsExecCtx; malformed context "
                      "metadata is not ignored");
        hint = autosplit_detail::merge_hints(hint, workload_traits<typename B::exec_ctx_type>::hint());
    }
    if constexpr (autosplit_detail::HasAutoSplitValueType<B>) {
        hint = autosplit_detail::merge_hints(hint, workload_traits<typename B::value_type>::hint());
    }

    if constexpr (std::is_empty_v<B>) {
        // Nothing captured means nothing to divide between shards.
        if (hint.directive == HintDirective::None) {
            hint.directive = HintDirective::PreferSequential;
        }
    }
    if constexpr (sizeof(B) > 256) {
        // Large captures: each shard copies the body onto the queue, so keep
        // the count of copies down.
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
    NumaPolicy numa = NumaPolicy::NumaIgnore;
    Tier tier = Tier::L1Resident;

    [[nodiscard]] constexpr std::size_t size() const noexcept { return end >= begin ? end - begin : 0; }

    [[nodiscard]] constexpr bool empty() const noexcept { return size() == 0; }
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

    [[nodiscard]] constexpr bool empty() const noexcept { return shard_count == 0 || total_items == 0; }

    [[nodiscard]] constexpr bool runs_inline() const noexcept { return shard_count <= 1; }

    [[nodiscard]] constexpr AutoSplitShard shard(std::size_t index) const noexcept {
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
    std::copy_constructible<std::decay_t<Job>> && std::is_invocable_r_v<void, std::decay_t<Job>&, AutoSplitShard>;

[[nodiscard]] inline const AutoSplitRuntimeProfile& auto_split_runtime_profile_once() noexcept {
    static const AutoSplitRuntimeProfile profile = [] {
        const Topology& topology = Topology::instance();
        return AutoSplitRuntimeProfile{
            .route =
                AutoRouteRuntimeProfile{
                    .l2_per_core_bytes = topology.l2_per_core_bytes(),
                    .huge_bytes = topology.l3_total_bytes(),
                    .medium_shards = 4,
                    .huge_shards = 16,
                },
            .available_workers = std::max<std::size_t>(1, topology.process_cpu_count()),
            .dispatch_cost_ns = 10000,
        };
    }();
    return profile;
}

[[nodiscard]] inline AutoSplitRuntimeProfile
auto_split_runtime_profile_from_topology(const Topology& topology = Topology::instance()) noexcept {
    return AutoSplitRuntimeProfile{
        .route = auto_route_runtime_profile_from_topology(topology),
        .available_workers = std::max<std::size_t>(1, topology.process_cpu_count()),
        .dispatch_cost_ns = 10000,
    };
}

[[nodiscard]] constexpr AutoSplitRuntimeProfile
auto_split_runtime_profile_from_topology_snapshot(Topology::Snapshot snapshot) noexcept {
    return AutoSplitRuntimeProfile{
        .route = auto_route_runtime_profile_from_topology_snapshot(snapshot),
        .available_workers = std::max<std::size_t>(1, snapshot.process_cpu_count),
        .dispatch_cost_ns = 10000,
    };
}

[[nodiscard]] inline AutoSplitRuntimeProfile auto_split_runtime_profile_refresh() noexcept {
    return auto_split_runtime_profile_from_topology_snapshot(Topology::instance().snapshot());
}

[[nodiscard]] inline AutoSplitRuntimeProfile auto_split_runtime_profile_reprobe() noexcept {
    return auto_split_runtime_profile_from_topology_snapshot(Topology::reprobe_snapshot());
}

[[nodiscard]] constexpr AutoRouteDecision normalize_auto_split_route(AutoRouteDecision route,
                                                                     std::size_t shard_count) noexcept {
    const std::size_t fanout = std::max<std::size_t>(1, shard_count);
    route.worker_fanout = fanout;
    route.uses_worker_fanout = shard_count > 1;
    if (!route.uses_worker_fanout) {
        route.kind = detail::route_kind_from_topology_runtime(route.channel_topology);
    } else {
        route.kind = RouteKind::ShardedGrid;
    }
    return route;
}

[[nodiscard]] constexpr AutoSplitPlan auto_split_plan(AutoSplitRequest request,
                                                      AutoSplitRuntimeProfile profile = {}) noexcept {
    const std::size_t total_bytes = autosplit_detail::saturating_mul(request.item_count, request.bytes_per_item);
    const std::size_t max_shards = autosplit_detail::sanitized(request.max_shards, 1);
    const std::size_t worker_limit = autosplit_detail::sanitized(profile.available_workers, 1);
    const std::size_t producers = autosplit_detail::sanitized(request.producers, 1);
    const std::size_t consumers = autosplit_detail::sanitized(request.consumers, 1);
    const std::size_t hard_cap = std::max<std::size_t>(1, std::min(max_shards, worker_limit));
    const std::size_t l2 = autosplit_detail::sanitized(profile.route.l2_per_core_bytes, conservative_cliff_l2_per_core);
    const Tier request_tier = autosplit_detail::classify_from_profile(total_bytes, profile.route);
    const std::size_t bandwidth_min_bytes =
        std::max<std::size_t>(8ULL * 1024ULL * 1024ULL, autosplit_detail::saturating_mul(l2, 16));
    const bool memory_bandwidth_candidate = request.touches_memory && request.intent != SchedulingIntent::Sequential
                                         && request_tier != Tier::L1Resident && request_tier != Tier::L2Resident
                                         && total_bytes >= bandwidth_min_bytes;

    const AutoRouteDecision route =
        auto_route_decision_runtime(RouteIntent::Shardable, producers, consumers, total_bytes, hard_cap, profile.route);
    const std::size_t l2_fit = total_bytes == 0 ? 1 : autosplit_detail::ceil_div(total_bytes, l2);
    const std::size_t route_factor = route.uses_worker_fanout ? std::max(route.worker_fanout, l2_fit) : 1;
    const std::size_t item_cap = request.item_count == 0 ? 0 : std::max<std::size_t>(1, request.item_count);
    std::size_t shard_count = item_cap == 0 ? 0 : std::min({route_factor, hard_cap, item_cap});

    // A sequential intent always collapses.  Bodies typed onto the hot path
    // rely on that.
    if (request.intent == SchedulingIntent::Sequential) {
        shard_count = std::min<std::size_t>(shard_count, 1);
    }

    // Where the caller gave a compute estimate, wall time overrides the
    // choice the footprint made.  A caller on a deadline skips this: it has
    // already said it will pay CPU for finish time.
    if (request.intent != SchedulingIntent::LatencyCritical && !request.is_io_bound
        && autosplit_detail::break_even_prefers_sequential(request.item_count, request.per_item_compute_ns,
                                                           profile.dispatch_cost_ns, shard_count)) {
        shard_count = 1;
    }

    // A caller that values system throughput refuses a split whose workers
    // would spend more time being dispatched than working.  The loop walks
    // down to the widest split that clears the floor.
    //
    // Without a compute estimate the efficiency is zero for every split, so
    // the gate would collapse everything: it runs only where the caller
    // supplied one.
    if (request.per_item_compute_ns > 0 && !memory_bandwidth_candidate && !request.is_io_bound
        && request.intent != SchedulingIntent::LatencyCritical && request.intent != SchedulingIntent::Sequential) {
        while (shard_count > 1
               && autosplit_detail::efficiency_pct(request.item_count, request.per_item_compute_ns,
                                                   profile.dispatch_cost_ns, shard_count)
                      < profile.min_efficiency_pct) {
            shard_count >>= 1;
            if (shard_count == 0) shard_count = 1;
        }
    }

    const std::size_t grain = shard_count == 0 ? 0 : autosplit_detail::ceil_div(request.item_count, shard_count);

    ParallelismDecision decision{};
    decision.kind = shard_count > 1 ? ParallelismDecision::Kind::Parallel : ParallelismDecision::Kind::Sequential;
    decision.factor = std::max<std::size_t>(1, shard_count);
    decision.tier = request_tier;
    decision.numa = autosplit_detail::numa_from_tier(decision.tier);

    AutoSplitRoutingDecision routing{};
    if (shard_count > 1) {
        routing.partition = AutoSplitPartitionStrategy::EvenContiguous;
        routing.schedule = AutoSplitScheduleMode::SyncForkJoin;
        routing.completion = AutoSplitCompletionMode::BlockingWait;
        routing.placement = decision.numa == NumaPolicy::NumaLocal  ? AutoSplitPlacementPolicy::PoolNumaLocal
                          : decision.numa == NumaPolicy::NumaSpread ? AutoSplitPlacementPolicy::PoolNumaSpread
                                                                    : AutoSplitPlacementPolicy::PoolAny;
    }

    const AutoRouteDecision normalized_route = normalize_auto_split_route(route, shard_count);

    return AutoSplitPlan{
        .request = request,
        .route = normalized_route,
        .decision = decision,
        .routing = routing,
        .shard_count = shard_count,
        .total_items = request.item_count,
        .bytes_per_item = request.bytes_per_item,
        .total_bytes = total_bytes,
        .grain_items = grain,
    };
}

[[nodiscard]] inline AutoSplitPlan auto_split_plan_runtime(AutoSplitRequest request) noexcept {
    return auto_split_plan(request, auto_split_runtime_profile_once());
}

// Takes the shard count as given and skips every gate above.  It exists for
// callers holding knowledge the planner does not, and for measuring a fixed
// count against the one the planner would have chosen.
[[nodiscard]] constexpr AutoSplitPlan auto_split_plan_at_factor(AutoSplitRequest request, std::size_t factor,
                                                                AutoSplitRuntimeProfile profile = {}) noexcept {
    if (request.item_count == 0 || factor == 0) {
        return AutoSplitPlan{};
    }
    const std::size_t shard_count = std::min(factor, request.item_count);
    const std::size_t total_bytes = autosplit_detail::saturating_mul(request.item_count, request.bytes_per_item);

    AutoRouteDecision route{
        .kind = shard_count > 1 ? RouteKind::ShardedGrid : RouteKind::Spsc,
        .intent = RouteIntent::Shardable,
        .channel_topology =
            detail::recommend_topology_runtime(autosplit_detail::sanitized(request.producers, 1),
                                               autosplit_detail::sanitized(request.consumers, 1), false),
        .channel_producers = autosplit_detail::sanitized(request.producers, 1),
        .channel_consumers = autosplit_detail::sanitized(request.consumers, 1),
        .workload_bytes = total_bytes,
        .worker_fanout = shard_count,
        .uses_worker_fanout = shard_count > 1,
        .latest_only = false,
    };
    route = normalize_auto_split_route(route, shard_count);

    ParallelismDecision decision{};
    decision.kind = shard_count > 1 ? ParallelismDecision::Kind::Parallel : ParallelismDecision::Kind::Sequential;
    decision.factor = shard_count;
    decision.tier = autosplit_detail::classify_from_profile(total_bytes, profile.route);
    decision.numa = autosplit_detail::numa_from_tier(decision.tier);

    AutoSplitRoutingDecision routing{};
    if (shard_count > 1) {
        routing.partition = AutoSplitPartitionStrategy::EvenContiguous;
        routing.schedule = AutoSplitScheduleMode::SyncForkJoin;
        routing.placement = decision.numa == NumaPolicy::NumaLocal  ? AutoSplitPlacementPolicy::PoolNumaLocal
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

[[nodiscard]] inline AutoSplitPlan auto_split_plan_cached(AutoSplitRequest request, AutoSplitRuntimeProfile profile,
                                                          AutoSplitRouterState& state,
                                                          std::uint64_t body_key = 0) noexcept {
    const std::uint64_t key = auto_split_shape_key(request, profile, body_key);
    const std::size_t cached = state.cache.lookup_or(key, request.intent, 0);
    if (cached != 0) {
        return auto_split_plan_at_factor(request, cached, profile);
    }

    AutoSplitPlan plan = auto_split_plan(request, profile);
    state.cache.record(key, request.intent, plan.shard_count);
    return plan;
}

template <typename Policy, typename Job>
    requires scheduler::SchedulerPolicy<Policy, adaptive_detail::ticket_type> && AutoSplitShardBody<Job>
[[nodiscard]] AutoSplitDispatchResult dispatch_auto_split(Pool<Policy>& pool, AutoSplitRequest request,
                                                          AutoSplitRuntimeProfile runtime_profile, Job&& job) {
    const AutoSplitRuntimeProfile pressured_profile =
        autosplit_detail::apply_pool_pressure(request.intent, runtime_profile, pool.idle_workers_approx());
    const AutoSplitPlan plan = auto_split_plan(request, pressured_profile);
    return dispatch_auto_split_plan_(pool, plan, std::forward<Job>(job));
}

template <typename Policy, typename Job>
    requires scheduler::SchedulerPolicy<Policy, adaptive_detail::ticket_type> && AutoSplitShardBody<Job>
[[nodiscard]] AutoSplitDispatchResult
dispatch_auto_split(Pool<Policy>& pool, AutoSplitRequest request, AutoSplitRuntimeProfile runtime_profile,
                    AutoSplitRouterState& router_state, std::uint64_t body_key, Job&& job) {
    const AutoSplitRuntimeProfile pressured_profile =
        autosplit_detail::apply_pool_pressure(request.intent, runtime_profile, pool.idle_workers_approx());
    const AutoSplitPlan plan = auto_split_plan_cached(request, pressured_profile, router_state, body_key);
    return dispatch_auto_split_plan_(pool, plan, std::forward<Job>(job));
}

template <typename Policy, typename Job>
    requires scheduler::SchedulerPolicy<Policy, adaptive_detail::ticket_type> && AutoSplitShardBody<Job>
[[nodiscard]] AutoSplitDispatchResult dispatch_auto_split_plan_(Pool<Policy>& pool, AutoSplitPlan plan, Job&& job) {
    if (plan.empty()) {
        return AutoSplitDispatchResult{
            .plan = plan,
            .dispatch =
                DispatchWithWorkloadResult{
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
        plan.shard_count, plan.decision.numa);

    auto split_job = [plan, fn = std::decay_t<Job>{std::forward<Job>(job)}](WorkShard worker) mutable {
        const std::size_t worker_count = std::max<std::size_t>(1, worker.count);
        for (std::size_t i = worker.index; i < plan.shard_count; i += worker_count) {
            fn(plan.shard(i));
        }
    };

    return AutoSplitDispatchResult{
        .plan = plan,
        .dispatch = dispatch_with_workload(pool, profile, std::move(split_job)),
    };
}

template <typename Policy, typename Job>
    requires scheduler::SchedulerPolicy<Policy, adaptive_detail::ticket_type> && AutoSplitShardBody<Job>
[[nodiscard]] AutoSplitDispatchResult dispatch_auto_split(Pool<Policy>& pool, AutoSplitRequest request, Job&& job) {
    return dispatch_auto_split(pool, request, auto_split_runtime_profile_once(), std::forward<Job>(job));
}

// The dispatching counterpart of the fixed-count plan above.  A count of one
// runs the body on the calling thread.
template <typename Policy, typename Job>
    requires scheduler::SchedulerPolicy<Policy, adaptive_detail::ticket_type> && AutoSplitShardBody<Job>
[[nodiscard]] AutoSplitDispatchResult dispatch_at_factor(Pool<Policy>& pool, AutoSplitRequest request,
                                                         std::size_t factor, Job&& job) {
    const AutoSplitPlan plan = auto_split_plan_at_factor(request, factor);
    if (plan.empty()) {
        return AutoSplitDispatchResult{
            .plan = plan,
            .dispatch =
                DispatchWithWorkloadResult{
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
            .dispatch =
                DispatchWithWorkloadResult{
                    .decision = plan.decision,
                    .ran_inline = true,
                    .queued = false,
                    .worker_limit = 1,
                    .tasks_submitted = 1,
                },
        };
    }

    // Ask for exactly the planned count.  The pool's own rule may still
    // recommend fewer.
    WorkloadProfile profile = WorkloadProfile::from_budget(
        WorkBudget{
            .read_bytes = plan.total_bytes,
            .write_bytes = plan.total_bytes,
            .item_count = plan.total_items,
        },
        plan.shard_count, plan.decision.numa);

    // The stride makes the job self-balancing.  However many workers actually
    // run it, between them they walk every shard index exactly once, so a
    // single worker still covers the whole range.
    auto split_job = [plan, fn = std::decay_t<Job>{std::forward<Job>(job)}](WorkShard worker) mutable {
        const std::size_t worker_count = std::max<std::size_t>(1, worker.count);
        for (std::size_t i = worker.index; i < plan.shard_count; i += worker_count) {
            fn(plan.shard(i));
        }
    };

    return AutoSplitDispatchResult{
        .plan = plan,
        .dispatch = dispatch_with_workload(pool, profile, std::move(split_job)),
    };
}

// What the call site asked for wins over what the body's type suggests.  The
// body's author cannot know the context it is called from.

[[nodiscard]] constexpr AutoSplitRequest merge_request_with_hint(AutoSplitRequest req,
                                                                 AutoSplitWorkloadHint hint) noexcept {
    if (hint.directive == HintDirective::PreferSequential) {
        req.intent = SchedulingIntent::Sequential;
    }
    // A caller that named a deadline, or named sequential, meant it.  The
    // body's preference for splitting does not overrule either.
    if (hint.directive == HintDirective::PreferParallel && req.intent != SchedulingIntent::Sequential
        && req.intent != SchedulingIntent::LatencyCritical) {
        req.intent = SchedulingIntent::LatencyCritical;
    }
    if (req.per_item_compute_ns == 0 && hint.per_item_ns > 0) {
        req.per_item_compute_ns = hint.per_item_ns;
    }
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
    requires scheduler::SchedulerPolicy<Policy, adaptive_detail::ticket_type> && AutoSplitShardBody<Job>
[[nodiscard]] AutoSplitDispatchResult dispatch_auto_split_typed(Pool<Policy>& pool, AutoSplitRequest request,
                                                                AutoSplitRuntimeProfile runtime_profile, Job&& job) {
    using Body = std::decay_t<Job>;
    constexpr AutoSplitWorkloadHint hint = infer_workload_hint<Body>();
    const AutoSplitRequest merged = merge_request_with_hint(request, hint);
    return dispatch_auto_split(pool, merged, runtime_profile, std::forward<Job>(job));
}

template <typename Policy, typename Job>
    requires scheduler::SchedulerPolicy<Policy, adaptive_detail::ticket_type> && AutoSplitShardBody<Job>
[[nodiscard]] AutoSplitDispatchResult dispatch_auto_split_typed(Pool<Policy>& pool, AutoSplitRequest request,
                                                                Job&& job) {
    return dispatch_auto_split_typed(pool, request, auto_split_runtime_profile_once(), std::forward<Job>(job));
}

template <typename Body>
[[nodiscard]] constexpr AutoSplitPlan auto_split_plan_typed(AutoSplitRequest request,
                                                            AutoSplitRuntimeProfile profile = {}) noexcept {
    constexpr AutoSplitWorkloadHint hint = infer_workload_hint<Body>();
    return auto_split_plan(merge_request_with_hint(request, hint), profile);
}

}  // namespace crucible::concurrent
