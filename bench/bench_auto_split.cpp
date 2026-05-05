// AutoSplit bench — range-planning and scheduler-dispatch overhead.

#include <crucible/concurrent/AutoSplit.h>

#include "bench_harness.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace cc = crucible::concurrent;
namespace cs = crucible::concurrent::scheduler;

namespace {

constexpr std::size_t KiB = 1024;
constexpr std::size_t MiB = 1024 * KiB;

struct TraceRingTag {};
struct MetaLogTag {};
struct GuardEvalTag {};
struct KernelCacheTag {};
struct ActivationGridTag {};
struct CompileDequeTag {};
struct OptimizerInboxTag {};
struct CipherSnapshotTag {};
struct DispatchControlTag {};
struct TensorMetaTag {};
struct ReplayEventTag {};
struct PmuSampleTag {};
struct GradientBucketTag {};
struct AttentionTileTag {};
struct CollectiveBenchTag {};
struct RecipeSnapshotTag {};

template <std::size_t N>
struct Payload {
    std::byte bytes[N]{};
};

using Payload32 = Payload<32>;
using Payload64 = Payload<64>;
using Payload96 = Payload<96>;
using Payload128 = Payload<128>;
using Payload144 = Payload<144>;
using Payload256 = Payload<256>;

using DispatchControlSubstrate =
    cc::PermissionedSpscChannel<std::uint64_t, 256, DispatchControlTag>;
using TraceRingSubstrate =
    cc::PermissionedSpscChannel<Payload64, 4096, TraceRingTag>;
using MetaLogSubstrate =
    cc::PermissionedMpscChannel<Payload128, 8192, MetaLogTag>;
using GuardEvalSubstrate =
    cc::PermissionedMpscChannel<Payload256, 1024, GuardEvalTag>;
using TensorMetaSubstrate =
    cc::PermissionedMpscChannel<Payload144, 16384, TensorMetaTag>;
using KernelCacheSubstrate =
    cc::PermissionedMpmcChannel<Payload32, 65536, KernelCacheTag>;
using ReplayEventSubstrate =
    cc::PermissionedMpmcChannel<Payload96, 32768, ReplayEventTag>;
using CompileDequeSubstrate =
    cc::PermissionedChaseLevDeque<std::uint64_t, 4096, CompileDequeTag>;
using PmuSampleSubstrate =
    cc::PermissionedSpscChannel<Payload32, 1 << 20, PmuSampleTag>;
using GradientBucketSubstrate =
    cc::PermissionedMpscChannel<Payload128, 262144, GradientBucketTag>;
using AttentionTileSubstrate =
    cc::PermissionedMpmcChannel<Payload256, 131072, AttentionTileTag>;
using CollectiveBenchSubstrate =
    cc::PermissionedMpmcChannel<Payload64, 262144, CollectiveBenchTag>;
using OptimizerInboxSubstrate =
    cc::PermissionedMpscChannel<Payload64, 1 << 20, OptimizerInboxTag>;
using CipherSnapshotSubstrate =
    cc::PermissionedSnapshot<Payload128, CipherSnapshotTag>;
using RecipeSnapshotSubstrate =
    cc::PermissionedSnapshot<Payload256, RecipeSnapshotTag>;

static_assert(cc::channel_byte_footprint_v<TraceRingSubstrate> == 256 * KiB);
static_assert(cc::per_call_working_set_v<TraceRingSubstrate> == 192);
static_assert(cc::channel_byte_footprint_v<MetaLogSubstrate> == 1 * MiB);
static_assert(cc::per_call_working_set_v<MetaLogSubstrate> == 320);
static_assert(cc::per_call_working_set_v<DispatchControlSubstrate> == 192);
static_assert(cc::channel_byte_footprint_v<PmuSampleSubstrate> == 32 * MiB);

struct Scenario {
    const char* name = "";
    const char* substrate = "";
    cc::ChannelTopology topology = cc::ChannelTopology::OneToOne;
    std::size_t producers = 1;
    std::size_t consumers = 1;
    std::size_t capacity = 0;
    std::size_t channel_bytes = 0;
    std::size_t per_call_bytes = 0;
    cc::AutoSplitRequest request{};
};

template <typename S>
[[nodiscard]] constexpr Scenario substrate_scenario(
    const char* name,
    const char* substrate,
    std::size_t item_count,
    std::size_t max_shards,
    std::size_t producers,
    std::size_t consumers) noexcept {
    using value_type = cc::substrate_value_type_t<S>;
    return Scenario{
        .name = name,
        .substrate = substrate,
        .topology = cc::substrate_topology_v<S>,
        .producers = producers,
        .consumers = consumers,
        .capacity = cc::substrate_capacity_v<S>,
        .channel_bytes = cc::channel_byte_footprint_v<S>,
        .per_call_bytes = cc::per_call_working_set_v<S>,
        .request = cc::AutoSplitRequest{
            .item_count = item_count,
            .bytes_per_item = sizeof(value_type),
            .max_shards = max_shards,
            .producers = producers,
            .consumers = consumers,
        },
    };
}

[[nodiscard]] constexpr Scenario sharded_grid_scenario() noexcept {
    constexpr std::size_t producers = 8;
    constexpr std::size_t consumers = 8;
    constexpr std::size_t capacity = 2048;
    return Scenario{
        .name = "activation_grid.map_tiles",
        .substrate = "PermissionedShardedGrid<Payload64,8,8,2048>",
        .topology = cc::ChannelTopology::ManyToMany,
        .producers = producers,
        .consumers = consumers,
        .capacity = capacity,
        .channel_bytes = producers * consumers * capacity * sizeof(Payload64),
        .per_call_bytes = 192,
        .request = cc::AutoSplitRequest{
            .item_count = 256 * 1024,
            .bytes_per_item = sizeof(Payload64),
            .max_shards = 16,
            .producers = producers,
            .consumers = consumers,
        },
    };
}

[[nodiscard]] constexpr std::array<Scenario, 16> scenarios() noexcept {
    return {{
        substrate_scenario<DispatchControlSubstrate>(
            "dispatch_control.tick",
            "PermissionedSpscChannel<uint64_t,256>",
            256,
            16,
            1,
            1),
        substrate_scenario<TraceRingSubstrate>(
            "trace_ring.drain_batch",
            "PermissionedSpscChannel<Payload64,4096>",
            2048,
            16,
            1,
            1),
        substrate_scenario<GuardEvalSubstrate>(
            "guard_eval.mpsc_pair",
            "PermissionedMpscChannel<Payload256,1024>",
            2048,
            2,
            2,
            1),
        substrate_scenario<MetaLogSubstrate>(
            "metalog.hash_fold",
            "PermissionedMpscChannel<Payload128,8192>",
            8192,
            8,
            4,
            1),
        substrate_scenario<TensorMetaSubstrate>(
            "tensor_meta.plan_scan",
            "PermissionedMpscChannel<Payload144,16384>",
            16384,
            8,
            4,
            1),
        substrate_scenario<KernelCacheSubstrate>(
            "kernel_cache.probe_batch",
            "PermissionedMpmcChannel<Payload32,65536>",
            65536,
            8,
            8,
            8),
        substrate_scenario<ReplayEventSubstrate>(
            "replay_event.rehash_window",
            "PermissionedMpmcChannel<Payload96,32768>",
            32768,
            8,
            4,
            4),
        sharded_grid_scenario(),
        substrate_scenario<CompileDequeSubstrate>(
            "compile_deque.cost_bucket",
            "PermissionedChaseLevDeque<uint64_t,4096>",
            131072,
            4,
            1,
            8),
        substrate_scenario<PmuSampleSubstrate>(
            "pmu_sample.compress_pages",
            "PermissionedSpscChannel<Payload32,1048576>",
            1 << 20,
            16,
            1,
            1),
        substrate_scenario<GradientBucketSubstrate>(
            "gradient_bucket.reduce",
            "PermissionedMpscChannel<Payload128,262144>",
            262144,
            16,
            16,
            1),
        substrate_scenario<AttentionTileSubstrate>(
            "attention_tile.softmax_pass",
            "PermissionedMpmcChannel<Payload256,131072>",
            131072,
            16,
            8,
            8),
        substrate_scenario<CollectiveBenchSubstrate>(
            "collective_bench.score",
            "PermissionedMpmcChannel<Payload64,262144>",
            262144,
            16,
            8,
            8),
        substrate_scenario<OptimizerInboxSubstrate>(
            "optimizer_inbox.reduce_state",
            "PermissionedMpscChannel<Payload64,1048576>",
            1 << 20,
            16,
            16,
            1),
        substrate_scenario<CipherSnapshotSubstrate>(
            "cipher_snapshot.verify_latest",
            "PermissionedSnapshot<Payload128>",
            1,
            16,
            1,
            16),
        substrate_scenario<RecipeSnapshotSubstrate>(
            "recipe_snapshot.broadcast",
            "PermissionedSnapshot<Payload256>",
            1,
            16,
            1,
            32),
    }};
}

[[nodiscard]] constexpr cc::AutoSplitRuntimeProfile synthetic_profile(
    std::size_t workers) noexcept {
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

[[nodiscard]] const cc::AutoSplitRuntimeProfile& hardware_profile_once() noexcept {
    return cc::auto_split_runtime_profile_once();
}

[[nodiscard]] const char* topology_name(cc::ChannelTopology topology) noexcept {
    switch (topology) {
    case cc::ChannelTopology::OneToOne: return "spsc";
    case cc::ChannelTopology::ManyToOne: return "mpsc";
    case cc::ChannelTopology::OneToMany_Latest: return "snapshot";
    case cc::ChannelTopology::ManyToMany: return "mpmc";
    case cc::ChannelTopology::WorkStealing: return "work_steal";
    default: return "unknown";
    }
}

[[nodiscard]] const char* route_kind_name(cc::RouteKind kind) noexcept {
    switch (kind) {
    case cc::RouteKind::Spsc: return "spsc";
    case cc::RouteKind::Mpsc: return "mpsc";
    case cc::RouteKind::Snapshot: return "snapshot";
    case cc::RouteKind::Mpmc: return "mpmc";
    case cc::RouteKind::WorkStealing: return "work_steal";
    case cc::RouteKind::ShardedGrid: return "sharded_grid";
    default: return "unknown";
    }
}

[[nodiscard]] const char* tier_name(cc::Tier tier) noexcept {
    switch (tier) {
    case cc::Tier::L1Resident: return "L1";
    case cc::Tier::L2Resident: return "L2";
    case cc::Tier::L3Resident: return "L3";
    case cc::Tier::DRAMBound: return "DRAM";
    default: return "?";
    }
}

[[nodiscard]] std::uint64_t digest_shard(
    cc::AutoSplitShard shard) noexcept {
    return (static_cast<std::uint64_t>(shard.index) << 48)
         ^ (static_cast<std::uint64_t>(shard.count) << 40)
         ^ (static_cast<std::uint64_t>(shard.begin) << 20)
         ^ static_cast<std::uint64_t>(shard.end)
         ^ static_cast<std::uint64_t>(shard.byte_count);
}

[[nodiscard]] std::uint64_t digest_plan(
    cc::AutoSplitPlan plan) noexcept {
    std::uint64_t digest =
        (static_cast<std::uint64_t>(plan.shard_count) << 48)
      ^ (static_cast<std::uint64_t>(plan.decision.factor) << 40)
      ^ (static_cast<std::uint64_t>(plan.total_items) << 8)
      ^ static_cast<std::uint64_t>(plan.total_bytes & 0xFFULL);
    for (std::size_t i = 0; i < plan.shard_count; ++i) {
        digest ^= digest_shard(plan.shard(i)) + 0x9E3779B97F4A7C15ULL
                + (digest << 6) + (digest >> 2);
    }
    return digest;
}

[[nodiscard]] std::uint64_t scenario_digest(const Scenario& scenario,
                                            cc::AutoSplitPlan plan) noexcept {
    return digest_plan(plan)
         ^ (static_cast<std::uint64_t>(scenario.topology) << 56)
         ^ (static_cast<std::uint64_t>(scenario.producers) << 48)
         ^ (static_cast<std::uint64_t>(scenario.consumers) << 40)
         ^ (static_cast<std::uint64_t>(scenario.capacity) << 8)
         ^ static_cast<std::uint64_t>(scenario.channel_bytes & 0xFFULL);
}

[[nodiscard]] std::uint64_t scenario_matrix_digest(
    cc::AutoSplitRuntimeProfile profile) noexcept {
    std::uint64_t digest =
        static_cast<std::uint64_t>(profile.available_workers) << 32;
    for (const Scenario& scenario : scenarios()) {
        const cc::AutoSplitPlan plan = cc::auto_split_plan(
            scenario.request, profile);
        digest ^= scenario_digest(scenario, plan) + 0xD6E8FEB86659FD93ULL
                + (digest << 7) + (digest >> 3);
    }
    return digest;
}

[[nodiscard]] std::uint64_t manual_chunk_digest(std::size_t items,
                                                std::size_t bytes_per_item,
                                                std::size_t shards) noexcept {
    shards = std::max<std::size_t>(1, std::min(shards, items));
    const std::size_t base = items / shards;
    const std::size_t rem = items % shards;
    std::uint64_t digest = static_cast<std::uint64_t>(shards);
    for (std::size_t i = 0; i < shards; ++i) {
        const std::size_t size = base + (i < rem ? 1 : 0);
        const std::size_t begin = i * base + std::min(i, rem);
        const std::size_t end = begin + size;
        digest ^= (static_cast<std::uint64_t>(begin) << 21)
                ^ (static_cast<std::uint64_t>(end) << 3)
                ^ (size * bytes_per_item);
    }
    return digest;
}

template <typename Body>
[[nodiscard]] bench::Report run_split(std::string name,
                                      std::size_t samples,
                                      Body&& body) {
    bench::Run run{std::move(name)};
    if (const int core = bench::env_core(); core >= 0) {
        (void)run.core(core);
    }
    return run.samples(samples)
        .warmup(std::max<std::size_t>(10, samples / 10))
        .max_wall_ms(1'000)
        .measure(std::forward<Body>(body));
}

void print_scenario_table() {
    constexpr cc::AutoSplitRuntimeProfile profile = synthetic_profile(16);
    std::printf("=== auto_split_scenarios ===\n");
    std::printf("  profile: l2=%zu huge=%zu workers=%zu\n",
                profile.route.l2_per_core_bytes,
                profile.route.huge_bytes,
                profile.available_workers);
    for (const Scenario& scenario : scenarios()) {
        const cc::AutoSplitPlan plan = cc::auto_split_plan(
            scenario.request, profile);
        std::printf("  %-31s topo=%-10s route=%-12s shards=%2zu "
                    "tier=%-4s items=%8zu itemB=%4zu work=%9zu "
                    "chan=%9zu hot=%4zu cap=%7zu substrate=%s\n",
                    scenario.name,
                    topology_name(scenario.topology),
                    route_kind_name(plan.route.kind),
                    plan.shard_count,
                    tier_name(plan.decision.tier),
                    plan.total_items,
                    plan.bytes_per_item,
                    plan.total_bytes,
                    scenario.channel_bytes,
                    scenario.per_call_bytes,
                    scenario.capacity,
                    scenario.substrate);
    }
    std::putchar('\n');
}

void print_profile_sweep_table() {
    constexpr cc::AutoSplitRuntimeProfile w2 = synthetic_profile(2);
    constexpr cc::AutoSplitRuntimeProfile w4 = synthetic_profile(4);
    constexpr cc::AutoSplitRuntimeProfile w8 = synthetic_profile(8);
    constexpr cc::AutoSplitRuntimeProfile w16 = synthetic_profile(16);
    const cc::AutoSplitRuntimeProfile& hw = hardware_profile_once();

    std::printf("=== auto_split_profile_sweep ===\n");
    std::printf("  %-31s %4s %4s %4s %4s %4s %8s %8s\n",
                "scenario", "w2", "w4", "w8", "w16", "hw", "workB", "hotB");
    for (const Scenario& scenario : scenarios()) {
        const cc::AutoSplitPlan p2 = cc::auto_split_plan(scenario.request, w2);
        const cc::AutoSplitPlan p4 = cc::auto_split_plan(scenario.request, w4);
        const cc::AutoSplitPlan p8 = cc::auto_split_plan(scenario.request, w8);
        const cc::AutoSplitPlan p16 = cc::auto_split_plan(scenario.request, w16);
        const cc::AutoSplitPlan phw = cc::auto_split_plan(scenario.request, hw);
        std::printf("  %-31s %4zu %4zu %4zu %4zu %4zu %8zu %8zu\n",
                    scenario.name,
                    p2.shard_count,
                    p4.shard_count,
                    p8.shard_count,
                    p16.shard_count,
                    phw.shard_count,
                    p16.total_bytes,
                    scenario.per_call_bytes);
    }
    std::putchar('\n');
}

void add_scenario_reports(std::vector<bench::Report>& reports) {
    constexpr cc::AutoSplitRuntimeProfile profile = synthetic_profile(16);
    for (const Scenario& scenario : scenarios()) {
        std::string name = "autosplit.scenario.";
        name += scenario.name;
        reports.push_back(run_split(std::move(name),
                                    20'000,
                                    [scenario] {
            const cc::AutoSplitPlan plan = cc::auto_split_plan(
                scenario.request, profile);
            const std::uint64_t digest = scenario_digest(scenario, plan);
            bench::do_not_optimize(digest);
        }));
    }
}

void add_matrix_reports(std::vector<bench::Report>& reports) {
    constexpr cc::AutoSplitRuntimeProfile w2 = synthetic_profile(2);
    constexpr cc::AutoSplitRuntimeProfile w4 = synthetic_profile(4);
    constexpr cc::AutoSplitRuntimeProfile w8 = synthetic_profile(8);
    constexpr cc::AutoSplitRuntimeProfile w16 = synthetic_profile(16);

    reports.push_back(run_split("autosplit.matrix.synthetic.w2.all_scenarios",
                                20'000,
                                [] {
        const std::uint64_t digest = scenario_matrix_digest(w2);
        bench::do_not_optimize(digest);
    }));
    reports.push_back(run_split("autosplit.matrix.synthetic.w4.all_scenarios",
                                20'000,
                                [] {
        const std::uint64_t digest = scenario_matrix_digest(w4);
        bench::do_not_optimize(digest);
    }));
    reports.push_back(run_split("autosplit.matrix.synthetic.w8.all_scenarios",
                                20'000,
                                [] {
        const std::uint64_t digest = scenario_matrix_digest(w8);
        bench::do_not_optimize(digest);
    }));
    reports.push_back(run_split("autosplit.matrix.synthetic.w16.all_scenarios",
                                20'000,
                                [] {
        const std::uint64_t digest = scenario_matrix_digest(w16);
        bench::do_not_optimize(digest);
    }));
    reports.push_back(run_split("autosplit.matrix.hardware.all_scenarios",
                                20'000,
                                [] {
        const std::uint64_t digest =
            scenario_matrix_digest(hardware_profile_once());
        bench::do_not_optimize(digest);
    }));
}

void add_plan_reports(std::vector<bench::Report>& reports) {
    constexpr cc::AutoSplitRuntimeProfile synth8 = synthetic_profile(8);
    constexpr cc::AutoSplitRuntimeProfile synth16 = synthetic_profile(16);

    reports.push_back(run_split("manual.chunk_digest.4shards.4mib",
                                20'000,
                                [] {
        const std::uint64_t digest =
            manual_chunk_digest(4096, 1024, 4);
        bench::do_not_optimize(digest);
    }));

    reports.push_back(run_split("autosplit.plan.synthetic.l2_inline",
                                20'000,
                                [] {
        const cc::AutoSplitPlan plan = cc::auto_split_plan(
            cc::AutoSplitRequest{.item_count = 64, .bytes_per_item = 512},
            synth8);
        const std::uint64_t digest = digest_plan(plan);
        bench::do_not_optimize(digest);
    }));

    reports.push_back(run_split("autosplit.plan.synthetic.medium_4shards",
                                20'000,
                                [] {
        const cc::AutoSplitPlan plan = cc::auto_split_plan(
            cc::AutoSplitRequest{
                .item_count = 4099,
                .bytes_per_item = 1024,
                .max_shards = 8,
            },
            synth8);
        const std::uint64_t digest = digest_plan(plan);
        bench::do_not_optimize(digest);
    }));

    reports.push_back(run_split("autosplit.plan.synthetic.huge_16shards",
                                20'000,
                                [] {
        const cc::AutoSplitPlan plan = cc::auto_split_plan(
            cc::AutoSplitRequest{
                .item_count = 1'000'003,
                .bytes_per_item = 64,
                .max_shards = 16,
            },
            synth16);
        const std::uint64_t digest = digest_plan(plan);
        bench::do_not_optimize(digest);
    }));

    reports.push_back(run_split("autosplit.plan.hardware_once.dynamic",
                                20'000,
                                [] {
        const cc::AutoSplitPlan plan = cc::auto_split_plan(
            cc::AutoSplitRequest{
                .item_count = 1'000'003,
                .bytes_per_item = 64,
                .max_shards = 16,
            },
            hardware_profile_once());
        const std::uint64_t digest = digest_plan(plan);
        bench::do_not_optimize(digest);
    }));
}

void add_dispatch_reports(std::vector<bench::Report>& reports) {
    cc::Pool<cs::Fifo> pool{cc::CoreCount{4}};
    const cc::AutoSplitRuntimeProfile profile = synthetic_profile(16);
    std::atomic<std::uint64_t> visited{0};

    reports.push_back(run_split("autosplit.dispatch.fifo.8shards.strided",
                                500,
                                [&] {
        const auto result = cc::dispatch_auto_split(
            pool,
            cc::AutoSplitRequest{
                .item_count = 4099,
                .bytes_per_item = 1024,
                .max_shards = 8,
            },
            profile,
            [&visited](cc::AutoSplitShard shard) {
                visited.fetch_add(shard.size(), std::memory_order_relaxed);
            });
        pool.wait_idle();
        bench::do_not_optimize(result.plan.shard_count);
        bench::do_not_optimize(visited);
    }));

    if (pool.failed() != 0) {
        std::fprintf(stderr, "bench_auto_split: pool failures=%llu\n",
                     static_cast<unsigned long long>(pool.failed()));
        std::abort();
    }

    const auto cases = scenarios();
    constexpr std::array<std::size_t, 6> selected{0, 2, 8, 5, 10, 12};
    for (std::size_t scenario_index : selected) {
        const Scenario scenario = cases[scenario_index];
        std::string name = "autosplit.dispatch.scenario.";
        name += scenario.name;
        reports.push_back(run_split(std::move(name),
                                    300,
                                    [&, scenario] {
            const auto result = cc::dispatch_auto_split(
                pool,
                scenario.request,
                profile,
                [&visited](cc::AutoSplitShard shard) {
                    visited.fetch_add(shard.size(),
                                      std::memory_order_relaxed);
                });
            pool.wait_idle();
            bench::do_not_optimize(result.plan.shard_count);
            bench::do_not_optimize(result.dispatch.tasks_submitted);
            bench::do_not_optimize(visited);
        }));
    }

    if (pool.failed() != 0) {
        std::fprintf(stderr, "bench_auto_split: scenario pool failures=%llu\n",
                     static_cast<unsigned long long>(pool.failed()));
        std::abort();
    }
}

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const cc::AutoSplitRuntimeProfile& hw = hardware_profile_once();
    std::printf("=== auto_split ===\n");
    std::printf("  l2=%zu l3=%zu workers=%zu medium_shards=%zu huge_shards=%zu\n\n",
                hw.route.l2_per_core_bytes,
                hw.route.huge_bytes,
                hw.available_workers,
                hw.route.medium_shards,
                hw.route.huge_shards);

    std::vector<bench::Report> reports;
    reports.reserve(32);
    add_plan_reports(reports);
    print_scenario_table();
    print_profile_sweep_table();
    add_scenario_reports(reports);
    add_matrix_reports(reports);
    add_dispatch_reports(reports);

    std::printf("  report_count=%zu\n\n", reports.size());
    bench::emit_reports(reports, bench::env_json());
    return 0;
}
