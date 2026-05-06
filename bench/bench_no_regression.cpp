// AdaptiveScheduler no-regression bench.
//
// This is the first SEPLOG-E1 gate for the cache-tier rule:
// L1/L2 workloads must stay on the inline path, while L3/DRAM
// workloads must route through the scheduler without losing to the
// sequential baseline.  The "forced" baseline intentionally models
// naive per-call thread fanout: it spawns workers for every iteration
// regardless of the working set, which is the overhead the scheduler
// is meant to avoid.

#include <crucible/concurrent/AdaptiveScheduler.h>

#include "bench_harness.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace cc = crucible::concurrent;
namespace cs = crucible::concurrent::scheduler;

namespace {

constexpr std::size_t KiB = 1024;
constexpr std::size_t MiB = 1024 * KiB;
constexpr std::size_t GiB = 1024 * MiB;
constexpr std::size_t kL3GraphPasses = 16;
constexpr double kGateTolerance = 1.05;
constexpr double kInlineAbsoluteToleranceNs = 500.0;

enum class WorkloadKind : std::uint8_t {
    L1ArraySum,
    L2MatrixMultiply,
    L3GraphTraversal,
    DramStreamFold,
};

enum class Strategy : std::uint8_t {
    Sequential,
    Adaptive,
    ForcedParallel,
};

struct Range {
    std::size_t begin = 0;
    std::size_t end = 0;
};

[[nodiscard]] constexpr const char* strategy_name(Strategy s) noexcept {
    switch (s) {
    case Strategy::Sequential:     return "sequential";
    case Strategy::Adaptive:       return "adaptive";
    case Strategy::ForcedParallel: return "forced_parallel";
    default:                       return "?";
    }
}

[[nodiscard]] constexpr Range split_range(std::size_t total,
                                          std::size_t index,
                                          std::size_t count) noexcept {
    const std::size_t safe_count = std::max<std::size_t>(1, count);
    const std::size_t chunk = (total + safe_count - 1) / safe_count;
    const std::size_t begin = std::min(total, index * chunk);
    const std::size_t end = std::min(total, begin + chunk);
    return Range{begin, end};
}

[[nodiscard]] std::size_t env_usize(const char* name,
                                    std::size_t fallback) noexcept {
    const char* raw = std::getenv(name);
    if (raw == nullptr || *raw == '\0') return fallback;
    std::size_t value = 0;
    const std::string_view text{raw};
    const auto [ptr, ec] = std::from_chars(
        text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size()) {
        return fallback;
    }
    return value == 0 ? fallback : value;
}

struct Workload {
    const char* name = "";
    WorkloadKind kind = WorkloadKind::L1ArraySum;
    cc::WorkBudget budget{};
    std::size_t units = 0;
    std::size_t forced_workers = 1;

    std::vector<std::uint64_t> l1;
    std::vector<double> matrix_a;
    std::vector<double> matrix_b;
    std::vector<double> matrix_c;
    std::size_t matrix_n = 0;
    std::vector<std::uint32_t> graph_next;
    std::vector<std::uint64_t> dram;

    void run_range(Range range, std::atomic<std::uint64_t>& sink) noexcept {
        switch (kind) {
        case WorkloadKind::L1ArraySum:
            run_l1_sum(range, sink);
            break;
        case WorkloadKind::L2MatrixMultiply:
            run_l2_matmul(range, sink);
            break;
        case WorkloadKind::L3GraphTraversal:
            run_l3_graph(range, sink);
            break;
        case WorkloadKind::DramStreamFold:
            run_dram_stream(range, sink);
            break;
        default:
            break;
        }
    }

private:
    void run_l1_sum(Range range, std::atomic<std::uint64_t>& sink) noexcept {
        std::uint64_t acc = 0;
        for (std::size_t i = range.begin; i < range.end; ++i) {
            acc += l1[i];
        }
        bench::do_not_optimize(acc);
        sink.fetch_add(acc, std::memory_order_relaxed);
    }

    void run_l2_matmul(Range rows, std::atomic<std::uint64_t>& sink) noexcept {
        double acc = 0.0;
        const std::size_t n = matrix_n;
        for (std::size_t i = rows.begin; i < rows.end; ++i) {
            for (std::size_t j = 0; j < n; ++j) {
                double sum = 0.0;
                for (std::size_t k = 0; k < n; ++k) {
                    sum += matrix_a[i * n + k] * matrix_b[k * n + j];
                }
                matrix_c[i * n + j] = sum;
                acc += sum;
            }
        }
        bench::do_not_optimize(acc);
        sink.fetch_add(static_cast<std::uint64_t>(acc), std::memory_order_relaxed);
    }

    void run_l3_graph(Range starts, std::atomic<std::uint64_t>& sink) noexcept {
        const std::uint32_t mask =
            static_cast<std::uint32_t>(graph_next.size() - 1);
        std::uint64_t acc = 0;
        for (std::size_t pass = 0; pass < kL3GraphPasses; ++pass) {
            for (std::size_t i = starts.begin; i < starts.end; ++i) {
                const std::uint32_t node = graph_next[(i + pass) & mask];
                acc += graph_next[node & mask];
            }
        }
        bench::do_not_optimize(acc);
        sink.fetch_add(acc, std::memory_order_relaxed);
    }

    void run_dram_stream(Range lines, std::atomic<std::uint64_t>& sink) noexcept {
        std::uint64_t acc = 0;
        for (std::size_t line = lines.begin; line < lines.end; ++line) {
            acc += dram[line * 8];
        }
        bench::do_not_optimize(acc);
        sink.fetch_add(acc, std::memory_order_relaxed);
    }
};

[[nodiscard]] Workload make_l1(std::size_t forced_workers) {
    Workload w{};
    w.name = "L1d.array_sum.16KiB";
    w.kind = WorkloadKind::L1ArraySum;
    w.budget = cc::WorkBudget{
        .read_bytes = 16 * KiB,
        .write_bytes = 16 * KiB,
        .item_count = (16 * KiB) / sizeof(std::uint64_t),
    };
    w.units = (16 * KiB) / sizeof(std::uint64_t);
    w.forced_workers = forced_workers;
    w.l1.resize(w.units);
    for (std::size_t i = 0; i < w.l1.size(); ++i) {
        w.l1[i] = i ^ 0x9E37'79B9ULL;
    }
    return w;
}

[[nodiscard]] Workload make_l2(std::size_t forced_workers) {
    constexpr std::size_t n = 146;
    Workload w{};
    w.name = "L2.matrix_multiply.512KB";
    w.kind = WorkloadKind::L2MatrixMultiply;
    w.budget = cc::WorkBudget{
        .read_bytes = 2 * n * n * sizeof(double),
        .write_bytes = n * n * sizeof(double),
        .item_count = n * n,
    };
    w.units = n;
    w.forced_workers = forced_workers;
    w.matrix_n = n;
    w.matrix_a.resize(n * n);
    w.matrix_b.resize(n * n);
    w.matrix_c.resize(n * n);
    for (std::size_t i = 0; i < n * n; ++i) {
        w.matrix_a[i] = static_cast<double>((i % 17) + 1) * 0.03125;
        w.matrix_b[i] = static_cast<double>((i % 29) + 1) * 0.015625;
    }
    return w;
}

[[nodiscard]] Workload make_l3(std::size_t forced_workers) {
    constexpr std::size_t bytes = 16 * MiB;
    constexpr std::size_t entries = bytes / sizeof(std::uint32_t);
    static_assert((entries & (entries - 1)) == 0);
    Workload w{};
    w.name = "L3.graph_traversal.16MiB";
    w.kind = WorkloadKind::L3GraphTraversal;
    w.budget = cc::WorkBudget{
        .read_bytes = bytes,
        .write_bytes = 0,
        .item_count = entries,
    };
    w.units = entries;
    w.forced_workers = forced_workers;
    w.graph_next.resize(entries);
    for (std::size_t i = 0; i < entries; ++i) {
        w.graph_next[i] = static_cast<std::uint32_t>((i + 257) & (entries - 1));
    }
    return w;
}

[[nodiscard]] Workload make_dram(std::size_t forced_workers) {
    constexpr std::size_t bytes = GiB;
    constexpr std::size_t elems = bytes / sizeof(std::uint64_t);
    Workload w{};
    w.name = "DRAM.stream_fold.1GiB";
    w.kind = WorkloadKind::DramStreamFold;
    w.budget = cc::WorkBudget{
        .read_bytes = bytes,
        .write_bytes = 0,
        .item_count = bytes / 64,
    };
    w.units = bytes / 64;
    w.forced_workers = forced_workers;
    w.dram.resize(elems);
    for (std::size_t i = 0; i < w.dram.size(); i += 8) {
        w.dram[i] = (i * 0x9E37'79B9'7F4A'7C15ULL) ^ (i >> 7);
    }
    return w;
}

void run_sequential(Workload& w, std::atomic<std::uint64_t>& sink) noexcept {
    w.run_range(Range{0, w.units}, sink);
}

void run_adaptive(cc::Pool<cs::LocalityAware>& pool,
                  Workload& w,
                  std::atomic<std::uint64_t>& sink) {
    const cc::WorkloadProfile profile = cc::WorkloadProfile::from_budget(
        w.budget,
        pool.worker_count());
    const auto result = cc::dispatch_with_workload(
        pool,
        profile,
        [&](cc::WorkShard shard) {
            const Range r = split_range(w.units, shard.index, shard.count);
            w.run_range(r, sink);
        });
    if (result.queued) pool.wait_idle();
}

void run_forced_parallel(Workload& w, std::atomic<std::uint64_t>& sink) {
    std::vector<std::jthread> threads;
    threads.reserve(w.forced_workers);
    for (std::size_t worker = 0; worker < w.forced_workers; ++worker) {
        threads.emplace_back([&, worker](std::stop_token) noexcept {
            const Range r = split_range(w.units, worker, w.forced_workers);
            w.run_range(r, sink);
        });
    }
}

[[nodiscard]] bench::Report measure_once(std::string name,
                                         Strategy strategy,
                                         Workload& w,
                                         cc::Pool<cs::LocalityAware>& pool,
                                         std::size_t samples) {
    std::atomic<std::uint64_t> sink{0};
    bench::Run run{std::move(name)};
    if (const int core = bench::env_core(); core >= 0) {
        (void)run.core(core);
    }
    return run.samples(samples)
        .warmup(std::max<std::size_t>(2, samples / 6))
        .batch(1)
        .max_wall_ms(8'000)
        .measure([&] {
            sink.store(0, std::memory_order_relaxed);
            switch (strategy) {
            case Strategy::Sequential:
                run_sequential(w, sink);
                break;
            case Strategy::Adaptive:
                run_adaptive(pool, w, sink);
                break;
            case Strategy::ForcedParallel:
                run_forced_parallel(w, sink);
                break;
            default:
                break;
            }
            bench::do_not_optimize(sink.load(std::memory_order_relaxed));
        });
}

[[nodiscard]] bench::Report measure_stable(std::string name,
                                           Strategy strategy,
                                           Workload& w,
                                           cc::Pool<cs::LocalityAware>& pool,
                                           std::size_t samples) {
    bench::Report best;
    double best_cv = std::numeric_limits<double>::infinity();
    for (int attempt = 0; attempt < 3; ++attempt) {
        bench::Report current = measure_once(name, strategy, w, pool, samples);
        if (!current.noisy(0.05)) return current;
        if (current.pct.cv < best_cv) {
            best_cv = current.pct.cv;
            best = std::move(current);
        }
    }
    return best;
}

struct Trio {
    bench::Report sequential;
    bench::Report adaptive;
    bench::Report forced;
};

[[nodiscard]] Trio run_workload(Workload& w,
                                cc::Pool<cs::LocalityAware>& pool,
                                std::size_t samples) {
    auto make_name = [&](Strategy s) {
        std::string name{"no_regression."};
        name += w.name;
        name += '.';
        name += strategy_name(s);
        return name;
    };
    return Trio{
        .sequential = measure_stable(make_name(Strategy::Sequential),
                                     Strategy::Sequential, w, pool, samples),
        .adaptive = measure_stable(make_name(Strategy::Adaptive),
                                   Strategy::Adaptive, w, pool, samples),
        .forced = measure_stable(make_name(Strategy::ForcedParallel),
                                 Strategy::ForcedParallel, w, pool, samples),
    };
}

[[nodiscard]] double ratio(double lhs, double rhs) noexcept {
    return rhs > 0.0 ? lhs / rhs : 0.0;
}

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const auto& topo = cc::Topology::instance();
    const std::size_t default_forced_workers = std::max<std::size_t>(
        2, std::min<std::size_t>(1024, topo.process_cpu_count() * 32));
    const std::size_t forced_workers =
        env_usize("CRUCIBLE_NO_REGRESSION_FORCED_WORKERS",
                  default_forced_workers);
    const std::size_t pool_workers = std::max<std::size_t>(
        2, std::min<std::size_t>(16, topo.process_cpu_count()));
    const std::size_t samples = std::max<std::size_t>(
        10, env_usize("CRUCIBLE_NO_REGRESSION_SAMPLES", 12));

    std::printf("=== adaptive_scheduler no-regression ===\n");
    std::printf("  samples=%zu forced_workers=%zu pool_workers=%zu\n",
                samples, forced_workers, pool_workers);
    std::printf("  gates: small tiers forced>=sequential/1.05, "
                "adaptive<=forced*1.05, and inline overhead<=5%% or %.0fns\n",
                kInlineAbsoluteToleranceNs);
    std::printf("         large tiers adaptive<=sequential*1.05 and "
                "adaptive<=forced*0.50\n\n");

    cc::Pool<cs::LocalityAware> pool{cc::CoreCount{pool_workers}};

    std::vector<Workload> workloads;
    workloads.reserve(4);
    workloads.push_back(make_l1(forced_workers));
    workloads.push_back(make_l2(forced_workers));
    workloads.push_back(make_l3(forced_workers));
    workloads.push_back(make_dram(forced_workers));

    std::vector<bench::Report> reports;
    reports.reserve(workloads.size() * 3);

    int failures = 0;
    std::printf("=== measuring ===\n");
    for (std::size_t i = 0; i < workloads.size(); ++i) {
        Workload& w = workloads[i];
        Trio trio = run_workload(w, pool, samples);
        reports.push_back(std::move(trio.sequential));
        reports.push_back(std::move(trio.adaptive));
        reports.push_back(std::move(trio.forced));
    }

    bench::emit_reports_text(reports);

    std::printf("\n=== no-regression gates ===\n");
    for (std::size_t i = 0; i < workloads.size(); ++i) {
        const bench::Report& seq = reports[i * 3 + 0];
        const bench::Report& adp = reports[i * 3 + 1];
        const bench::Report& frc = reports[i * 3 + 2];
        const Workload& w = workloads[i];
        const double seq_p50 = seq.pct.p50;
        const double adp_p50 = adp.pct.p50;
        const double frc_p50 = frc.pct.p50;
        const auto dec = cc::ParallelismRule::recommend(w.budget);
        std::printf("  %-26s tier=%u rule=%s factor=%zu "
                    "adaptive/seq=%.3fx adaptive/forced=%.3fx\n",
                    w.name,
                    static_cast<unsigned>(dec.tier),
                    dec.is_parallel() ? "parallel" : "sequential",
                    dec.factor,
                    ratio(adp_p50, seq_p50),
                    ratio(adp_p50, frc_p50));

        const bool small = w.kind == WorkloadKind::L1ArraySum ||
                           w.kind == WorkloadKind::L2MatrixMultiply;
        if (small && dec.is_parallel()) {
            std::printf("  FAIL %-26s expected sequential decision for small tier\n",
                        w.name);
            ++failures;
        }
        if (!small && !dec.is_parallel()) {
            std::printf("  FAIL %-26s expected parallel decision for large tier\n",
                        w.name);
            ++failures;
        }

        const bool noisy = seq.noisy(0.05) || adp.noisy(0.05) || frc.noisy(0.05);
        if (noisy) {
            if (seq.noisy(0.05)) {
                std::printf("  INVALID %-26s sequential cv=%.1f%% > 5%%\n",
                            w.name, seq.pct.cv * 100.0);
                ++failures;
            }
            if (adp.noisy(0.05)) {
                std::printf("  INVALID %-26s adaptive cv=%.1f%% > 5%%\n",
                            w.name, adp.pct.cv * 100.0);
                ++failures;
            }
            if (frc.noisy(0.05)) {
                std::printf("  INVALID %-26s forced cv=%.1f%% > 5%%\n",
                            w.name, frc.pct.cv * 100.0);
                ++failures;
            }
            continue;
        }

        if (small) {
            if (frc_p50 * kGateTolerance < seq_p50) {
                std::printf("  FAIL %-26s forced parallel beat sequential by >5%%: %.3fx\n",
                            w.name, ratio(frc_p50, seq_p50));
                ++failures;
            }
            if (adp_p50 > frc_p50 * kGateTolerance) {
                std::printf("  FAIL %-26s adaptive slower than forced by >5%%: %.3fx\n",
                            w.name, ratio(adp_p50, frc_p50));
                ++failures;
            }
            if (adp_p50 > seq_p50 * kGateTolerance &&
                (adp_p50 - seq_p50) > kInlineAbsoluteToleranceNs) {
                std::printf("  FAIL %-26s adaptive inline overhead exceeded budget: %.3fx\n",
                            w.name, ratio(adp_p50, seq_p50));
                ++failures;
            }
        } else {
            if (adp_p50 > seq_p50 * kGateTolerance) {
                std::printf("  FAIL %-26s adaptive regressed vs sequential: %.3fx\n",
                            w.name, ratio(adp_p50, seq_p50));
                ++failures;
            }
            if (adp_p50 > frc_p50 * 0.50) {
                std::printf("  FAIL %-26s adaptive did not beat forced by 2x: %.3fx\n",
                            w.name, ratio(adp_p50, frc_p50));
                ++failures;
            }
        }
    }

    if (pool.failed() != 0) {
        std::printf("  FAIL pool recorded failed jobs=%llu\n",
                    static_cast<unsigned long long>(pool.failed()));
        ++failures;
    }

    std::printf("\n=== verdict ===\n");
    if (failures == 0) {
        std::printf("  PASS - AdaptiveScheduler cache-tier routing stayed within gates.\n");
    } else {
        std::printf("  FAIL - %d no-regression gate(s) failed.\n", failures);
    }

    bench::emit_reports_json(reports, bench::env_json());
    return failures == 0 ? 0 : 1;
}
