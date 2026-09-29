// No-regression bench for the cache-tier rule.
//
// L1/L2 workloads must stay on the inline path, while L3/DRAM
// workloads must go parallel without losing to the sequential
// baseline.  The "rule" arm runs the work through
// fixy::spawn::mint_parallel_for, which asks
// fixy::concurrent::ParallelismRule for a decision and runs at the
// factor of that decision.  The "forced" baseline intentionally
// models naive per-call thread fanout: it spawns workers for every
// iteration regardless of the working set, which is the overhead the
// rule is meant to avoid.

#include <fixy/OwnedRegion.h>
#include <fixy/concurrent/ParallelismRule.h>
#include <fixy/concurrent/Topology.h>
#include <fixy/os/Spawn.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include "bench_harness.h"

#include <algorithm>
#include <array>
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

namespace fc = ::fixy::concurrent;
namespace eff = ::foundation::effects;
namespace perm = ::foundation::permissions;

// The permission over the piece table that the rule arm splits into
// shards.
struct PieceTableWhole {
    using permission_row = eff::Row<>;
};

namespace {

// mint_parallel_for can start threads, so its context owns Bg.
using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;

// The work divides into this many pieces, the top of the factor ladder
// of fixy/concurrent/ParallelismRule.h.  Each piece is one shard of
// mint_parallel_for.  Each factor of the ladder divides the count, so
// every thread of the mint runs the same number of pieces.
constexpr std::size_t kPieces = 16;

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
    Rule,
    ForcedParallel,
};

struct Range {
    std::size_t begin = 0;
    std::size_t end = 0;
};

[[nodiscard]] constexpr const char* strategy_name(Strategy s) noexcept {
    switch (s) {
        case Strategy::Sequential:
            return "sequential";
        case Strategy::Rule:
            return "rule";
        case Strategy::ForcedParallel:
            return "forced_parallel";
        default:
            return "?";
    }
}

[[nodiscard]] constexpr Range split_range(std::size_t total, std::size_t index, std::size_t count) noexcept {
    const std::size_t safe_count = std::max<std::size_t>(1, count);
    const std::size_t chunk = (total + safe_count - 1) / safe_count;
    const std::size_t begin = std::min(total, index * chunk);
    const std::size_t end = std::min(total, begin + chunk);
    return Range{begin, end};
}

[[nodiscard]] std::size_t env_usize(const char* name, std::size_t fallback) noexcept {
    const char* raw = std::getenv(name);
    if (raw == nullptr || *raw == '\0') return fallback;
    std::size_t value = 0;
    const std::string_view text{raw};
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size()) {
        return fallback;
    }
    return value == 0 ? fallback : value;
}

struct Workload {
    const char* name = "";
    WorkloadKind kind = WorkloadKind::L1ArraySum;
    fc::WorkBudget budget{};
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
        const std::uint32_t mask = static_cast<std::uint32_t>(graph_next.size() - 1);
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
    w.budget = fc::WorkBudget{
        .read_bytes = 16 * KiB,
        .write_bytes = 16 * KiB,
        .item_count = (16 * KiB) / sizeof(std::uint64_t),
    };
    w.units = (16 * KiB) / sizeof(std::uint64_t);
    w.forced_workers = forced_workers;
    w.l1.resize(w.units);
    for (std::size_t i = 0; i < w.l1.size(); ++i) {
        w.l1[i] = i ^ 0x9E3779B9ULL;
    }
    return w;
}

[[nodiscard]] Workload make_l2(std::size_t forced_workers) {
    constexpr std::size_t n = 146;
    Workload w{};
    w.name = "L2.matrix_multiply.512KB";
    w.kind = WorkloadKind::L2MatrixMultiply;
    w.budget = fc::WorkBudget{
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
    w.budget = fc::WorkBudget{
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
    w.budget = fc::WorkBudget{
        .read_bytes = bytes,
        .write_bytes = 0,
        .item_count = bytes / 64,
    };
    w.units = bytes / 64;
    w.forced_workers = forced_workers;
    w.dram.resize(elems);
    for (std::size_t i = 0; i < w.dram.size(); i += 8) {
        w.dram[i] = (i * 0x9E3779B97F4A7C15ULL) ^ (i >> 7);
    }
    return w;
}

void run_sequential(Workload& w, std::atomic<std::uint64_t>& sink) noexcept { w.run_range(Range{0, w.units}, sink); }

// The piece table that the rule arm splits.  Element i names piece i
// of the work, and a shard runs the pieces of its slice.
using PieceTable = std::array<std::uint32_t, kPieces>;

[[nodiscard]] PieceTable make_piece_table() noexcept {
    PieceTable table{};
    for (std::uint32_t piece = 0; piece < kPieces; ++piece) {
        table[piece] = piece;
    }
    return table;
}

// Runs the work through mint_parallel_for, one shard for each piece.
// The mint asks the rule for a decision on each call and starts no more
// threads than the factor of that decision.  A sequential decision runs
// every piece on the calling thread.
void run_rule(Workload& w, std::atomic<std::uint64_t>& sink, PieceTable& pieces) noexcept {
    const BgCtx ctx{eff::testing::bg()};
    auto region = ::fixy::OwnedRegion<std::uint32_t, PieceTableWhole>::wrap(
        pieces.data(), kPieces, perm::mint_permission_root<PieceTableWhole>());
    auto whole =
        ::fixy::spawn::mint_parallel_for<kPieces>(ctx, w.budget, std::move(region), [&w, &sink](auto& shard) noexcept {
            for (const std::uint32_t piece : shard) {
                w.run_range(split_range(w.units, piece, kPieces), sink);
            }
        });
    (void)whole;
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

[[nodiscard]] bench::Report measure_once(std::string name, Strategy strategy, Workload& w, PieceTable& pieces,
                                         std::size_t samples) {
    std::atomic<std::uint64_t> sink{0};
    bench::Run run{std::move(name)};
    if (const int core = bench::env_core(); core >= 0) {
        (void)run.core(core);
    }
    return run.samples(samples).warmup(std::max<std::size_t>(2, samples / 6)).batch(1).max_wall_ms(8000).measure([&] {
        sink.store(0, std::memory_order_relaxed);
        switch (strategy) {
            case Strategy::Sequential:
                run_sequential(w, sink);
                break;
            case Strategy::Rule:
                run_rule(w, sink, pieces);
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

[[nodiscard]] bench::Report measure_stable(std::string name, Strategy strategy, Workload& w, PieceTable& pieces,
                                           std::size_t samples) {
    bench::Report best;
    double best_cv = std::numeric_limits<double>::infinity();
    for (int attempt = 0; attempt < 3; ++attempt) {
        bench::Report current = measure_once(name, strategy, w, pieces, samples);
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
    bench::Report rule;
    bench::Report forced;
};

[[nodiscard]] Trio run_workload(Workload& w, PieceTable& pieces, std::size_t samples) {
    auto make_name = [&](Strategy s) {
        std::string name{"no_regression."};
        name += w.name;
        name += '.';
        name += strategy_name(s);
        return name;
    };
    return Trio{
        .sequential = measure_stable(make_name(Strategy::Sequential), Strategy::Sequential, w, pieces, samples),
        .rule = measure_stable(make_name(Strategy::Rule), Strategy::Rule, w, pieces, samples),
        .forced = measure_stable(make_name(Strategy::ForcedParallel), Strategy::ForcedParallel, w, pieces, samples),
    };
}

[[nodiscard]] double ratio(double lhs, double rhs) noexcept { return rhs > 0.0 ? lhs / rhs : 0.0; }

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const auto& topo = fc::Topology::instance();
    const std::size_t default_forced_workers =
        std::max<std::size_t>(2, std::min<std::size_t>(1024, topo.process_cpu_count() * 32));
    const std::size_t forced_workers = env_usize("CRUCIBLE_NO_REGRESSION_FORCED_WORKERS", default_forced_workers);
    const std::size_t samples = std::max<std::size_t>(10, env_usize("CRUCIBLE_NO_REGRESSION_SAMPLES", 12));

    std::printf("=== parallelism rule no-regression ===\n");
    std::printf("  samples=%zu forced_workers=%zu pieces=%zu\n", samples, forced_workers, kPieces);
    std::printf("  gates: small tiers forced>=sequential/1.05, "
                "rule<=forced*1.05, and inline overhead<=5%% or %.0fns\n",
                kInlineAbsoluteToleranceNs);
    std::printf("         large tiers rule<=sequential*1.05 and "
                "rule<=forced*0.50\n\n");

    PieceTable pieces = make_piece_table();

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
        Trio trio = run_workload(w, pieces, samples);
        reports.push_back(std::move(trio.sequential));
        reports.push_back(std::move(trio.rule));
        reports.push_back(std::move(trio.forced));
    }

    bench::emit_reports_text(reports);

    std::printf("\n=== no-regression gates ===\n");
    for (std::size_t i = 0; i < workloads.size(); ++i) {
        const bench::Report& seq = reports[i * 3 + 0];
        const bench::Report& rul = reports[i * 3 + 1];
        const bench::Report& frc = reports[i * 3 + 2];
        const Workload& w = workloads[i];
        const double seq_p50 = seq.pct.p50;
        const double rul_p50 = rul.pct.p50;
        const double frc_p50 = frc.pct.p50;
        const auto dec = fc::ParallelismRule::recommend(w.budget);
        std::printf("  %-26s tier=%u rule=%s factor=%zu "
                    "rule/seq=%.3fx rule/forced=%.3fx\n",
                    w.name, static_cast<unsigned>(dec.tier), dec.is_parallel() ? "parallel" : "sequential", dec.factor,
                    ratio(rul_p50, seq_p50), ratio(rul_p50, frc_p50));

        const bool small = w.kind == WorkloadKind::L1ArraySum || w.kind == WorkloadKind::L2MatrixMultiply;
        if (small && dec.is_parallel()) {
            std::printf("  FAIL %-26s expected sequential decision for small tier\n", w.name);
            ++failures;
        }
        if (!small && !dec.is_parallel()) {
            std::printf("  FAIL %-26s expected parallel decision for large tier\n", w.name);
            ++failures;
        }

        const bool noisy = seq.noisy(0.05) || rul.noisy(0.05) || frc.noisy(0.05);
        if (noisy) {
            if (seq.noisy(0.05)) {
                std::printf("  INVALID %-26s sequential cv=%.1f%% > 5%%\n", w.name, seq.pct.cv * 100.0);
                ++failures;
            }
            if (rul.noisy(0.05)) {
                std::printf("  INVALID %-26s rule cv=%.1f%% > 5%%\n", w.name, rul.pct.cv * 100.0);
                ++failures;
            }
            if (frc.noisy(0.05)) {
                std::printf("  INVALID %-26s forced cv=%.1f%% > 5%%\n", w.name, frc.pct.cv * 100.0);
                ++failures;
            }
            continue;
        }

        if (small) {
            if (frc_p50 * kGateTolerance < seq_p50) {
                std::printf("  FAIL %-26s forced parallel beat sequential by >5%%: %.3fx\n", w.name,
                            ratio(frc_p50, seq_p50));
                ++failures;
            }
            if (rul_p50 > frc_p50 * kGateTolerance) {
                std::printf("  FAIL %-26s rule slower than forced by >5%%: %.3fx\n", w.name, ratio(rul_p50, frc_p50));
                ++failures;
            }
            if (rul_p50 > seq_p50 * kGateTolerance && (rul_p50 - seq_p50) > kInlineAbsoluteToleranceNs) {
                std::printf("  FAIL %-26s rule inline overhead exceeded budget: %.3fx\n", w.name,
                            ratio(rul_p50, seq_p50));
                ++failures;
            }
        } else {
            if (rul_p50 > seq_p50 * kGateTolerance) {
                std::printf("  FAIL %-26s rule regressed vs sequential: %.3fx\n", w.name, ratio(rul_p50, seq_p50));
                ++failures;
            }
            if (rul_p50 > frc_p50 * 0.50) {
                std::printf("  FAIL %-26s rule did not beat forced by 2x: %.3fx\n", w.name, ratio(rul_p50, frc_p50));
                ++failures;
            }
        }
    }

    std::printf("\n=== verdict ===\n");
    if (failures == 0) {
        std::printf("  PASS - the parallelism rule stayed within the cache-tier gates.\n");
    } else {
        std::printf("  FAIL - %d no-regression gate(s) failed.\n", failures);
    }

    bench::emit_reports_json(reports, bench::env_json());
    return failures == 0 ? 0 : 1;
}
