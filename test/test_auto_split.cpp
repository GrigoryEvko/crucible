#include <crucible/concurrent/AutoSplit.h>
#include <crucible/effects/_Computation.h>
#include <crucible/effects/_ExecCtx.h>
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

[[nodiscard]] constexpr cc::AutoSplitRuntimeProfile synthetic_profile(std::size_t workers = 8) noexcept {
    return cc::AutoSplitRuntimeProfile{
        .route =
            cc::AutoRouteRuntimeProfile{
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
    static_assert(plan.routing.partition == cc::AutoSplitPartitionStrategy::Inline);
    static_assert(plan.routing.schedule == cc::AutoSplitScheduleMode::Inline);
    static_assert(plan.routing.placement == cc::AutoSplitPlacementPolicy::Caller);
    static_assert(plan.routing.completion == cc::AutoSplitCompletionMode::None);
    static_assert(plan.total_bytes == 32 * KiB);
    static_assert(plan.decision.kind == cc::ParallelismDecision::Kind::Sequential);

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
    static_assert(plan.routing.partition == cc::AutoSplitPartitionStrategy::EvenContiguous);
    static_assert(plan.routing.schedule == cc::AutoSplitScheduleMode::SyncForkJoin);
    static_assert(plan.routing.completion == cc::AutoSplitCompletionMode::BlockingWait);
    static_assert(plan.grain_items == 251);
    static_assert(plan.route.uses_worker_fanout);

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
            .item_count = 1000000,
            .bytes_per_item = 64,
            .max_shards = 16,
        },
        synthetic_profile(3));
    static_assert(worker_capped.shard_count == 3);
    static_assert(worker_capped.decision.factor == 3);

    constexpr cc::AutoSplitPlan max_capped = cc::auto_split_plan(
        cc::AutoSplitRequest{
            .item_count = 1000000,
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

    const auto result = cc::dispatch_auto_split(pool,
                                                cc::AutoSplitRequest{
                                                    .item_count = kItems,
                                                    .bytes_per_item = 1024,
                                                    .max_shards = 8,
                                                },
                                                synthetic_profile(8), [&visits](cc::AutoSplitShard shard) {
                                                    for (std::size_t i = shard.begin; i < shard.end; ++i) {
                                                        visits[i].fetch_add(1, std::memory_order_relaxed);
                                                    }
                                                });

    pool.wait_idle();

    require(result.plan.shard_count == 8, "dispatch plan should keep the requested shard cap");
    require(result.dispatch.tasks_submitted != 0, "dispatch should submit or inline a scheduler job");
    require(pool.failed() == 0, "autosplit dispatch recorded a failure");
    for (const auto& visit : visits) {
        require(visit.load(std::memory_order_relaxed) == 1, "dispatch coverage is not exactly once");
    }
}

// The byte tier wants to shard this, and break-even overrules it. Sequential
// work is 16 ns per item over 4096 items, about 65 µs. A sixteen-way fanout
// costs 160 µs in dispatch alone, so sequential wins outright.
[[maybe_unused]] static void test_break_even_demotes_light_compute() {
    constexpr cc::AutoSplitPlan plan = cc::auto_split_plan(
        cc::AutoSplitRequest{
            .item_count = 4096,
            .bytes_per_item = 1024,  // 4 MiB → byte-tier wants huge fanout
            .max_shards = 16,
            .per_item_compute_ns = 16,  // ~65 µs total — too cheap to fan out
        },
        cc::AutoSplitRuntimeProfile{
            .route =
                cc::AutoRouteRuntimeProfile{
                    .l2_per_core_bytes = 256 * KiB,
                    .huge_bytes = 2 * MiB,
                    .medium_shards = 4,
                    .huge_shards = 16,
                },
            .available_workers = 16,
            .dispatch_cost_ns = 10000,  // 10 µs/shard
        });

    static_assert(plan.shard_count == 1, "break-even must demote light-compute workloads");
    static_assert(plan.runs_inline());
    static_assert(!plan.route.uses_worker_fanout, "demoted plans must not retain a sharded route flag");
    static_assert(plan.route.worker_fanout == 1, "demoted plans must normalize route fanout to one");
}

// Here the work is heavy enough to pay for a fanout, so break-even keeps a
// parallel decision. The efficiency gate then picks the largest factor that
// still reaches its floor, which the profile leaves at seventy percent.
//
// Sequential work is 4096 items at 500 ns, or 2048 µs.
//   At 16: wall = 2048/16 + 16 x 10 = 288 µs, cpu = 4608 µs, efficiency 44%.
//   At 8:  wall = 2048/8  +  8 x 10 = 336 µs, cpu = 2688 µs, efficiency 76%.
//
// Sixteen is rejected even though it has the lower wall time. Eight cores
// busy for 336 µs leaves more of the machine free than sixteen cores busy for
// 288 µs, and the gate is about the system rather than this one call.
[[maybe_unused]] static void test_break_even_keeps_heavy_compute() {
    constexpr cc::AutoSplitPlan plan = cc::auto_split_plan(
        cc::AutoSplitRequest{
            .item_count = 4096,
            .bytes_per_item = 1024,
            .max_shards = 16,
            .per_item_compute_ns = 500,  // 2 ms sequential — worth fanning
        },
        cc::AutoSplitRuntimeProfile{
            .route =
                cc::AutoRouteRuntimeProfile{
                    .l2_per_core_bytes = 256 * KiB,
                    .huge_bytes = 2 * MiB,
                    .medium_shards = 4,
                    .huge_shards = 16,
                },
            .available_workers = 16,
            .dispatch_cost_ns = 10000,
        });

    static_assert(plan.shard_count == 8, "efficiency gate must halve until eff >= 70%");
    static_assert(plan.decision.kind == cc::ParallelismDecision::Kind::Parallel);
}

// The same workload keeps all sixteen shards here. A latency-critical caller
// has declared that spending cores to reach a deadline is the point, so the
// efficiency gate does not apply.
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
            .route =
                cc::AutoRouteRuntimeProfile{
                    .l2_per_core_bytes = 256 * KiB,
                    .huge_bytes = 2 * MiB,
                    .medium_shards = 4,
                    .huge_shards = 16,
                },
            .available_workers = 16,
            .dispatch_cost_ns = 10000,
            .min_efficiency_pct = 70,
        });
    static_assert(plan.shard_count == 16, "LatencyCritical must skip the efficiency gate");
}

// A sequential intent collapses the plan whatever the byte tier asks for.
[[maybe_unused]] static void test_sequential_intent_always_collapses() {
    constexpr cc::AutoSplitPlan plan = cc::auto_split_plan(
        cc::AutoSplitRequest{
            .item_count = 1000000,
            .bytes_per_item = 64,
            .max_shards = 16,
            .intent = cc::SchedulingIntent::Sequential,
        },
        synthetic_profile(16));
    static_assert(plan.shard_count == 1);
    static_assert(plan.runs_inline());
}

// A tighter floor rejects more fanout. The same heavy workload as above now
// loses the factor of eight as well.
[[maybe_unused]] static void test_higher_efficiency_floor_demotes_more() {
    constexpr cc::AutoSplitPlan plan = cc::auto_split_plan(
        cc::AutoSplitRequest{
            .item_count = 4096,
            .bytes_per_item = 1024,
            .max_shards = 16,
            .per_item_compute_ns = 500,
        },
        cc::AutoSplitRuntimeProfile{
            .route =
                cc::AutoRouteRuntimeProfile{
                    .l2_per_core_bytes = 256 * KiB,
                    .huge_bytes = 2 * MiB,
                    .medium_shards = 4,
                    .huge_shards = 16,
                },
            .available_workers = 16,
            .dispatch_cost_ns = 10000,
            .min_efficiency_pct = 90,
        });
    // Total compute is 2048 µs and each dispatch costs 10 µs.
    //   At 8: wall = 336 µs, cpu = 2688 µs, efficiency 76%, below the floor.
    //   At 4: wall = 552 µs, cpu = 2208 µs, efficiency 92.7%, above it.
    static_assert(plan.shard_count == 4);
}

[[maybe_unused]] static void test_break_even_disabled_when_compute_unspecified() {
    constexpr cc::AutoSplitPlan plan = cc::auto_split_plan(
        cc::AutoSplitRequest{
            .item_count = 4096,
            .bytes_per_item = 1024,
            .max_shards = 16,
            // The compute hint is left at its default of zero, which is what
            // switches break-even off.
        },
        synthetic_profile(16));

    static_assert(plan.shard_count == 16, "byte-tier rule must run when no compute hint is provided");
}

[[maybe_unused]] static void test_memory_bandwidth_hint_skips_cpu_efficiency_gate() {
    constexpr cc::AutoSplitRuntimeProfile profile{
        .route =
            cc::AutoRouteRuntimeProfile{
                .l2_per_core_bytes = 256 * KiB,
                .huge_bytes = 2 * MiB,
                .medium_shards = 4,
                .huge_shards = 16,
            },
        .available_workers = 16,
        .dispatch_cost_ns = 10000,
        .min_efficiency_pct = 70,
    };

    constexpr cc::AutoSplitRequest plain_memory{
        .item_count = 65536,
        .bytes_per_item = 256,  // 16 MiB total
        .max_shards = 16,
        .per_item_compute_ns = 3,  // a bandwidth probe, not a body cost
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

    constexpr cc::AutoSplitPlan plain_plan = cc::auto_split_plan(plain_memory, profile);
    constexpr cc::AutoSplitPlan tagged_plan = cc::auto_split_plan(tagged_memory, profile);

    static_assert(plain_plan.shard_count == 2, "untyped memory keeps the CPU efficiency gate");
    static_assert(tagged_plan.shard_count == 16, "large memory-bandwidth workloads must keep DRAM fanout");
}

// Planning at a fixed factor bypasses both the byte tier and break-even and
// takes the caller's number as given. An A and B harness uses it to measure a
// hand-picked factor against the one the router would have chosen.
[[maybe_unused]] static void test_at_factor_bypasses_byte_tier() {
    // A workload the byte tier would hold at one shard.
    constexpr cc::AutoSplitPlan forced_4 = cc::auto_split_plan_at_factor(
        cc::AutoSplitRequest{
            .item_count = 100,
            .bytes_per_item = 8,  // 800 B, far below L1
        },
        4);
    static_assert(forced_4.shard_count == 4);
    static_assert(forced_4.decision.kind == cc::ParallelismDecision::Kind::Parallel);
    static_assert(forced_4.route.uses_worker_fanout);

    // The item count still caps the factor.
    constexpr cc::AutoSplitPlan capped = cc::auto_split_plan_at_factor(
        cc::AutoSplitRequest{
            .item_count = 3,
            .bytes_per_item = 1024,
        },
        16);
    static_assert(capped.shard_count == 3);

    constexpr cc::AutoSplitPlan one = cc::auto_split_plan_at_factor(
        cc::AutoSplitRequest{
            .item_count = 1000,
            .bytes_per_item = 64,
        },
        1);
    static_assert(one.shard_count == 1);
    static_assert(one.runs_inline());
    static_assert(!one.route.uses_worker_fanout);
    static_assert(one.route.worker_fanout == 1);

    constexpr cc::AutoSplitPlan empty = cc::auto_split_plan_at_factor(
        cc::AutoSplitRequest{
            .item_count = 1000,
            .bytes_per_item = 64,
        },
        0);
    static_assert(empty.empty());
}

namespace typed_test_detail {
struct StatelessBody {
    void operator()(cc::AutoSplitShard) const noexcept {}
};
static_assert(std::is_empty_v<StatelessBody>);

struct TaggedBody : cc::AutoSplitWorkloadTagged<cc::AutoSplitWorkloadHint{
                        .directive = cc::HintDirective::None,
                        .per_item_ns = 100,
                        .max_natural_shards = 8,
                        .intent = cc::SchedulingIntent::Throughput,
                        .is_pure = true,
                    }> {
    void operator()(cc::AutoSplitShard) const noexcept {}
    int field = 0;  // keeps the body non-empty, so the stateless rule stays out
};

using HotValue = crucible::safety::HotPath<crucible::safety::HotPathTier_v::Hot, int>;
using ColdResidency = crucible::safety::ResidencyHeat<crucible::safety::ResidencyHeatTag_v::Cold, int>;
using BitexactValue = crucible::safety::NumericalTier<crucible::safety::Tolerance::BITEXACT, int>;
using BlockingValue = crucible::safety::Wait<crucible::safety::WaitStrategy_v::Block, int>;
using BlockingComputation =
    crucible::effects::Computation<crucible::effects::Row<crucible::effects::Effect::Block>, int>;

using LargeBgMemoryCtx =
    crucible::effects::ExecCtx<crucible::effects::Bg, crucible::effects::ctx_numa::Spread,
                               crucible::effects::ctx_alloc::Arena, crucible::effects::ctx_heat::Cold,
                               crucible::effects::ctx_resid::DRAM,
                               crucible::effects::Row<crucible::effects::Effect::Bg, crucible::effects::Effect::Alloc>,
                               crucible::effects::ctx_workload::ByteBudget<16 * MiB>>;

using LargeChannelBudgetCtx =
    crucible::effects::ExecCtx<crucible::effects::Bg, crucible::effects::ctx_numa::Spread,
                               crucible::effects::ctx_alloc::Arena, crucible::effects::ctx_heat::Cold,
                               crucible::effects::ctx_resid::DRAM,
                               crucible::effects::Row<crucible::effects::Effect::Bg, crucible::effects::Effect::Alloc>,
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
}  // namespace typed_test_detail

[[maybe_unused]] static void test_typed_planner_is_empty_forces_sequential() {
    constexpr cc::AutoSplitPlan plan = cc::auto_split_plan_typed<typed_test_detail::StatelessBody>(
        cc::AutoSplitRequest{
            .item_count = 1000000,
            .bytes_per_item = 64,
            .max_shards = 16,
        },
        synthetic_profile(16));

    // An empty body infers a sequential preference, which merges into a
    // sequential intent, which collapses the plan. The byte footprint here is
    // large enough that the byte tier alone would have asked for sixteen.
    static_assert(plan.shard_count == 1, "stateless body must auto-route to sequential");
}

// A tagged body carries the break-even data the request leaves out, and its
// natural shard count clamps the request's own ceiling.
[[maybe_unused]] static void test_typed_planner_crtp_supplies_intent_and_per_item() {
    constexpr cc::AutoSplitPlan plan = cc::auto_split_plan_typed<typed_test_detail::TaggedBody>(
        cc::AutoSplitRequest{
            .item_count = 100000,
            .bytes_per_item = 64,
            .max_shards = 16,  // the body clamps this to 8
            // The per-item cost is left out here; the body supplies it.
        },
        cc::AutoSplitRuntimeProfile{
            .route =
                cc::AutoRouteRuntimeProfile{
                    .l2_per_core_bytes = 256 * KiB,
                    .huge_bytes = 2 * MiB,
                    .medium_shards = 4,
                    .huge_shards = 16,
                },
            .available_workers = 16,
            .dispatch_cost_ns = 10000,
            .min_efficiency_pct = 70,
        });
    // The bound is a range rather than a value because the efficiency gate is
    // free to demote below the clamp.
    static_assert(plan.shard_count >= 1 && plan.shard_count <= 8, "CRTP hint must clamp shard_count to <= 8");
}

[[maybe_unused]] static void test_substrate_wrapper_hints_are_read() {
    constexpr cc::AutoSplitWorkloadHint hot = cc::infer_workload_hint<typed_test_detail::HotValue>();
    static_assert(hot.directive == cc::HintDirective::PreferSequential);
    static_assert(hot.intent == cc::SchedulingIntent::Sequential);
    static_assert(hot.max_natural_shards == 1);

    constexpr cc::AutoSplitWorkloadHint cold = cc::infer_workload_hint<typed_test_detail::ColdResidency>();
    static_assert(cold.touches_memory);
    static_assert(cold.directive == cc::HintDirective::None);

    constexpr cc::AutoSplitWorkloadHint bitexact = cc::infer_workload_hint<typed_test_detail::BitexactValue>();
    static_assert(bitexact.directive == cc::HintDirective::PreferSequential);

    constexpr cc::AutoSplitWorkloadHint blocking = cc::infer_workload_hint<typed_test_detail::BlockingValue>();
    static_assert(blocking.directive == cc::HintDirective::PreferParallel);
    static_assert(blocking.is_io_bound);

    constexpr cc::AutoSplitWorkloadHint computation = cc::infer_workload_hint<typed_test_detail::BlockingComputation>();
    static_assert(computation.directive == cc::HintDirective::PreferParallel);
    static_assert(computation.is_io_bound);
}

[[maybe_unused]] static void test_typed_planner_reads_exec_ctx_and_value_type() {
    constexpr cc::AutoSplitRuntimeProfile profile{
        .route =
            cc::AutoRouteRuntimeProfile{
                .l2_per_core_bytes = 256 * KiB,
                .huge_bytes = 2 * MiB,
                .medium_shards = 4,
                .huge_shards = 16,
            },
        .available_workers = 16,
        .dispatch_cost_ns = 10000,
        .min_efficiency_pct = 70,
    };

    constexpr cc::AutoSplitPlan ctx_plan = cc::auto_split_plan_typed<typed_test_detail::CtxBody>(
        cc::AutoSplitRequest{
            .item_count = 65536,
            .bytes_per_item = 256,
            .max_shards = 16,
            .per_item_compute_ns = 3,
        },
        profile);
    static_assert(ctx_plan.request.touches_memory);
    static_assert(ctx_plan.shard_count == 16, "ExecCtx ByteBudget+DRAM should preserve memory fanout");

    constexpr cc::AutoSplitPlan channel_ctx_plan = cc::auto_split_plan_typed<typed_test_detail::ChannelCtxBody>(
        cc::AutoSplitRequest{
            .item_count = 65536,
            .bytes_per_item = 256,
            .max_shards = 16,
            .per_item_compute_ns = 3,
        },
        profile);
    static_assert(channel_ctx_plan.request.touches_memory);
    static_assert(channel_ctx_plan.shard_count == 16, "ExecCtx ChannelBudget+DRAM should preserve memory fanout");

    constexpr cc::AutoSplitPlan value_plan = cc::auto_split_plan_typed<typed_test_detail::ColdResidencyBody>(
        cc::AutoSplitRequest{
            .item_count = 65536,
            .bytes_per_item = 256,
            .max_shards = 16,
            .per_item_compute_ns = 3,
        },
        profile);
    static_assert(value_plan.request.touches_memory);
    static_assert(value_plan.shard_count == 16, "value_type ResidencyHeat<Cold> should preserve memory fanout");
}

[[maybe_unused]] static void test_typed_hint_axes_compose_before_directive_resolution() {
    constexpr cc::AutoSplitWorkloadHint bitexact_over_io =
        cc::infer_workload_hint<typed_test_detail::ParallelIoButBitexactBody>();
    static_assert(bitexact_over_io.directive == cc::HintDirective::PreferSequential);
    static_assert(bitexact_over_io.intent == cc::SchedulingIntent::Sequential);
    static_assert(bitexact_over_io.max_natural_shards == 1);
    static_assert(bitexact_over_io.is_io_bound, "io-bound fact should still be retained diagnostically");

    constexpr cc::AutoSplitWorkloadHint seq_with_memory =
        cc::infer_workload_hint<typed_test_detail::SequentialButColdResidencyBody>();
    static_assert(seq_with_memory.directive == cc::HintDirective::PreferSequential);
    static_assert(seq_with_memory.touches_memory, "value_type metadata must merge even when inline hint is sequential");

    constexpr cc::AutoSplitPlan plan = cc::auto_split_plan_typed<typed_test_detail::ParallelIoButBitexactBody>(
        cc::AutoSplitRequest{
            .item_count = 1000000,
            .bytes_per_item = 64,
            .max_shards = 16,
        },
        synthetic_profile(16));
    static_assert(plan.shard_count == 1, "bitexact value_type must dominate parallel IO hint");
}

[[nodiscard]] static std::size_t cache_slot_for_test(std::uint64_t key) noexcept {
    const std::uint64_t mixed = cc::AutoSplitShapeCache::mix_key(key);
    return (mixed >> 3) & (cc::AutoSplitShapeCache::kSlotCount - 1);
}

[[nodiscard]] static std::uint64_t colliding_cache_key_for_test(std::uint64_t key) noexcept {
    const std::size_t slot = cache_slot_for_test(key);
    for (std::uint64_t candidate = key + 1;; ++candidate) {
        if (cache_slot_for_test(candidate) == slot) return candidate;
    }
}

static void test_dispatch_at_factor_covers_ranges() {
    constexpr std::size_t kItems = 1000;
    constexpr std::size_t kFactor = 7;
    cc::Pool<cs::Fifo> pool{cc::CoreCount{2}};
    std::vector<std::atomic<int>> visits(kItems);

    const auto result = cc::dispatch_at_factor(pool,
                                               cc::AutoSplitRequest{
                                                   .item_count = kItems,
                                                   .bytes_per_item = 64,
                                               },
                                               kFactor, [&visits](cc::AutoSplitShard shard) {
                                                   for (std::size_t i = shard.begin; i < shard.end; ++i) {
                                                       visits[i].fetch_add(1, std::memory_order_relaxed);
                                                   }
                                               });

    pool.wait_idle();

    require(result.plan.shard_count == kFactor, "at_factor must produce exactly the requested shard count");
    require(pool.failed() == 0, "at_factor dispatch recorded a failure");
    for (std::size_t i = 0; i < kItems; ++i) {
        require(visits[i].load(std::memory_order_relaxed) == 1, "dispatch_at_factor coverage is not exactly once");
    }
}

static void test_shape_cache_promotes_after_repeated_hits() {
    cc::AutoSplitShapeCache cache;
    constexpr std::uint64_t key = 0x123456789ABCDEF0ULL;
    constexpr std::uint64_t other_body_key = 0xCAFECAFECAFECAFEULL;

    require(cache.lookup_or(key, cc::SchedulingIntent::Throughput, 99) == 99, "empty shape cache must return fallback");

    for (std::uint8_t i = 0; i < cc::AutoSplitShapeCache::kPromotedHits - 1; ++i) {
        cache.record(key, cc::SchedulingIntent::Throughput, 8);
        require(cache.lookup_or(key, cc::SchedulingIntent::Throughput, 99) == 99,
                "shape cache must not serve before promotion threshold");
    }

    cache.record(key, cc::SchedulingIntent::Throughput, 8);
    require(cache.lookup_or(key, cc::SchedulingIntent::Throughput, 99) == 8, "shape cache must serve promoted factor");
    require(cache.lookup_or(key, cc::SchedulingIntent::LatencyCritical, 77) == 77,
            "shape cache intent mismatch must return fallback");
    require(cache.lookup_or(other_body_key, cc::SchedulingIntent::Throughput, 77) == 77,
            "shape cache body-key mismatch must return fallback");
}

static void test_shape_cache_concurrent_slot_overwrite_is_coherent() {
    cc::AutoSplitShapeCache cache;
    constexpr std::uint64_t key_a = 0x123456789ABCDEF0ULL;
    const std::uint64_t key_b = colliding_cache_key_for_test(key_a);
    require(key_a != key_b, "test requires two distinct colliding keys");
    require(cache_slot_for_test(key_a) == cache_slot_for_test(key_b), "test keys must collide into one cache slot");

    std::atomic<bool> stop{false};
    std::atomic<bool> bad_read{false};

    std::jthread writer{[&] {
        while (!stop.load(std::memory_order_acquire)) {
            cache.record(key_a, cc::SchedulingIntent::Throughput, 3);
            cache.record(key_b, cc::SchedulingIntent::Throughput, 11);
        }
    }};

    for (std::size_t i = 0; i < 200000; ++i) {
        const std::size_t a = cache.lookup_or(key_a, cc::SchedulingIntent::Throughput, 99);
        const std::size_t b = cache.lookup_or(key_b, cc::SchedulingIntent::Throughput, 77);
        if (!((a == 3 || a == 99) && (b == 11 || b == 77))) {
            bad_read.store(true, std::memory_order_release);
            break;
        }
    }

    stop.store(true, std::memory_order_release);
    writer.join();
    require(!bad_read.load(std::memory_order_acquire), "shape cache returned a factor from a different colliding key");
}

static void test_runtime_profile_refresh_and_reprobe() {
    const cc::AutoSplitRuntimeProfile refreshed = cc::auto_split_runtime_profile_refresh();
    const cc::AutoSplitRuntimeProfile reprobed = cc::auto_split_runtime_profile_reprobe();

    require(refreshed.available_workers >= 1, "refreshed profile must expose at least one worker");
    require(reprobed.available_workers >= 1, "reprobed profile must expose at least one worker");
    require(refreshed.route.l2_per_core_bytes > 0, "refreshed profile must expose nonzero L2");
    require(reprobed.route.l2_per_core_bytes > 0, "reprobed profile must expose nonzero L2");
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

    constexpr cc::AutoSplitPlan heavy_uncached = cc::auto_split_plan(heavy, profile);
    constexpr cc::AutoSplitPlan light_uncached = cc::auto_split_plan(light, profile);
    static_assert(heavy_uncached.shard_count == 8);
    static_assert(light_uncached.shard_count == 1);

    for (std::uint8_t i = 0; i < cc::AutoSplitShapeCache::kPromotedHits; ++i) {
        const cc::AutoSplitPlan plan = cc::auto_split_plan_cached(heavy, profile, state, 0xAA55);
        require(plan.shard_count == heavy_uncached.shard_count,
                "cached planner must match uncached factor during promotion");
    }

    const cc::AutoSplitPlan promoted = cc::auto_split_plan_cached(heavy, profile, state, 0xAA55);
    require(promoted.shard_count == heavy_uncached.shard_count, "promoted cache must preserve heavy factor");

    const cc::AutoSplitPlan separated_shape = cc::auto_split_plan_cached(light, profile, state, 0xAA55);
    require(separated_shape.shard_count == light_uncached.shard_count,
            "cached planner must not reuse factor across request shapes");

    cc::AutoSplitRequest sequential = heavy;
    sequential.intent = cc::SchedulingIntent::Sequential;
    const cc::AutoSplitPlan separated_intent = cc::auto_split_plan_cached(sequential, profile, state, 0xAA55);
    require(separated_intent.shard_count == 1, "cached planner must not reuse factor across intents");

    const std::uint64_t heavy_key_a = cc::auto_split_shape_key(heavy, profile, 0xAA55);
    const std::uint64_t heavy_key_b = cc::auto_split_shape_key(heavy, profile, 0x55AA);
    require(heavy_key_a != heavy_key_b, "shape key must include body key");
}

static void test_online_calibrator_updates_ewma() {
    cc::AutoSplitOnlineCalibrator cal;
    require(cal.dispatch_cost_ns() == 10000, "calibrator default dispatch cost changed");
    require(cal.per_item_ns() == 0, "calibrator default per-item estimate must be zero");
    require(cal.sample_count() == 0, "calibrator must start with zero samples");

    cal.record_dispatch(26000);
    require(cal.dispatch_cost_ns() == 11000, "dispatch EWMA must use alpha=1/16");
    require(cal.sample_count() == 1, "dispatch sample must increment sample counter");

    cal.record_shard(1000, 10);
    require(cal.per_item_ns() == 100, "per-item EWMA must initialize from first shard sample");
    require(cal.sample_count() == 2, "shard sample must increment sample counter");

    cal.record_shard(2600, 10);
    require(cal.per_item_ns() == 110, "per-item EWMA must mix subsequent shard sample");

    cal.record_dispatch(static_cast<std::uint64_t>(-1));
    require(cal.dispatch_cost_ns() > 1000000000ULL, "dispatch EWMA must saturate instead of wrapping on huge samples");
}

// A moving average built from a separate load, a computation and a store
// loses any sample that lands between one thread's load and its matching
// store. Every thread here reports the same value, so a lost sample leaves
// the average measurably off that value rather than merely noisy. The single
// compare-exchange the calibrator uses instead is race-free by construction,
// which also matters when this runs under the thread sanitizer.
//
// The sample counter is asserted separately. It advances by an atomic
// increment on every call whatever the averaging does, so a miscount points
// somewhere else entirely.
static void test_online_calibrator_concurrent_no_sample_loss() {
    cc::AutoSplitOnlineCalibrator cal;

    constexpr std::size_t thread_count = 8;
    constexpr std::size_t samples_per_thr = 4000;
    constexpr std::uint64_t target_ns = 16000;

    std::vector<std::jthread> threads;
    threads.reserve(thread_count);
    std::atomic<bool> go{false};
    for (std::size_t t = 0; t < thread_count; ++t) {
        threads.emplace_back([&] {
            while (!go.load(std::memory_order_acquire)) {
                CRUCIBLE_SPIN_PAUSE;
            }
            for (std::size_t i = 0; i < samples_per_thr; ++i) {
                cal.record_dispatch(target_ns);
            }
        });
    }
    go.store(true, std::memory_order_release);
    threads.clear();  // jthread dtors join all workers

    require(cal.sample_count() == thread_count * samples_per_thr,
            "concurrent record_dispatch must not lose sample-counter "
            "increments");

    // The average converges on its input within a few half-lives, so after
    // this many identical samples it should sit within a nanosecond or two of
    // the target, off only by integer-division rounding. A band of 64 ns is
    // wide enough to absorb that rounding and still far narrower than the
    // hundreds of nanoseconds of drift that losing samples produces.
    const std::uint64_t observed_ewma = cal.dispatch_cost_ns();
    const std::uint64_t lower = target_ns - 64;
    const std::uint64_t upper = target_ns + 64;
    if (observed_ewma < lower || observed_ewma > upper) {
        std::fprintf(stderr,
                     "concurrent EWMA drifted out of band: target=%lu "
                     "observed=%lu band=[%lu, %lu] — a sample was dropped "
                     "under contention\n",
                     target_ns, observed_ewma, lower, upper);
        std::exit(1);
    }
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

    const auto result = cc::dispatch_auto_split(pool,
                                                cc::AutoSplitRequest{
                                                    .item_count = 1000000,
                                                    .bytes_per_item = 64,
                                                    .max_shards = 16,
                                                    .intent = cc::SchedulingIntent::Background,
                                                },
                                                synthetic_profile(16), [&visited](cc::AutoSplitShard shard) {
                                                    visited.fetch_add(shard.size(), std::memory_order_relaxed);
                                                });

    require(result.plan.shard_count == 1, "Background intent must demote to inline when pool is busy");
    require(result.dispatch.ran_inline, "Background demotion should run inline instead of queueing");
    require(visited.load(std::memory_order_relaxed) == 1000000, "Background demotion must still cover the whole range");

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
    test_runtime_profile_refresh_and_reprobe();
    test_cached_planner_matches_uncached_and_separates_shapes();
    test_online_calibrator_updates_ewma();
    test_online_calibrator_concurrent_no_sample_loss();
    test_background_intent_demotes_under_pool_pressure();

    std::puts("test_auto_split: PASS");
    return 0;
}
