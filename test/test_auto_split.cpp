#include <crucible/concurrent/AutoSplit.h>
#include <crucible/effects/Computation.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/HotPath.h>
#include <crucible/safety/NumericalTier.h>
#include <crucible/safety/ResidencyHeat.h>
#include <crucible/safety/Wait.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

namespace cc = crucible::concurrent;
namespace cs = crucible::concurrent::scheduler;

namespace {

constexpr std::size_t KiB = 1024;
constexpr std::size_t MiB = 1024 * KiB;

[[nodiscard]] constexpr cc::AutoSplitRuntimeProfile synthetic_profile(
    std::size_t workers = 8) noexcept {
    return cc::AutoSplitRuntimeProfile{
        .route = cc::AutoRouteRuntimeProfile{
            .l2_per_core_bytes = 256 * KiB,
            .huge_bytes = 2 * MiB,
            .medium_shards = 4,
            .huge_shards = 16,
        },
        .available_workers = workers,
    };
}

static void require(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "test_auto_split: %s\n", message);
        std::abort();
    }
}

[[maybe_unused]] static void test_zero_work_has_no_shards() {
    constexpr cc::AutoSplitPlan plan = cc::auto_split_plan(
        cc::AutoSplitRequest{
            .item_count = 0,
            .bytes_per_item = 64,
        },
        synthetic_profile());

    static_assert(plan.empty());
    static_assert(plan.shard_count == 0);
    static_assert(plan.shard(0).empty());
}

[[maybe_unused]] static void test_l2_resident_runs_inline() {
    constexpr cc::AutoSplitPlan plan = cc::auto_split_plan(
        cc::AutoSplitRequest{
            .item_count = 64,
            .bytes_per_item = 512,
        },
        synthetic_profile());

    static_assert(plan.runs_inline());
    static_assert(plan.shard_count == 1);
    static_assert(plan.routing.partition ==
                  cc::AutoSplitPartitionStrategy::Inline);
    static_assert(plan.routing.schedule ==
                  cc::AutoSplitScheduleMode::Inline);
    static_assert(plan.routing.placement ==
                  cc::AutoSplitPlacementPolicy::Caller);
    static_assert(plan.routing.completion ==
                  cc::AutoSplitCompletionMode::None);
    static_assert(plan.total_bytes == 32 * KiB);
    static_assert(plan.decision.kind ==
                  cc::ParallelismDecision::Kind::Sequential);

    constexpr cc::AutoSplitShard shard = plan.shard(0);
    static_assert(shard.begin == 0);
    static_assert(shard.end == 64);
    static_assert(shard.byte_offset == 0);
    static_assert(shard.byte_count == 32 * KiB);
}

static void test_uneven_shards_cover_once() {
    constexpr cc::AutoSplitPlan plan = cc::auto_split_plan(
        cc::AutoSplitRequest{
            .item_count = 1003,
            .bytes_per_item = 1024,
            .max_shards = 8,
        },
        synthetic_profile());

    static_assert(plan.shard_count == 4);
    static_assert(plan.routing.partition ==
                  cc::AutoSplitPartitionStrategy::EvenContiguous);
    static_assert(plan.routing.schedule ==
                  cc::AutoSplitScheduleMode::SyncForkJoin);
    static_assert(plan.routing.completion ==
                  cc::AutoSplitCompletionMode::BlockingWait);
    static_assert(plan.grain_items == 251);
    static_assert(plan.route.sharded);

    std::vector<int> coverage(plan.total_items, 0);
    std::size_t cursor = 0;
    for (std::size_t i = 0; i < plan.shard_count; ++i) {
        const cc::AutoSplitShard shard = plan.shard(i);
        require(shard.index == i, "shard index mismatch");
        require(shard.count == plan.shard_count, "shard count mismatch");
        require(shard.begin == cursor, "gap or overlap in shard ranges");
        for (std::size_t j = shard.begin; j < shard.end; ++j) {
            ++coverage[j];
        }
        cursor = shard.end;
    }

    require(cursor == plan.total_items, "final shard does not reach item_count");
    for (int visits : coverage) {
        require(visits == 1, "item coverage is not exactly once");
    }
}

[[maybe_unused]] static void test_worker_max_and_item_caps() {
    constexpr cc::AutoSplitPlan worker_capped = cc::auto_split_plan(
        cc::AutoSplitRequest{
            .item_count = 1'000'000,
            .bytes_per_item = 64,
            .max_shards = 16,
        },
        synthetic_profile(3));
    static_assert(worker_capped.shard_count == 3);
    static_assert(worker_capped.decision.factor == 3);

    constexpr cc::AutoSplitPlan max_capped = cc::auto_split_plan(
        cc::AutoSplitRequest{
            .item_count = 1'000'000,
            .bytes_per_item = 64,
            .max_shards = 5,
        },
        synthetic_profile(16));
    static_assert(max_capped.shard_count == 5);

    constexpr cc::AutoSplitPlan item_capped = cc::auto_split_plan(
        cc::AutoSplitRequest{
            .item_count = 3,
            .bytes_per_item = 1 * MiB,
            .max_shards = 16,
        },
        synthetic_profile(16));
    static_assert(item_capped.shard_count == 3);
}

[[maybe_unused]] static void test_overflow_saturates() {
    constexpr std::size_t max = static_cast<std::size_t>(-1);
    constexpr cc::AutoSplitPlan plan = cc::auto_split_plan(
        cc::AutoSplitRequest{
            .item_count = max,
            .bytes_per_item = 2,
            .max_shards = 16,
        },
        synthetic_profile(16));
    static_assert(plan.total_bytes == max);
    static_assert(plan.shard_count == 16);
}

static void test_dispatch_auto_split_covers_ranges() {
    constexpr std::size_t kItems = 4099;
    cc::Pool<cs::Fifo> pool{cc::CoreCount{2}};
    std::vector<std::atomic<int>> visits(kItems);

    const auto result = cc::dispatch_auto_split(
        pool,
        cc::AutoSplitRequest{
            .item_count = kItems,
            .bytes_per_item = 1024,
            .max_shards = 8,
        },
        synthetic_profile(8),
        [&visits](cc::AutoSplitShard shard) {
            for (std::size_t i = shard.begin; i < shard.end; ++i) {
                visits[i].fetch_add(1, std::memory_order_relaxed);
            }
        });

    pool.wait_idle();

    require(result.plan.shard_count == 8,
            "dispatch plan should keep the requested shard cap");
    require(result.dispatch.tasks_submitted != 0,
            "dispatch should submit or inline a scheduler job");
    require(pool.failed() == 0, "autosplit dispatch recorded a failure");
    for (const auto& visit : visits) {
        require(visit.load(std::memory_order_relaxed) == 1,
                "dispatch coverage is not exactly once");
    }
}

// Break-even gate: when per_item_compute_ns is small, the byte-tier
// rule wants to shard but break-even rejects it.  16 ns/item × 4096
// items = ~65 µs sequential vs 16-shard fanout costing 16 × 10µs =
// 160 µs overhead alone — sequential wins, planner must say so.
[[maybe_unused]] static void test_break_even_demotes_light_compute() {
    constexpr cc::AutoSplitPlan plan = cc::auto_split_plan(
        cc::AutoSplitRequest{
            .item_count = 4096,
            .bytes_per_item = 1024,           // 4 MiB → byte-tier wants huge fanout
            .max_shards = 16,
            .per_item_compute_ns = 16,        // ~65 µs total — too cheap to fan out
        },
        cc::AutoSplitRuntimeProfile{
            .route = cc::AutoRouteRuntimeProfile{
                .l2_per_core_bytes = 256 * KiB,
                .huge_bytes = 2 * MiB,
                .medium_shards = 4,
                .huge_shards = 16,
            },
            .available_workers = 16,
            .dispatch_cost_ns = 10'000,        // 10 µs/shard
        });

    static_assert(plan.shard_count == 1,
                  "break-even must demote light-compute workloads");
    static_assert(plan.runs_inline());
}

// Break-even gate AND efficiency gate together: when per_item_compute_ns
// is large enough that parallel pays off, the planner keeps a parallel
// decision.  But the EFFICIENCY GATE (default 70%) may downgrade from
// the byte-tier's max factor (16) to the largest factor that still
// achieves >= 70% parallel efficiency.
//
// Math: 4096 items × 500 ns = 2.048 ms sequential.
//   F=16: par_wall = 2048µs/16 + 16×10µs = 128 + 160 = 288 µs;
//         par_cpu = 288 × 16 = 4.6 ms; eff = 2048/4608 = 44%.  REJECTED.
//   F=8:  par_wall = 256 + 80 = 336 µs;
//         par_cpu = 336 × 8 = 2.69 ms; eff = 2048/2688 = 76%.  ACCEPTED.
//
// So the gate correctly halves 16 → 8 — 8 cores busy 336 µs each is
// system-throughput-better than 16 cores busy 288 µs each, even though
// the latter has slightly lower wall time.
[[maybe_unused]] static void test_break_even_keeps_heavy_compute() {
    constexpr cc::AutoSplitPlan plan = cc::auto_split_plan(
        cc::AutoSplitRequest{
            .item_count = 4096,
            .bytes_per_item = 1024,
            .max_shards = 16,
            .per_item_compute_ns = 500,        // 2 ms sequential — worth fanning
        },
        cc::AutoSplitRuntimeProfile{
            .route = cc::AutoRouteRuntimeProfile{
                .l2_per_core_bytes = 256 * KiB,
                .huge_bytes = 2 * MiB,
                .medium_shards = 4,
                .huge_shards = 16,
            },
            .available_workers = 16,
            .dispatch_cost_ns = 10'000,
        });

    // 8 (efficiency gate halved 16 → 8 to stay above 70% throughput)
    static_assert(plan.shard_count == 8,
                  "efficiency gate must halve until eff >= 70%");
    static_assert(plan.decision.kind ==
                  cc::ParallelismDecision::Kind::Parallel);
}

// Efficiency gate disabled by LatencyCritical intent: the same workload
// keeps F=16 because the caller declared "burn cores to hit deadline".
[[maybe_unused]] static void test_latency_critical_skips_efficiency_gate() {
    constexpr cc::AutoSplitPlan plan = cc::auto_split_plan(
        cc::AutoSplitRequest{
            .item_count = 4096,
            .bytes_per_item = 1024,
            .max_shards = 16,
            .per_item_compute_ns = 500,
            .intent = cc::SchedulingIntent::LatencyCritical,
        },
        cc::AutoSplitRuntimeProfile{
            .route = cc::AutoRouteRuntimeProfile{
                .l2_per_core_bytes = 256 * KiB,
                .huge_bytes = 2 * MiB,
                .medium_shards = 4,
                .huge_shards = 16,
            },
            .available_workers = 16,
            .dispatch_cost_ns = 10'000,
            .min_efficiency_pct = 70,
        });
    static_assert(plan.shard_count == 16,
                  "LatencyCritical must skip the efficiency gate");
}

// Sequential intent always collapses, regardless of byte-tier rule.
[[maybe_unused]] static void test_sequential_intent_always_collapses() {
    constexpr cc::AutoSplitPlan plan = cc::auto_split_plan(
        cc::AutoSplitRequest{
            .item_count = 1'000'000,
            .bytes_per_item = 64,
            .max_shards = 16,
            .intent = cc::SchedulingIntent::Sequential,
        },
        synthetic_profile(16));
    static_assert(plan.shard_count == 1);
    static_assert(plan.runs_inline());
}

// Tighter min_efficiency_pct rejects more fanout.  90% floor rejects
// even F=8 (76%) for the heavy-compute case → halves down to F=4 (88%).
[[maybe_unused]] static void test_higher_efficiency_floor_demotes_more() {
    constexpr cc::AutoSplitPlan plan = cc::auto_split_plan(
        cc::AutoSplitRequest{
            .item_count = 4096,
            .bytes_per_item = 1024,
            .max_shards = 16,
            .per_item_compute_ns = 500,
        },
        cc::AutoSplitRuntimeProfile{
            .route = cc::AutoRouteRuntimeProfile{
                .l2_per_core_bytes = 256 * KiB,
                .huge_bytes = 2 * MiB,
                .medium_shards = 4,
                .huge_shards = 16,
            },
            .available_workers = 16,
            .dispatch_cost_ns = 10'000,
            .min_efficiency_pct = 90,           // strict
        });
    // 2048 µs total compute, dispatch_cost = 10 µs.
    // F=8: par_wall = 256+80 = 336 µs; par_cpu = 2688 µs; eff = 76% < 90%. REJECT.
    // F=4: par_wall = 512+40 = 552 µs; par_cpu = 2208 µs; eff = 92.7% >= 90%. ACCEPT.
    static_assert(plan.shard_count == 4);
}

// Default behavior preserved: when per_item_compute_ns == 0 the planner
// falls back to the byte-tier rule unchanged.
[[maybe_unused]] static void test_break_even_disabled_when_compute_unspecified() {
    constexpr cc::AutoSplitPlan plan = cc::auto_split_plan(
        cc::AutoSplitRequest{
            .item_count = 4096,
            .bytes_per_item = 1024,
            .max_shards = 16,
            // per_item_compute_ns = 0 (default) — no break-even applied
        },
        synthetic_profile(16));

    static_assert(plan.shard_count == 16,
                  "byte-tier rule must run when no compute hint is provided");
}

[[maybe_unused]] static void test_memory_bandwidth_hint_skips_cpu_efficiency_gate() {
    constexpr cc::AutoSplitRuntimeProfile profile{
        .route = cc::AutoRouteRuntimeProfile{
            .l2_per_core_bytes = 256 * KiB,
            .huge_bytes = 2 * MiB,
            .medium_shards = 4,
            .huge_shards = 16,
        },
        .available_workers = 16,
        .dispatch_cost_ns = 10'000,
        .min_efficiency_pct = 70,
    };

    constexpr cc::AutoSplitRequest plain_memory{
        .item_count = 65536,
        .bytes_per_item = 256,        // 16 MiB total
        .max_shards = 16,
        .per_item_compute_ns = 3,     // bandwidth probe, not CPU body cost
        .intent = cc::SchedulingIntent::Throughput,
        .touches_memory = false,
    };
    constexpr cc::AutoSplitRequest tagged_memory{
        .item_count = plain_memory.item_count,
        .bytes_per_item = plain_memory.bytes_per_item,
        .max_shards = plain_memory.max_shards,
        .per_item_compute_ns = plain_memory.per_item_compute_ns,
        .intent = plain_memory.intent,
        .touches_memory = true,
    };

    constexpr cc::AutoSplitPlan plain_plan =
        cc::auto_split_plan(plain_memory, profile);
    constexpr cc::AutoSplitPlan tagged_plan =
        cc::auto_split_plan(tagged_memory, profile);

    static_assert(plain_plan.shard_count == 2,
                  "untyped memory keeps the CPU efficiency gate");
    static_assert(tagged_plan.shard_count == 16,
                  "large memory-bandwidth workloads must keep DRAM fanout");
}

// auto_split_plan_at_factor: bypasses both byte-tier and break-even.
// Forces shard_count = factor regardless of inputs.  Used by A/B
// harnesses to compare router-chosen factors against handpicked ones.
[[maybe_unused]] static void test_at_factor_bypasses_byte_tier() {
    // Tiny workload that the byte-tier rule would force to factor=1:
    constexpr cc::AutoSplitPlan forced_4 = cc::auto_split_plan_at_factor(
        cc::AutoSplitRequest{
            .item_count = 100,
            .bytes_per_item = 8,                // 800 B — well below L1
        },
        4);
    static_assert(forced_4.shard_count == 4);
    static_assert(forced_4.decision.kind ==
                  cc::ParallelismDecision::Kind::Parallel);
    static_assert(forced_4.route.sharded);

    // Factor capped by item_count when factor > items:
    constexpr cc::AutoSplitPlan capped = cc::auto_split_plan_at_factor(
        cc::AutoSplitRequest{
            .item_count = 3,
            .bytes_per_item = 1024,
        },
        16);
    static_assert(capped.shard_count == 3);

    // Factor=1 produces a sequential plan:
    constexpr cc::AutoSplitPlan one = cc::auto_split_plan_at_factor(
        cc::AutoSplitRequest{
            .item_count = 1000,
            .bytes_per_item = 64,
        },
        1);
    static_assert(one.shard_count == 1);
    static_assert(one.runs_inline());
    static_assert(!one.route.sharded);

    // Factor=0 collapses to empty:
    constexpr cc::AutoSplitPlan empty = cc::auto_split_plan_at_factor(
        cc::AutoSplitRequest{
            .item_count = 1000,
            .bytes_per_item = 64,
        },
        0);
    static_assert(empty.empty());
}

// Type-level routing: a stateless lambda is auto-inferred PreferSequential
// because std::is_empty_v<Body> is true.  Even with a huge byte footprint
// that the byte-tier rule would shard 16-ways, the typed planner returns
// shard_count == 1.
namespace typed_test_detail {
struct StatelessBody {
    void operator()(cc::AutoSplitShard) const noexcept {}
};
static_assert(std::is_empty_v<StatelessBody>);

// CRTP-tagged body with explicit Throughput intent + per_item_ns.
struct TaggedBody : cc::AutoSplitWorkloadTagged<cc::AutoSplitWorkloadHint{
    .directive = cc::HintDirective::None,
    .per_item_ns = 100,
    .max_natural_shards = 8,
    .intent = cc::SchedulingIntent::Throughput,
    .is_pure = true,
}> {
    void operator()(cc::AutoSplitShard) const noexcept {}
    int field = 0;  // not stateless, so empty_v auto-rule won't fire
};

using HotValue =
    crucible::safety::HotPath<crucible::safety::HotPathTier_v::Hot, int>;
using ColdResidency =
    crucible::safety::ResidencyHeat<
        crucible::safety::ResidencyHeatTag_v::Cold, int>;
using BitexactValue =
    crucible::safety::NumericalTier<
        crucible::safety::Tolerance::BITEXACT, int>;
using BlockingValue =
    crucible::safety::Wait<crucible::safety::WaitStrategy_v::Block, int>;
using BlockingComputation =
    crucible::effects::Computation<
        crucible::effects::Row<crucible::effects::Effect::Block>, int>;

using LargeBgMemoryCtx = crucible::effects::ExecCtx<
    crucible::effects::Bg,
    crucible::effects::ctx_numa::Spread,
    crucible::effects::ctx_alloc::Arena,
    crucible::effects::ctx_heat::Cold,
    crucible::effects::ctx_resid::DRAM,
    crucible::effects::Row<crucible::effects::Effect::Bg,
                           crucible::effects::Effect::Alloc>,
    crucible::effects::ctx_workload::ByteBudget<16 * MiB>>;

using LargeChannelBudgetCtx = crucible::effects::ExecCtx<
    crucible::effects::Bg,
    crucible::effects::ctx_numa::Spread,
    crucible::effects::ctx_alloc::Arena,
    crucible::effects::ctx_heat::Cold,
    crucible::effects::ctx_resid::DRAM,
    crucible::effects::Row<crucible::effects::Effect::Bg,
                           crucible::effects::Effect::Alloc>,
    crucible::effects::ctx_workload::ChannelBudget<16 * MiB, 4, 4, false>>;

struct CtxBody {
    using exec_ctx_type = LargeBgMemoryCtx;
    void operator()(cc::AutoSplitShard) const noexcept {}
    int non_empty = 0;
};

struct ChannelCtxBody {
    using exec_ctx_type = LargeChannelBudgetCtx;
    void operator()(cc::AutoSplitShard) const noexcept {}
    int non_empty = 0;
};

struct ColdResidencyBody {
    using value_type = ColdResidency;
    void operator()(cc::AutoSplitShard) const noexcept {}
    int non_empty = 0;
};

struct ParallelIoButBitexactBody : cc::AutoSplitWorkloadTagged<cc::AutoSplitWorkloadHint{
    .directive = cc::HintDirective::PreferParallel,
    .intent = cc::SchedulingIntent::Overlapped,
    .is_io_bound = true,
}> {
    using value_type = BitexactValue;
    void operator()(cc::AutoSplitShard) const noexcept {}
    int non_empty = 0;
};

struct SequentialButColdResidencyBody : cc::AutoSplitWorkloadTagged<cc::AutoSplitWorkloadHint{
    .directive = cc::HintDirective::PreferSequential,
    .intent = cc::SchedulingIntent::Sequential,
}> {
    using value_type = ColdResidency;
    void operator()(cc::AutoSplitShard) const noexcept {}
    int non_empty = 0;
};
}

[[maybe_unused]] static void test_typed_planner_is_empty_forces_sequential() {
    constexpr cc::AutoSplitPlan plan =
        cc::auto_split_plan_typed<typed_test_detail::StatelessBody>(
            cc::AutoSplitRequest{
                .item_count = 1'000'000,
                .bytes_per_item = 64,
                .max_shards = 16,
            },
            synthetic_profile(16));

    // is_empty_v<StatelessBody> → infer sets PreferSequential → merge sets
    // intent=Sequential → planner collapses to 1.
    static_assert(plan.shard_count == 1,
                  "stateless body must auto-route to sequential");
}

// CRTP body with Throughput intent + per_item_ns supplies break-even
// data the request didn't carry, and clamps max_shards to natural=8.
[[maybe_unused]] static void test_typed_planner_crtp_supplies_intent_and_per_item() {
    constexpr cc::AutoSplitPlan plan =
        cc::auto_split_plan_typed<typed_test_detail::TaggedBody>(
            cc::AutoSplitRequest{
                .item_count = 100'000,
                .bytes_per_item = 64,
                .max_shards = 16,             // body's max_natural_shards (8) clamps
                // No per_item_compute_ns; body supplies 100 via hint.
            },
            cc::AutoSplitRuntimeProfile{
                .route = cc::AutoRouteRuntimeProfile{
                    .l2_per_core_bytes = 256 * KiB,
                    .huge_bytes = 2 * MiB,
                    .medium_shards = 4,
                    .huge_shards = 16,
                },
                .available_workers = 16,
                .dispatch_cost_ns = 10'000,
                .min_efficiency_pct = 70,
            });
    // max_shards clamped to 8 by hint; efficiency gate may further demote.
    static_assert(plan.shard_count >= 1 && plan.shard_count <= 8,
                  "CRTP hint must clamp shard_count to <= 8");
}

[[maybe_unused]] static void test_substrate_wrapper_hints_are_read() {
    constexpr cc::AutoSplitWorkloadHint hot =
        cc::infer_workload_hint<typed_test_detail::HotValue>();
    static_assert(hot.directive == cc::HintDirective::PreferSequential);
    static_assert(hot.intent == cc::SchedulingIntent::Sequential);
    static_assert(hot.max_natural_shards == 1);

    constexpr cc::AutoSplitWorkloadHint cold =
        cc::infer_workload_hint<typed_test_detail::ColdResidency>();
    static_assert(cold.touches_memory);
    static_assert(cold.directive == cc::HintDirective::None);

    constexpr cc::AutoSplitWorkloadHint bitexact =
        cc::infer_workload_hint<typed_test_detail::BitexactValue>();
    static_assert(bitexact.directive == cc::HintDirective::PreferSequential);

    constexpr cc::AutoSplitWorkloadHint blocking =
        cc::infer_workload_hint<typed_test_detail::BlockingValue>();
    static_assert(blocking.directive == cc::HintDirective::PreferParallel);
    static_assert(blocking.is_io_bound);

    constexpr cc::AutoSplitWorkloadHint computation =
        cc::infer_workload_hint<typed_test_detail::BlockingComputation>();
    static_assert(computation.directive == cc::HintDirective::PreferParallel);
    static_assert(computation.is_io_bound);
}

[[maybe_unused]] static void test_typed_planner_reads_exec_ctx_and_value_type() {
    constexpr cc::AutoSplitRuntimeProfile profile{
        .route = cc::AutoRouteRuntimeProfile{
            .l2_per_core_bytes = 256 * KiB,
            .huge_bytes = 2 * MiB,
            .medium_shards = 4,
            .huge_shards = 16,
        },
        .available_workers = 16,
        .dispatch_cost_ns = 10'000,
        .min_efficiency_pct = 70,
    };

    constexpr cc::AutoSplitPlan ctx_plan =
        cc::auto_split_plan_typed<typed_test_detail::CtxBody>(
            cc::AutoSplitRequest{
                .item_count = 65536,
                .bytes_per_item = 256,
                .max_shards = 16,
                .per_item_compute_ns = 3,
            },
            profile);
    static_assert(ctx_plan.request.touches_memory);
    static_assert(ctx_plan.shard_count == 16,
                  "ExecCtx ByteBudget+DRAM should preserve memory fanout");

    constexpr cc::AutoSplitPlan channel_ctx_plan =
        cc::auto_split_plan_typed<typed_test_detail::ChannelCtxBody>(
            cc::AutoSplitRequest{
                .item_count = 65536,
                .bytes_per_item = 256,
                .max_shards = 16,
                .per_item_compute_ns = 3,
            },
            profile);
    static_assert(channel_ctx_plan.request.touches_memory);
    static_assert(channel_ctx_plan.shard_count == 16,
                  "ExecCtx ChannelBudget+DRAM should preserve memory fanout");

    constexpr cc::AutoSplitPlan value_plan =
        cc::auto_split_plan_typed<typed_test_detail::ColdResidencyBody>(
            cc::AutoSplitRequest{
                .item_count = 65536,
                .bytes_per_item = 256,
                .max_shards = 16,
                .per_item_compute_ns = 3,
            },
            profile);
    static_assert(value_plan.request.touches_memory);
    static_assert(value_plan.shard_count == 16,
                  "value_type ResidencyHeat<Cold> should preserve memory fanout");
}

[[maybe_unused]] static void test_typed_hint_axes_compose_before_directive_resolution() {
    constexpr cc::AutoSplitWorkloadHint bitexact_over_io =
        cc::infer_workload_hint<typed_test_detail::ParallelIoButBitexactBody>();
    static_assert(bitexact_over_io.directive ==
                  cc::HintDirective::PreferSequential);
    static_assert(bitexact_over_io.intent == cc::SchedulingIntent::Sequential);
    static_assert(bitexact_over_io.max_natural_shards == 1);
    static_assert(bitexact_over_io.is_io_bound,
                  "io-bound fact should still be retained diagnostically");

    constexpr cc::AutoSplitWorkloadHint seq_with_memory =
        cc::infer_workload_hint<typed_test_detail::SequentialButColdResidencyBody>();
    static_assert(seq_with_memory.directive ==
                  cc::HintDirective::PreferSequential);
    static_assert(seq_with_memory.touches_memory,
                  "value_type metadata must merge even when inline hint is sequential");

    constexpr cc::AutoSplitPlan plan =
        cc::auto_split_plan_typed<typed_test_detail::ParallelIoButBitexactBody>(
            cc::AutoSplitRequest{
                .item_count = 1'000'000,
                .bytes_per_item = 64,
                .max_shards = 16,
            },
            synthetic_profile(16));
    static_assert(plan.shard_count == 1,
                  "bitexact value_type must dominate parallel IO hint");
}

[[nodiscard]] static std::size_t cache_slot_for_test(
    std::uint64_t key) noexcept {
    const std::uint64_t mixed = cc::AutoSplitShapeCache::mix_key(key);
    return (mixed >> 3) & (cc::AutoSplitShapeCache::kSlotCount - 1);
}

[[nodiscard]] static std::uint64_t colliding_cache_key_for_test(
    std::uint64_t key) noexcept {
    const std::size_t slot = cache_slot_for_test(key);
    for (std::uint64_t candidate = key + 1;; ++candidate) {
        if (cache_slot_for_test(candidate) == slot) return candidate;
    }
}

// dispatch_at_factor: actually runs the body and verifies coverage.
static void test_dispatch_at_factor_covers_ranges() {
    constexpr std::size_t kItems = 1000;
    constexpr std::size_t kFactor = 7;
    cc::Pool<cs::Fifo> pool{cc::CoreCount{2}};
    std::vector<std::atomic<int>> visits(kItems);

    const auto result = cc::dispatch_at_factor(
        pool,
        cc::AutoSplitRequest{
            .item_count = kItems,
            .bytes_per_item = 64,
        },
        kFactor,
        [&visits](cc::AutoSplitShard shard) {
            for (std::size_t i = shard.begin; i < shard.end; ++i) {
                visits[i].fetch_add(1, std::memory_order_relaxed);
            }
        });

    pool.wait_idle();

    require(result.plan.shard_count == kFactor,
            "at_factor must produce exactly the requested shard count");
    require(pool.failed() == 0, "at_factor dispatch recorded a failure");
    for (std::size_t i = 0; i < kItems; ++i) {
        require(visits[i].load(std::memory_order_relaxed) == 1,
                "dispatch_at_factor coverage is not exactly once");
    }
}

static void test_shape_cache_promotes_after_repeated_hits() {
    cc::AutoSplitShapeCache cache;
    constexpr std::uint64_t key = 0x1234'5678'9ABC'DEF0ULL;
    constexpr std::uint64_t other_body_key = 0xCAFE'CAFE'CAFE'CAFEULL;

    require(cache.lookup_or(key, cc::SchedulingIntent::Throughput, 99) == 99,
            "empty shape cache must return fallback");

    for (std::uint8_t i = 0; i < cc::AutoSplitShapeCache::kPromotedHits - 1; ++i) {
        cache.record(key, cc::SchedulingIntent::Throughput, 8);
        require(cache.lookup_or(key, cc::SchedulingIntent::Throughput, 99) == 99,
                "shape cache must not serve before promotion threshold");
    }

    cache.record(key, cc::SchedulingIntent::Throughput, 8);
    require(cache.lookup_or(key, cc::SchedulingIntent::Throughput, 99) == 8,
            "shape cache must serve promoted factor");
    require(cache.lookup_or(key, cc::SchedulingIntent::LatencyCritical, 77) == 77,
            "shape cache intent mismatch must return fallback");
    require(cache.lookup_or(other_body_key, cc::SchedulingIntent::Throughput, 77) == 77,
            "shape cache body-key mismatch must return fallback");
}

static void test_shape_cache_concurrent_slot_overwrite_is_coherent() {
    cc::AutoSplitShapeCache cache;
    constexpr std::uint64_t key_a = 0x1234'5678'9ABC'DEF0ULL;
    const std::uint64_t key_b = colliding_cache_key_for_test(key_a);
    require(key_a != key_b, "test requires two distinct colliding keys");
    require(cache_slot_for_test(key_a) == cache_slot_for_test(key_b),
            "test keys must collide into one cache slot");

    std::atomic<bool> stop{false};
    std::atomic<bool> bad_read{false};

    std::thread writer{[&] {
        while (!stop.load(std::memory_order_acquire)) {
            cache.record(key_a, cc::SchedulingIntent::Throughput, 3);
            cache.record(key_b, cc::SchedulingIntent::Throughput, 11);
        }
    }};

    for (std::size_t i = 0; i < 200'000; ++i) {
        const std::size_t a =
            cache.lookup_or(key_a, cc::SchedulingIntent::Throughput, 99);
        const std::size_t b =
            cache.lookup_or(key_b, cc::SchedulingIntent::Throughput, 77);
        if (!((a == 3 || a == 99) && (b == 11 || b == 77))) {
            bad_read.store(true, std::memory_order_release);
            break;
        }
    }

    stop.store(true, std::memory_order_release);
    writer.join();
    require(!bad_read.load(std::memory_order_acquire),
            "shape cache returned a factor from a different colliding key");
}

static void test_cached_planner_matches_uncached_and_separates_shapes() {
    cc::AutoSplitRouterState state;
    constexpr cc::AutoSplitRuntimeProfile profile = synthetic_profile(16);
    constexpr cc::AutoSplitRequest heavy{
        .item_count = 4096,
        .bytes_per_item = 1024,
        .max_shards = 16,
        .per_item_compute_ns = 500,
        .intent = cc::SchedulingIntent::Throughput,
    };
    constexpr cc::AutoSplitRequest light{
        .item_count = 4096,
        .bytes_per_item = 1024,
        .max_shards = 16,
        .per_item_compute_ns = 16,
        .intent = cc::SchedulingIntent::Throughput,
    };

    constexpr cc::AutoSplitPlan heavy_uncached =
        cc::auto_split_plan(heavy, profile);
    constexpr cc::AutoSplitPlan light_uncached =
        cc::auto_split_plan(light, profile);
    static_assert(heavy_uncached.shard_count == 8);
    static_assert(light_uncached.shard_count == 1);

    for (std::uint8_t i = 0; i < cc::AutoSplitShapeCache::kPromotedHits; ++i) {
        const cc::AutoSplitPlan plan =
            cc::auto_split_plan_cached(heavy, profile, state, 0xAA55);
        require(plan.shard_count == heavy_uncached.shard_count,
                "cached planner must match uncached factor during promotion");
    }

    const cc::AutoSplitPlan promoted =
        cc::auto_split_plan_cached(heavy, profile, state, 0xAA55);
    require(promoted.shard_count == heavy_uncached.shard_count,
            "promoted cache must preserve heavy factor");

    const cc::AutoSplitPlan separated_shape =
        cc::auto_split_plan_cached(light, profile, state, 0xAA55);
    require(separated_shape.shard_count == light_uncached.shard_count,
            "cached planner must not reuse factor across request shapes");

    cc::AutoSplitRequest sequential = heavy;
    sequential.intent = cc::SchedulingIntent::Sequential;
    const cc::AutoSplitPlan separated_intent =
        cc::auto_split_plan_cached(sequential, profile, state, 0xAA55);
    require(separated_intent.shard_count == 1,
            "cached planner must not reuse factor across intents");

    const std::uint64_t heavy_key_a =
        cc::auto_split_shape_key(heavy, profile, 0xAA55);
    const std::uint64_t heavy_key_b =
        cc::auto_split_shape_key(heavy, profile, 0x55AA);
    require(heavy_key_a != heavy_key_b,
            "shape key must include body key");
}

static void test_online_calibrator_updates_ewma() {
    cc::AutoSplitOnlineCalibrator cal;
    require(cal.dispatch_cost_ns() == 10'000,
            "calibrator default dispatch cost changed");
    require(cal.per_item_ns() == 0,
            "calibrator default per-item estimate must be zero");
    require(cal.sample_count() == 0,
            "calibrator must start with zero samples");

    cal.record_dispatch(26'000);
    require(cal.dispatch_cost_ns() == 11'000,
            "dispatch EWMA must use alpha=1/16");
    require(cal.sample_count() == 1,
            "dispatch sample must increment sample counter");

    cal.record_shard(1'000, 10);
    require(cal.per_item_ns() == 100,
            "per-item EWMA must initialize from first shard sample");
    require(cal.sample_count() == 2,
            "shard sample must increment sample counter");

    cal.record_shard(2'600, 10);
    require(cal.per_item_ns() == 110,
            "per-item EWMA must mix subsequent shard sample");

    cal.record_dispatch(static_cast<std::uint64_t>(-1));
    require(cal.dispatch_cost_ns() > 1'000'000'000ULL,
            "dispatch EWMA must saturate instead of wrapping on huge samples");
}

static void test_background_intent_demotes_under_pool_pressure() {
    cc::Pool<cs::Fifo> pool{cc::CoreCount{1}};
    std::atomic<bool> blocker_started{false};
    std::atomic<bool> release_blocker{false};
    std::atomic<std::size_t> visited{0};

    cc::dispatch(pool, [&] {
        blocker_started.store(true, std::memory_order_release);
        while (!release_blocker.load(std::memory_order_acquire)) {
            CRUCIBLE_SPIN_PAUSE;
        }
    });

    while (!blocker_started.load(std::memory_order_acquire)) {
        CRUCIBLE_SPIN_PAUSE;
    }

    const auto result = cc::dispatch_auto_split(
        pool,
        cc::AutoSplitRequest{
            .item_count = 1'000'000,
            .bytes_per_item = 64,
            .max_shards = 16,
            .intent = cc::SchedulingIntent::Background,
        },
        synthetic_profile(16),
        [&visited](cc::AutoSplitShard shard) {
            visited.fetch_add(shard.size(), std::memory_order_relaxed);
        });

    require(result.plan.shard_count == 1,
            "Background intent must demote to inline when pool is busy");
    require(result.dispatch.ran_inline,
            "Background demotion should run inline instead of queueing");
    require(visited.load(std::memory_order_relaxed) == 1'000'000,
            "Background demotion must still cover the whole range");

    release_blocker.store(true, std::memory_order_release);
    pool.wait_idle();
    require(pool.failed() == 0, "pool recorded failure in pressure test");
}

}  // namespace

int main() {
    test_uneven_shards_cover_once();
    test_dispatch_auto_split_covers_ranges();
    test_dispatch_at_factor_covers_ranges();
    test_shape_cache_promotes_after_repeated_hits();
    test_shape_cache_concurrent_slot_overwrite_is_coherent();
    test_cached_planner_matches_uncached_and_separates_shapes();
    test_online_calibrator_updates_ewma();
    test_background_intent_demotes_under_pool_pressure();

    std::puts("test_auto_split: PASS");
    return 0;
}
