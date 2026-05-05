// AutoRouter bench — BPF-harness regression suite for type-level routing.
//
// The router must stay compile-time-only.  These benches intentionally
// consume already-constant decisions, type aliases, and one startup hardware
// snapshot; if a future edit replaces consteval policy with runtime dispatch,
// this target should become noisy immediately.

#include <crucible/concurrent/AutoRouter.h>
#include <crucible/concurrent/Topology.h>
#include <crucible/safety/Simd.h>

#include "bench_harness.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace cc = crucible::concurrent;
namespace csimd = crucible::simd;

namespace {

struct StreamTag {};
struct StreamTagB {};
struct LatestTag {};
struct ShardTag {};
struct ShardTagB {};
struct WorkTag {};
struct WorkTagB {};

template <std::size_t N>
struct Payload {
    std::byte bytes[N]{};
};

using Payload8   = Payload<8>;
using Payload32  = Payload<32>;
using Payload64  = Payload<64>;
using Payload128 = Payload<128>;
using Payload256 = Payload<256>;
using Payload512 = Payload<512>;
using Payload4K  = Payload<4096>;

constexpr std::size_t KiB = 1024;
constexpr std::size_t MiB = 1024 * KiB;
constexpr std::size_t Tiny = 4 * KiB;
constexpr std::size_t Small = 64 * KiB;
constexpr std::size_t CliffMinus = cc::conservative_cliff_l2_per_core - 64;
constexpr std::size_t Cliff = cc::conservative_cliff_l2_per_core;
constexpr std::size_t CliffPlus = cc::conservative_cliff_l2_per_core + 64;
constexpr std::size_t Large = 4 * MiB;
constexpr std::size_t Huge = 64 * MiB;

struct HardwareSnapshot {
    std::size_t l1d = 0;
    std::size_t l2 = 0;
    std::size_t l3 = 0;
    std::size_t line = 0;
    std::size_t physical_cores = 0;
    std::size_t smt_threads = 0;
    std::size_t process_cpus = 0;
    std::size_t numa_nodes = 0;
    std::size_t page_size = 0;
    bool hugepage_2mb = false;
    bool avx512 = false;
    bool avx2 = false;
    bool sse42 = false;
    cc::Topology::Source source = cc::Topology::Source::Fallback;
};

struct RuntimeCase {
    cc::RouteIntent intent;
    std::size_t producers;
    std::size_t consumers;
    std::size_t bytes;
    std::size_t max_shards;
};

[[nodiscard]] const HardwareSnapshot& hardware_once() noexcept {
    static const HardwareSnapshot snapshot = [] {
        const cc::Topology& topology = cc::Topology::instance();
        return HardwareSnapshot{
            .l1d = topology.l1d_per_core_bytes(),
            .l2 = topology.l2_per_core_bytes(),
            .l3 = topology.l3_total_bytes(),
            .line = topology.cache_line_bytes(),
            .physical_cores = topology.num_cores(),
            .smt_threads = topology.num_smt_threads(),
            .process_cpus = topology.process_cpu_count(),
            .numa_nodes = topology.numa_nodes(),
            .page_size = topology.page_size_bytes(),
            .hugepage_2mb = topology.hugepage_2mb_available(),
            .avx512 = csimd::runtime_supports_avx512(),
            .avx2 = csimd::runtime_supports_avx2(),
            .sse42 = csimd::runtime_supports_sse42(),
            .source = topology.source(),
        };
    }();
    return snapshot;
}

[[nodiscard]] const char* source_name(cc::Topology::Source source) noexcept {
    switch (source) {
    case cc::Topology::Source::Sysfs: return "sysfs";
    case cc::Topology::Source::Fallback: return "fallback";
    default: return "unknown";
    }
}

template <typename Body>
[[nodiscard]] bench::Report run_router(std::string name, Body&& body) {
    bench::Run run{std::move(name)};
    if (const int core = bench::env_core(); core >= 0) {
        (void)run.core(core);
    }
    return run.samples(20'000)
        .warmup(2'000)
        .max_wall_ms(1'000)
        .measure(std::forward<Body>(body));
}

[[nodiscard]] constexpr std::uint64_t
route_digest_from_decision(cc::AutoRouteDecision decision) noexcept {
    return (static_cast<std::uint64_t>(decision.kind) << 56)
         ^ (static_cast<std::uint64_t>(decision.intent) << 48)
         ^ (static_cast<std::uint64_t>(decision.topology) << 40)
         ^ (static_cast<std::uint64_t>(decision.producers) << 32)
         ^ (static_cast<std::uint64_t>(decision.consumers) << 24)
         ^ (static_cast<std::uint64_t>(decision.shard_factor) << 16)
         ^ (decision.sharded ? 0xA55AULL : 0ULL)
         ^ (decision.latest_only ? 0x5AA5ULL : 0ULL)
         ^ static_cast<std::uint64_t>(decision.workload_bytes & 0xFFFFULL);
}

template <cc::RouteIntent Intent,
          std::size_t Producers,
          std::size_t Consumers,
          std::size_t WorkloadBytes,
          std::size_t MaxShards = 16>
[[nodiscard]] consteval std::uint64_t route_digest() noexcept {
    constexpr cc::AutoRouteDecision decision =
        cc::auto_route_v<Intent, Producers, Consumers, WorkloadBytes, MaxShards>;
    return route_digest_from_decision(decision);
}

template <cc::RouteIntent Intent,
          typename T,
          std::size_t Capacity,
          typename UserTag,
          std::size_t Producers,
          std::size_t Consumers,
          std::size_t WorkloadBytes,
          std::size_t MaxShards = 16>
[[nodiscard]] consteval std::uint64_t static_route_digest() noexcept {
    constexpr cc::AutoRouteDecision decision =
        cc::static_auto_route_v<Intent,
                                T,
                                Capacity,
                                UserTag,
                                Producers,
                                Consumers,
                                WorkloadBytes,
                                MaxShards>;
    return route_digest_from_decision(decision);
}

template <cc::RouteIntent Intent,
          std::size_t Producers,
          std::size_t Consumers,
          std::size_t WorkloadBytes,
          std::size_t MaxShards = 16>
void consume_decision_fields() noexcept {
    constexpr cc::AutoRouteDecision decision =
        cc::auto_route_v<Intent, Producers, Consumers, WorkloadBytes, MaxShards>;

    auto kind = static_cast<std::uint8_t>(decision.kind);
    auto intent = static_cast<std::uint8_t>(decision.intent);
    auto topology = static_cast<std::uint8_t>(decision.topology);
    auto producers = decision.producers;
    auto consumers = decision.consumers;
    auto bytes = decision.workload_bytes;
    auto shards = decision.shard_factor;
    auto flags = static_cast<std::uint8_t>(
        (decision.sharded ? 0x1 : 0x0) | (decision.latest_only ? 0x2 : 0x0));

    bench::do_not_optimize(kind);
    bench::do_not_optimize(intent);
    bench::do_not_optimize(topology);
    bench::do_not_optimize(producers);
    bench::do_not_optimize(consumers);
    bench::do_not_optimize(bytes);
    bench::do_not_optimize(shards);
    bench::do_not_optimize(flags);
}

template <cc::RouteIntent Intent,
          std::size_t Producers,
          std::size_t Consumers,
          std::size_t WorkloadBytes,
          std::size_t MaxShards = 16>
void consume_route_digest() noexcept {
    constexpr std::uint64_t digest =
        route_digest<Intent, Producers, Consumers, WorkloadBytes, MaxShards>();
    bench::do_not_optimize(digest);
}

template <cc::RouteIntent Intent,
          typename T,
          std::size_t Capacity,
          typename UserTag,
          std::size_t Producers,
          std::size_t Consumers,
          std::size_t WorkloadBytes,
          std::size_t MaxShards = 16>
void consume_static_route_digest() noexcept {
    constexpr std::uint64_t digest =
        static_route_digest<Intent,
                            T,
                            Capacity,
                            UserTag,
                            Producers,
                            Consumers,
                            WorkloadBytes,
                            MaxShards>();
    bench::do_not_optimize(digest);
}

void consume_decision_value(cc::AutoRouteDecision decision) noexcept {
    const std::uint64_t digest = route_digest_from_decision(decision);
    bench::do_not_optimize(digest);
}

template <typename Route>
void consume_route_type() noexcept {
    constexpr std::size_t object_bytes = sizeof(Route);
    constexpr std::size_t value_bytes = sizeof(typename Route::value_type);
    constexpr std::size_t tag_bytes = sizeof(typename Route::user_tag);

    bench::do_not_optimize(object_bytes);
    bench::do_not_optimize(value_bytes);
    bench::do_not_optimize(tag_bytes);
}

void consume_hardware_cache_snapshot(const HardwareSnapshot& hw) noexcept {
    const std::size_t cache_fold = hw.l1d ^ hw.l2 ^ hw.l3 ^ hw.line;
    const std::size_t core_fold =
        hw.physical_cores ^ hw.smt_threads ^ hw.process_cpus ^ hw.numa_nodes;
    bench::do_not_optimize(cache_fold);
    bench::do_not_optimize(core_fold);
}

void consume_hardware_feature_snapshot(const HardwareSnapshot& hw) noexcept {
    const std::uint8_t flags =
        static_cast<std::uint8_t>((hw.hugepage_2mb ? 0x01 : 0x00)
                                | (hw.avx512 ? 0x02 : 0x00)
                                | (hw.avx2 ? 0x04 : 0x00)
                                | (hw.sse42 ? 0x08 : 0x00));
    bench::do_not_optimize(flags);
    bench::do_not_optimize(hw.page_size);
}

template <cc::RouteIntent Intent,
          std::size_t Producers,
          std::size_t Consumers,
          std::size_t WorkloadBytes,
          std::size_t MaxShards = 16>
void add_decision(std::vector<bench::Report>& reports, std::string_view name) {
    reports.push_back(run_router(std::string{name}, [] {
        consume_decision_fields<Intent, Producers, Consumers, WorkloadBytes, MaxShards>();
    }));
}

template <cc::RouteIntent Intent,
          std::size_t Producers,
          std::size_t Consumers,
          std::size_t WorkloadBytes,
          std::size_t MaxShards = 16>
void add_digest(std::vector<bench::Report>& reports, std::string_view name) {
    reports.push_back(run_router(std::string{name}, [] {
        consume_route_digest<Intent, Producers, Consumers, WorkloadBytes, MaxShards>();
    }));
}

template <typename Route>
void add_type(std::vector<bench::Report>& reports, std::string_view name) {
    reports.push_back(run_router(std::string{name}, [] {
        consume_route_type<Route>();
    }));
}

template <cc::RouteIntent Intent,
          typename T,
          std::size_t Capacity,
          typename UserTag,
          std::size_t Producers,
          std::size_t Consumers,
          std::size_t WorkloadBytes,
          std::size_t MaxShards = 16>
void add_static_digest(std::vector<bench::Report>& reports,
                       std::string_view name) {
    reports.push_back(run_router(std::string{name}, [] {
        consume_static_route_digest<Intent,
                                    T,
                                    Capacity,
                                    UserTag,
                                    Producers,
                                    Consumers,
                                    WorkloadBytes,
                                    MaxShards>();
    }));
}

void add_runtime_digest(std::vector<bench::Report>& reports,
                        std::string_view name,
                        RuntimeCase route,
                        cc::AutoRouteRuntimeProfile profile) {
    reports.push_back(run_router(std::string{name}, [route, profile] {
        const cc::AutoRouteDecision decision =
            cc::auto_route_decision_runtime(route.intent,
                                            route.producers,
                                            route.consumers,
                                            route.bytes,
                                            route.max_shards,
                                            profile);
        consume_decision_value(decision);
    }));
}

void add_hardware_reports(std::vector<bench::Report>& reports,
                          const HardwareSnapshot& hw) {
    reports.push_back(run_router("hardware.once.cache_snapshot", [&hw] {
        consume_hardware_cache_snapshot(hw);
    }));
    reports.push_back(run_router("hardware.once.feature_snapshot", [&hw] {
        consume_hardware_feature_snapshot(hw);
    }));
    reports.push_back(run_router("hardware.once.combined", [&hw] {
        consume_hardware_cache_snapshot(hw);
        consume_hardware_feature_snapshot(hw);
    }));
}

void add_stream_decisions(std::vector<bench::Report>& reports) {
    add_decision<cc::RouteIntent::Stream, 1, 1, Tiny>(reports, "decision.stream.1p1c.tiny");
    add_decision<cc::RouteIntent::Stream, 1, 1, Small>(reports, "decision.stream.1p1c.small");
    add_decision<cc::RouteIntent::Stream, 1, 1, CliffMinus>(reports, "decision.stream.1p1c.cliff_minus");
    add_decision<cc::RouteIntent::Stream, 1, 1, Cliff>(reports, "decision.stream.1p1c.cliff");
    add_decision<cc::RouteIntent::Stream, 1, 1, CliffPlus>(reports, "decision.stream.1p1c.cliff_plus");
    add_decision<cc::RouteIntent::Stream, 1, 1, Large>(reports, "decision.stream.1p1c.large");
    add_decision<cc::RouteIntent::Stream, 1, 1, Huge>(reports, "decision.stream.1p1c.huge");

    add_decision<cc::RouteIntent::Stream, 4, 1, Tiny>(reports, "decision.stream.4p1c.tiny");
    add_decision<cc::RouteIntent::Stream, 4, 1, Small>(reports, "decision.stream.4p1c.small");
    add_decision<cc::RouteIntent::Stream, 4, 1, CliffMinus>(reports, "decision.stream.4p1c.cliff_minus");
    add_decision<cc::RouteIntent::Stream, 4, 1, Cliff>(reports, "decision.stream.4p1c.cliff");
    add_decision<cc::RouteIntent::Stream, 4, 1, CliffPlus>(reports, "decision.stream.4p1c.cliff_plus");
    add_decision<cc::RouteIntent::Stream, 4, 1, Large>(reports, "decision.stream.4p1c.large");
    add_decision<cc::RouteIntent::Stream, 4, 1, Huge>(reports, "decision.stream.4p1c.huge");

    add_decision<cc::RouteIntent::Stream, 1, 4, Tiny>(reports, "decision.stream.1p4c.tiny");
    add_decision<cc::RouteIntent::Stream, 1, 4, Small>(reports, "decision.stream.1p4c.small");
    add_decision<cc::RouteIntent::Stream, 1, 4, CliffMinus>(reports, "decision.stream.1p4c.cliff_minus");
    add_decision<cc::RouteIntent::Stream, 1, 4, Cliff>(reports, "decision.stream.1p4c.cliff");
    add_decision<cc::RouteIntent::Stream, 1, 4, CliffPlus>(reports, "decision.stream.1p4c.cliff_plus");
    add_decision<cc::RouteIntent::Stream, 1, 4, Large>(reports, "decision.stream.1p4c.large");
    add_decision<cc::RouteIntent::Stream, 1, 4, Huge>(reports, "decision.stream.1p4c.huge");

    add_decision<cc::RouteIntent::Stream, 4, 4, Tiny>(reports, "decision.stream.4p4c.tiny");
    add_decision<cc::RouteIntent::Stream, 4, 4, Small>(reports, "decision.stream.4p4c.small");
    add_decision<cc::RouteIntent::Stream, 4, 4, CliffMinus>(reports, "decision.stream.4p4c.cliff_minus");
    add_decision<cc::RouteIntent::Stream, 4, 4, Cliff>(reports, "decision.stream.4p4c.cliff");
    add_decision<cc::RouteIntent::Stream, 4, 4, CliffPlus>(reports, "decision.stream.4p4c.cliff_plus");
    add_decision<cc::RouteIntent::Stream, 4, 4, Large>(reports, "decision.stream.4p4c.large");
    add_decision<cc::RouteIntent::Stream, 4, 4, Huge>(reports, "decision.stream.4p4c.huge");
}

void add_shardable_decisions(std::vector<bench::Report>& reports) {
    add_decision<cc::RouteIntent::Shardable, 1, 1, Tiny>(reports, "decision.shardable.1p1c.tiny");
    add_decision<cc::RouteIntent::Shardable, 1, 1, Small>(reports, "decision.shardable.1p1c.small");
    add_decision<cc::RouteIntent::Shardable, 1, 1, CliffMinus>(reports, "decision.shardable.1p1c.cliff_minus");
    add_decision<cc::RouteIntent::Shardable, 1, 1, Cliff>(reports, "decision.shardable.1p1c.cliff");
    add_decision<cc::RouteIntent::Shardable, 1, 1, CliffPlus>(reports, "decision.shardable.1p1c.cliff_plus");
    add_decision<cc::RouteIntent::Shardable, 1, 1, Large>(reports, "decision.shardable.1p1c.large");
    add_decision<cc::RouteIntent::Shardable, 1, 1, Huge>(reports, "decision.shardable.1p1c.huge");

    add_decision<cc::RouteIntent::Shardable, 4, 1, Tiny>(reports, "decision.shardable.4p1c.tiny");
    add_decision<cc::RouteIntent::Shardable, 4, 1, Cliff>(reports, "decision.shardable.4p1c.cliff");
    add_decision<cc::RouteIntent::Shardable, 4, 1, CliffPlus>(reports, "decision.shardable.4p1c.cliff_plus");
    add_decision<cc::RouteIntent::Shardable, 4, 1, Large>(reports, "decision.shardable.4p1c.large");
    add_decision<cc::RouteIntent::Shardable, 4, 1, Huge>(reports, "decision.shardable.4p1c.huge");

    add_decision<cc::RouteIntent::Shardable, 1, 4, Tiny>(reports, "decision.shardable.1p4c.tiny");
    add_decision<cc::RouteIntent::Shardable, 1, 4, Cliff>(reports, "decision.shardable.1p4c.cliff");
    add_decision<cc::RouteIntent::Shardable, 1, 4, CliffPlus>(reports, "decision.shardable.1p4c.cliff_plus");
    add_decision<cc::RouteIntent::Shardable, 1, 4, Large>(reports, "decision.shardable.1p4c.large");
    add_decision<cc::RouteIntent::Shardable, 1, 4, Huge>(reports, "decision.shardable.1p4c.huge");

    add_decision<cc::RouteIntent::Shardable, 4, 4, Tiny>(reports, "decision.shardable.4p4c.tiny");
    add_decision<cc::RouteIntent::Shardable, 4, 4, Cliff>(reports, "decision.shardable.4p4c.cliff");
    add_decision<cc::RouteIntent::Shardable, 4, 4, CliffPlus>(reports, "decision.shardable.4p4c.cliff_plus");
    add_decision<cc::RouteIntent::Shardable, 4, 4, Large>(reports, "decision.shardable.4p4c.large");
    add_decision<cc::RouteIntent::Shardable, 4, 4, Huge>(reports, "decision.shardable.4p4c.huge");
}

void add_latest_and_work_decisions(std::vector<bench::Report>& reports) {
    add_decision<cc::RouteIntent::Latest, 1, 1, Tiny>(reports, "decision.latest.1p1c.tiny");
    add_decision<cc::RouteIntent::Latest, 1, 4, Tiny>(reports, "decision.latest.1p4c.tiny");
    add_decision<cc::RouteIntent::Latest, 1, 32, Tiny>(reports, "decision.latest.1p32c.tiny");
    add_decision<cc::RouteIntent::Latest, 1, 4, Cliff>(reports, "decision.latest.1p4c.cliff");
    add_decision<cc::RouteIntent::Latest, 1, 4, CliffPlus>(reports, "decision.latest.1p4c.cliff_plus");
    add_decision<cc::RouteIntent::Latest, 1, 4, Large>(reports, "decision.latest.1p4c.large");
    add_decision<cc::RouteIntent::Latest, 1, 4, Huge>(reports, "decision.latest.1p4c.huge");
    add_decision<cc::RouteIntent::Latest, 4, 32, Huge>(reports, "decision.latest.4p32c.huge");

    add_decision<cc::RouteIntent::VariableCost, 1, 1, Tiny>(reports, "decision.work.1p1c.tiny");
    add_decision<cc::RouteIntent::VariableCost, 1, 4, Tiny>(reports, "decision.work.1p4c.tiny");
    add_decision<cc::RouteIntent::VariableCost, 1, 32, Tiny>(reports, "decision.work.1p32c.tiny");
    add_decision<cc::RouteIntent::VariableCost, 4, 4, Cliff>(reports, "decision.work.4p4c.cliff");
    add_decision<cc::RouteIntent::VariableCost, 4, 4, CliffPlus>(reports, "decision.work.4p4c.cliff_plus");
    add_decision<cc::RouteIntent::VariableCost, 4, 4, Large>(reports, "decision.work.4p4c.large");
    add_decision<cc::RouteIntent::VariableCost, 4, 32, Huge>(reports, "decision.work.4p32c.huge");
    add_decision<cc::RouteIntent::VariableCost, 32, 32, Huge>(reports, "decision.work.32p32c.huge");
}

void add_shard_caps(std::vector<bench::Report>& reports) {
    add_decision<cc::RouteIntent::Shardable, 1, 1, Large, 1>(reports, "decision.shard_cap.large.max1");
    add_decision<cc::RouteIntent::Shardable, 1, 1, Large, 2>(reports, "decision.shard_cap.large.max2");
    add_decision<cc::RouteIntent::Shardable, 1, 1, Large, 3>(reports, "decision.shard_cap.large.max3");
    add_decision<cc::RouteIntent::Shardable, 1, 1, Large, 4>(reports, "decision.shard_cap.large.max4");
    add_decision<cc::RouteIntent::Shardable, 1, 1, Large, 8>(reports, "decision.shard_cap.large.max8");
    add_decision<cc::RouteIntent::Shardable, 1, 1, Large, 16>(reports, "decision.shard_cap.large.max16");

    add_decision<cc::RouteIntent::Shardable, 1, 1, Huge, 1>(reports, "decision.shard_cap.huge.max1");
    add_decision<cc::RouteIntent::Shardable, 1, 1, Huge, 2>(reports, "decision.shard_cap.huge.max2");
    add_decision<cc::RouteIntent::Shardable, 1, 1, Huge, 3>(reports, "decision.shard_cap.huge.max3");
    add_decision<cc::RouteIntent::Shardable, 1, 1, Huge, 4>(reports, "decision.shard_cap.huge.max4");
    add_decision<cc::RouteIntent::Shardable, 1, 1, Huge, 8>(reports, "decision.shard_cap.huge.max8");
    add_decision<cc::RouteIntent::Shardable, 1, 1, Huge, 16>(reports, "decision.shard_cap.huge.max16");
    add_decision<cc::RouteIntent::Shardable, 1, 1, Huge, 32>(reports, "decision.shard_cap.huge.max32");
    add_decision<cc::RouteIntent::Shardable, 1, 1, Huge, 64>(reports, "decision.shard_cap.huge.max64");
}

void add_digest_cases(std::vector<bench::Report>& reports) {
    add_digest<cc::RouteIntent::Stream, 1, 1, Tiny>(reports, "digest.stream.1p1c.tiny");
    add_digest<cc::RouteIntent::Stream, 4, 1, Tiny>(reports, "digest.stream.4p1c.tiny");
    add_digest<cc::RouteIntent::Stream, 1, 4, Tiny>(reports, "digest.stream.1p4c.tiny");
    add_digest<cc::RouteIntent::Stream, 4, 4, Tiny>(reports, "digest.stream.4p4c.tiny");
    add_digest<cc::RouteIntent::Stream, 1, 1, Huge>(reports, "digest.stream.1p1c.huge");
    add_digest<cc::RouteIntent::Stream, 4, 4, Huge>(reports, "digest.stream.4p4c.huge");
    add_digest<cc::RouteIntent::Shardable, 1, 1, Cliff>(reports, "digest.shardable.1p1c.cliff");
    add_digest<cc::RouteIntent::Shardable, 1, 1, CliffPlus>(reports, "digest.shardable.1p1c.cliff_plus");
    add_digest<cc::RouteIntent::Shardable, 4, 4, Large>(reports, "digest.shardable.4p4c.large");
    add_digest<cc::RouteIntent::Shardable, 4, 4, Huge>(reports, "digest.shardable.4p4c.huge");
    add_digest<cc::RouteIntent::Latest, 1, 32, Huge>(reports, "digest.latest.1p32c.huge");
    add_digest<cc::RouteIntent::VariableCost, 32, 32, Huge>(reports, "digest.work.32p32c.huge");
}

void add_implementation_comparison(std::vector<bench::Report>& reports,
                                   const HardwareSnapshot& hw) {
    const cc::AutoRouteRuntimeProfile fixed_profile{};
    const cc::AutoRouteRuntimeProfile hardware_profile{
        .l2_per_core_bytes = hw.l2 != 0 ? hw.l2 : cc::conservative_cliff_l2_per_core,
        .huge_bytes = 16 * MiB,
        .medium_shards = 4,
        .huge_shards = 16,
    };

    add_digest<cc::RouteIntent::Shardable, 4, 4, Large>(
        reports, "impl.consteval.digest.shardable.4p4c.large");
    add_static_digest<cc::RouteIntent::Shardable,
                      int,
                      1024,
                      ShardTag,
                      4,
                      4,
                      Large>(
        reports, "impl.static_trait.digest.shardable.4p4c.large");
    add_runtime_digest(reports,
                       "impl.runtime_fixed.digest.shardable.4p4c.large",
                       RuntimeCase{cc::RouteIntent::Shardable, 4, 4, Large, 16},
                       fixed_profile);
    add_runtime_digest(reports,
                       "impl.runtime_hw.digest.shardable.4p4c.large",
                       RuntimeCase{cc::RouteIntent::Shardable, 4, 4, Large, 16},
                       hardware_profile);

    add_digest<cc::RouteIntent::Stream, 1, 1, Huge>(
        reports, "impl.consteval.digest.stream.1p1c.huge");
    add_static_digest<cc::RouteIntent::Stream,
                      int,
                      1024,
                      StreamTag,
                      1,
                      1,
                      Huge>(
        reports, "impl.static_trait.digest.stream.1p1c.huge");
    add_runtime_digest(reports,
                       "impl.runtime_fixed.digest.stream.1p1c.huge",
                       RuntimeCase{cc::RouteIntent::Stream, 1, 1, Huge, 16},
                       fixed_profile);
    add_runtime_digest(reports,
                       "impl.runtime_hw.digest.stream.1p1c.huge",
                       RuntimeCase{cc::RouteIntent::Stream, 1, 1, Huge, 16},
                       hardware_profile);

    reports.push_back(run_router("impl.runtime_fixed.dynamic_mix", [] {
        static constexpr std::array<RuntimeCase, 8> cases{{
            {cc::RouteIntent::Stream, 1, 1, Tiny, 16},
            {cc::RouteIntent::Stream, 4, 1, Small, 16},
            {cc::RouteIntent::Shardable, 1, 1, CliffPlus, 16},
            {cc::RouteIntent::Shardable, 4, 4, Large, 16},
            {cc::RouteIntent::Latest, 1, 32, Huge, 16},
            {cc::RouteIntent::VariableCost, 32, 32, Huge, 16},
            {cc::RouteIntent::Shardable, 1, 1, Huge, 8},
            {cc::RouteIntent::Stream, 1, 4, Large, 16},
        }};
        static std::size_t cursor = 0;
        const RuntimeCase route = cases[cursor++ & (cases.size() - 1)];
        consume_decision_value(
            cc::auto_route_decision_runtime(route.intent,
                                            route.producers,
                                            route.consumers,
                                            route.bytes,
                                            route.max_shards));
    }));

    reports.push_back(run_router("impl.runtime_hw.dynamic_mix", [hardware_profile] {
        static constexpr std::array<RuntimeCase, 8> cases{{
            {cc::RouteIntent::Stream, 1, 1, Tiny, 16},
            {cc::RouteIntent::Stream, 4, 1, Small, 16},
            {cc::RouteIntent::Shardable, 1, 1, CliffPlus, 16},
            {cc::RouteIntent::Shardable, 4, 4, Large, 16},
            {cc::RouteIntent::Latest, 1, 32, Huge, 16},
            {cc::RouteIntent::VariableCost, 32, 32, Huge, 16},
            {cc::RouteIntent::Shardable, 1, 1, Huge, 8},
            {cc::RouteIntent::Stream, 1, 4, Large, 16},
        }};
        static std::size_t cursor = 0;
        const RuntimeCase route = cases[cursor++ & (cases.size() - 1)];
        consume_decision_value(
            cc::auto_route_decision_runtime(route.intent,
                                            route.producers,
                                            route.consumers,
                                            route.bytes,
                                            route.max_shards,
                                            hardware_profile));
    }));
}

void add_type_cases(std::vector<bench::Report>& reports) {
    add_type<cc::AutoRoute_t<cc::RouteIntent::Stream, int, 64, StreamTag, 1, 1, Tiny>>(reports, "type.stream.int.cap64.1p1c.tiny");
    add_type<cc::AutoRoute_t<cc::RouteIntent::Stream, int, 1024, StreamTag, 4, 1, Tiny>>(reports, "type.stream.int.cap1024.4p1c.tiny");
    add_type<cc::AutoRoute_t<cc::RouteIntent::Stream, int, 1024, StreamTag, 1, 4, Tiny>>(reports, "type.stream.int.cap1024.1p4c.tiny");
    add_type<cc::AutoRoute_t<cc::RouteIntent::Stream, int, 1024, StreamTag, 4, 4, Huge>>(reports, "type.stream.int.cap1024.4p4c.huge");

    add_type<cc::AutoRoute_t<cc::RouteIntent::Stream, Payload8, 64, StreamTagB, 1, 1, Tiny>>(reports, "type.stream.payload8.spsc");
    add_type<cc::AutoRoute_t<cc::RouteIntent::Stream, Payload64, 64, StreamTagB, 4, 1, Tiny>>(reports, "type.stream.payload64.mpsc");
    add_type<cc::AutoRoute_t<cc::RouteIntent::Stream, Payload256, 64, StreamTagB, 4, 4, Tiny>>(reports, "type.stream.payload256.mpmc");
    add_type<cc::AutoRoute_t<cc::RouteIntent::Stream, Payload4K, 64, StreamTagB, 1, 1, Huge>>(reports, "type.stream.payload4k.spsc_huge");

    add_type<cc::AutoRoute_t<cc::RouteIntent::Latest, int, 1, LatestTag, 1, 8, Tiny>>(reports, "type.latest.int");
    add_type<cc::AutoRoute_t<cc::RouteIntent::Latest, Payload8, 1, LatestTag, 1, 8, Tiny>>(reports, "type.latest.payload8");
    add_type<cc::AutoRoute_t<cc::RouteIntent::Latest, Payload128, 1, LatestTag, 1, 8, Large>>(reports, "type.latest.payload128.large");
    add_type<cc::AutoRoute_t<cc::RouteIntent::Latest, Payload256, 1, LatestTag, 1, 32, Huge>>(reports, "type.latest.payload256.huge");

    add_type<cc::AutoRoute_t<cc::RouteIntent::VariableCost, int, 64, WorkTag, 1, 8, Tiny>>(reports, "type.work.int.cap64");
    add_type<cc::AutoRoute_t<cc::RouteIntent::VariableCost, int, 1024, WorkTag, 4, 8, Large>>(reports, "type.work.int.cap1024");
    add_type<cc::AutoRoute_t<cc::RouteIntent::VariableCost, std::uint64_t, 2048, WorkTagB, 32, 32, Huge>>(reports, "type.work.u64.cap2048");

    add_type<cc::AutoRoute_t<cc::RouteIntent::Shardable, int, 64, ShardTag, 1, 1, Small>>(reports, "type.shardable.small.spsc");
    add_type<cc::AutoRoute_t<cc::RouteIntent::Shardable, int, 64, ShardTag, 1, 1, Large>>(reports, "type.shardable.large.grid4");
    add_type<cc::AutoRoute_t<cc::RouteIntent::Shardable, int, 64, ShardTag, 1, 1, Huge>>(reports, "type.shardable.huge.grid16");
    add_type<cc::AutoRoute_t<cc::RouteIntent::Shardable, Payload64, 64, ShardTagB, 4, 4, Large>>(reports, "type.shardable.payload64.grid4");
    add_type<cc::AutoRoute_t<cc::RouteIntent::Shardable, Payload256, 64, ShardTagB, 4, 4, Huge, 8>>(reports, "type.shardable.payload256.grid8");
    add_type<cc::AutoRoute_t<cc::RouteIntent::Shardable, Payload512, 64, ShardTagB, 4, 4, Huge, 16>>(reports, "type.shardable.payload512.grid16");
}

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const HardwareSnapshot& hw = hardware_once();

    std::printf("=== auto_router ===\n");
    std::printf("  topology_source=%s l1d=%zu l2=%zu l3=%zu line=%zu "
                "cores=%zu smt=%zu process_cpus=%zu numa=%zu page=%zu "
                "huge2m=%s avx512=%s avx2=%s sse4.2=%s\n",
                source_name(hw.source),
                hw.l1d,
                hw.l2,
                hw.l3,
                hw.line,
                hw.physical_cores,
                hw.smt_threads,
                hw.process_cpus,
                hw.numa_nodes,
                hw.page_size,
                hw.hugepage_2mb ? "yes" : "no",
                hw.avx512 ? "yes" : "no",
                hw.avx2 ? "yes" : "no",
                hw.sse42 ? "yes" : "no");
    std::printf("  router_cliff=%zu bytes reports=startup-built\n\n",
                cc::conservative_cliff_l2_per_core);

    std::vector<bench::Report> reports;
    reports.reserve(136);

    add_hardware_reports(reports, hw);
    add_stream_decisions(reports);
    add_shardable_decisions(reports);
    add_latest_and_work_decisions(reports);
    add_shard_caps(reports);
    add_digest_cases(reports);
    add_implementation_comparison(reports, hw);
    add_type_cases(reports);

    std::printf("  report_count=%zu\n\n", reports.size());
    bench::emit_reports(reports, bench::env_json());
    return 0;
}
