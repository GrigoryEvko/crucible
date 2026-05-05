// AdaptiveScheduler Pool<Policy> bench — per-policy dispatch tail and throughput.

#include <crucible/concurrent/AdaptiveScheduler.h>

#include "bench_harness.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace cc = crucible::concurrent;
namespace cs = crucible::concurrent::scheduler;

namespace {

constexpr std::size_t kJobsPerPolicy = 4096;
constexpr std::size_t kWorkers       = 2;

struct PriorityTicketKey {
    static std::uint64_t key(cc::adaptive_detail::ticket_type ticket) noexcept {
        return ticket;
    }
};

using DeadlinePolicy = cs::Deadline<PriorityTicketKey, 4, 64, 16, 1>;
using CfsPolicy = cs::Cfs<PriorityTicketKey, 4, 64, 16, 1>;
using EevdfPolicy = cs::Eevdf<PriorityTicketKey, 4, 64, 16, 1>;
using DeadlinePerShardPolicy =
    cs::DeadlinePerShard<PriorityTicketKey, 4, 64, 16, 1>;
using CfsPerShardPolicy = cs::CfsPerShard<PriorityTicketKey, 4, 64, 16, 1>;
using EevdfPerShardPolicy =
    cs::EevdfPerShard<PriorityTicketKey, 4, 64, 16, 1>;

struct BenchResult {
    const char* policy_name = "";
    bench::Percentiles dispatch_ns{};
    double jobs_per_sec = 0.0;
    double wall_ms = 0.0;
    std::uint64_t submitted = 0;
    std::uint64_t completed = 0;
    std::uint64_t failed = 0;
};

template <typename Policy>
[[nodiscard]] BenchResult bench_policy(const char* policy_name) {
    cc::Pool<Policy> pool{cc::CoreCount{kWorkers}};
    std::atomic<std::uint64_t> completed_body{0};
    std::vector<double> dispatch_samples;
    dispatch_samples.reserve(kJobsPerPolicy);

    const double nspc = bench::Timer::ns_per_cycle();
    const auto wall_start = std::chrono::steady_clock::now();

    for (std::size_t i = 0; i < kJobsPerPolicy; ++i) {
        const std::uint64_t t0 = bench::rdtsc_start();
        cc::dispatch(pool, [&completed_body] {
            completed_body.fetch_add(1, std::memory_order_relaxed);
        });
        const std::uint64_t t1 = bench::rdtsc_end();
        const std::uint64_t raw = t1 - t0;
        const std::uint64_t d =
            raw > bench::Timer::overhead_cycles()
                ? raw - bench::Timer::overhead_cycles()
                : 0;
        dispatch_samples.push_back(static_cast<double>(d) * nspc);
    }

    pool.wait_idle();
    const auto wall_end = std::chrono::steady_clock::now();
    const auto wall_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        wall_end - wall_start).count();

    const double wall_ms = static_cast<double>(wall_ns) / 1'000'000.0;
    const double jobs_per_sec = wall_ns > 0
        ? static_cast<double>(kJobsPerPolicy) * 1'000'000'000.0 /
              static_cast<double>(wall_ns)
        : 0.0;

    if (completed_body.load(std::memory_order_relaxed) != kJobsPerPolicy) {
        std::fprintf(stderr, "%s body completion mismatch: %llu/%zu\n",
                     policy_name,
                     static_cast<unsigned long long>(
                         completed_body.load(std::memory_order_relaxed)),
                     kJobsPerPolicy);
        std::abort();
    }

    return BenchResult{
        .policy_name = policy_name,
        .dispatch_ns = bench::Percentiles::compute(dispatch_samples),
        .jobs_per_sec = jobs_per_sec,
        .wall_ms = wall_ms,
        .submitted = pool.submitted(),
        .completed = pool.completed(),
        .failed = pool.failed(),
    };
}

void print_result(const BenchResult& r) noexcept {
    std::printf(
        "  %-18s jobs/sec=%12.2f wall=%8.3fms "
        "dispatch[p50=%8.2fns p99=%8.2fns p99.9=%8.2fns] "
        "submitted=%llu completed=%llu failed=%llu\n",
        r.policy_name,
        r.jobs_per_sec,
        r.wall_ms,
        r.dispatch_ns.p50,
        r.dispatch_ns.p99,
        r.dispatch_ns.p99_9,
        static_cast<unsigned long long>(r.submitted),
        static_cast<unsigned long long>(r.completed),
        static_cast<unsigned long long>(r.failed));
}

void print_json(const std::vector<BenchResult>& results) noexcept {
    std::printf("\n=== json ===\n[\n");
    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        std::printf(
            "  {\"policy\":\"%s\",\"jobs_per_sec\":%.6f,"
            "\"wall_ms\":%.6f,\"dispatch_p50_ns\":%.6f,"
            "\"dispatch_p99_ns\":%.6f,\"dispatch_p999_ns\":%.6f,"
            "\"submitted\":%llu,\"completed\":%llu,\"failed\":%llu}%s\n",
            r.policy_name,
            r.jobs_per_sec,
            r.wall_ms,
            r.dispatch_ns.p50,
            r.dispatch_ns.p99,
            r.dispatch_ns.p99_9,
            static_cast<unsigned long long>(r.submitted),
            static_cast<unsigned long long>(r.completed),
            static_cast<unsigned long long>(r.failed),
            (i + 1 < results.size()) ? "," : "");
    }
    std::printf("]\n");
}

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    std::printf("=== adaptive_scheduler ===\n");
    std::printf("  jobs_per_policy=%zu workers=%zu\n\n",
                kJobsPerPolicy, kWorkers);

    std::vector<BenchResult> results;
    results.reserve(10);
    results.push_back(bench_policy<cs::Fifo>("Fifo"));
    results.push_back(bench_policy<cs::Lifo>("Lifo"));
    results.push_back(bench_policy<cs::RoundRobin>("RoundRobin"));
    results.push_back(bench_policy<cs::LocalityAware>("LocalityAware"));
    results.push_back(bench_policy<DeadlinePolicy>("Deadline"));
    results.push_back(bench_policy<CfsPolicy>("Cfs"));
    results.push_back(bench_policy<EevdfPolicy>("Eevdf"));
    results.push_back(bench_policy<DeadlinePerShardPolicy>("DeadlinePerShard"));
    results.push_back(bench_policy<CfsPerShardPolicy>("CfsPerShard"));
    results.push_back(bench_policy<EevdfPerShardPolicy>("EevdfPerShard"));

    for (const BenchResult& result : results) {
        print_result(result);
        if (result.failed != 0 || result.completed != result.submitted) {
            std::abort();
        }
    }

    if (bench::env_json()) {
        print_json(results);
    }

    return 0;
}
