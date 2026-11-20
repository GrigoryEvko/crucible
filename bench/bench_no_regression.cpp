// No-regression bench for the cache-tier rule.
//
// L1/L2 workloads must stay on the inline path.  An L3/DRAM workload that
// the rule makes parallel must beat the sequential baseline by
// kParallelWinFactor, and one that the rule keeps sequential, such as on
// one CPU, must cost no more than the sequential baseline.  The "rule" arm
// runs the work through
// fixy::spawn::mint_parallel_for, which asks
// fixy::concurrent::ParallelismRule for a decision and runs at the
// factor of that decision.  The "forced" baseline intentionally
// models naive per-call thread fanout: it spawns workers for every
// iteration regardless of the working set, which is the overhead the
// rule is meant to avoid.

#include <fixy/Core.h>
#include <fixy/OwnedRegion.h>
#include <fixy/concurrent/ParallelismRule.h>
#include <fixy/concurrent/Spawn.h>
#include <fixy/os/Topology.h>
#include <foundation/Quarantine.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include "bench_harness.h"

#include <algorithm>
#include <array>
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
// A parallel decision must earn its threads: at an L3 or DRAM tier the
// rule arm runs at least this many times faster than the sequential arm.
// The bound is stricter than rule <= sequential * kGateTolerance, so the
// large tiers keep the no-regression bound too.
constexpr double kParallelWinFactor = 1.5;
static_assert(1.0 / kParallelWinFactor < kGateTolerance);

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

// The order of the matrices of the matrix multiply: n rows and n columns.
constexpr std::size_t kMatrixOrder = 146;

// One row of the product of the matrix multiply.  The region of the rule arm
// holds one element for each row, so the split gives each shard whole rows.
using ProductRow = ::fixy::FixedArray<double, kMatrixOrder>;

// The inputs of a workload, which the threads of a run read.  Each run of
// elements is a HeapArray, which owns its elements, so two threads can read
// the inputs at the same time (foundation/Transfer.h).  The product of the
// matrix multiply is no input: an arm writes it through a run of its rows.
struct WorkloadInputs {
    WorkloadKind kind = WorkloadKind::L1ArraySum;
    // The count of the units of the work: the elements of the sum, the rows
    // of the product, the starts of the traversal or the lines of the stream.
    std::size_t units = 0;
    ::fixy::HeapArray<std::uint64_t> l1{};
    ::fixy::HeapArray<double> matrix_a{};
    ::fixy::HeapArray<double> matrix_b{};
    std::size_t matrix_n = 0;
    ::fixy::HeapArray<std::uint32_t> graph_next{};
    ::fixy::HeapArray<std::uint64_t> dram{};
};

struct Workload {
    const char* name = "";
    fc::WorkBudget budget{};
    std::size_t forced_workers = 1;
    WorkloadInputs inputs{};
    // The rows of the product of the matrix multiply, which each arm writes.
    // The run is empty for each other workload.
    ::fixy::HeapArray<ProductRow> product{};
};

// The sink of the folds of the kernels.  Each kernel adds its fold one time
// with fetch_add_relaxed.  On x86-64 the add is one lock xadd, the
// instruction of the relaxed add of std::atomic too.
using Sink = ::fixy::Atomic<std::uint64_t, ::fixy::Counter>;

// The tag of the region of the rows of the product, which the rule arm of
// the matrix multiply splits into shards.
struct ProductWhole {
    using permission_row = eff::Row<>;
};

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

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: the kernels index their runs as the memory hierarchy sees them, with no check")
namespace {

void run_l1_sum(WorkloadInputs const& inputs, Range range, Sink& sink) noexcept {
    std::uint64_t const* const l1 = ::foundation::core::foreign_first(inputs.l1.view());
    std::uint64_t acc = 0;
    for (std::size_t i = range.begin; i < range.end; ++i) {
        acc += l1[i];
    }
    bench::do_not_optimize(acc);
    static_cast<void>(sink.fetch_add_relaxed(acc));
}

// The rows of RANGE of the product, in the order i, j, k, into OUT, which
// points at the first row of RANGE.  Each cell is the dot product of its row
// of A and its column of B, in the order of k.
void run_l2_rows(WorkloadInputs const& inputs, Range rows, Sink& sink, ProductRow* out) noexcept {
    double const* const matrix_a = ::foundation::core::foreign_first(inputs.matrix_a.view());
    double const* const matrix_b = ::foundation::core::foreign_first(inputs.matrix_b.view());
    std::size_t const n = inputs.matrix_n;
    double acc = 0.0;
    for (std::size_t i = rows.begin; i < rows.end; ++i) {
        double* const row_out = ::foundation::core::foreign_first(out[i - rows.begin].view());
        for (std::size_t j = 0; j < n; ++j) {
            double sum = 0.0;
            for (std::size_t k = 0; k < n; ++k) {
                sum += matrix_a[i * n + k] * matrix_b[k * n + j];
            }
            row_out[j] = sum;
            acc += sum;
        }
    }
    bench::do_not_optimize(acc);
    static_cast<void>(sink.fetch_add_relaxed(static_cast<std::uint64_t>(acc)));
}

void run_l3_graph(WorkloadInputs const& inputs, Range starts, Sink& sink) noexcept {
    std::uint32_t const* const graph_next = ::foundation::core::foreign_first(inputs.graph_next.view());
    const std::uint32_t mask = static_cast<std::uint32_t>(inputs.graph_next.size() - 1);
    std::uint64_t acc = 0;
    for (std::size_t pass = 0; pass < kL3GraphPasses; ++pass) {
        for (std::size_t i = starts.begin; i < starts.end; ++i) {
            const std::uint32_t node = graph_next[(i + pass) & mask];
            acc += graph_next[node & mask];
        }
    }
    bench::do_not_optimize(acc);
    static_cast<void>(sink.fetch_add_relaxed(acc));
}

void run_dram_stream(WorkloadInputs const& inputs, Range lines, Sink& sink) noexcept {
    std::uint64_t const* const dram = ::foundation::core::foreign_first(inputs.dram.view());
    std::uint64_t acc = 0;
    for (std::size_t line = lines.begin; line < lines.end; ++line) {
        acc += dram[line * 8];
    }
    bench::do_not_optimize(acc);
    static_cast<void>(sink.fetch_add_relaxed(acc));
}

// The units of RANGE of the workload.  The matrix multiply writes the rows of
// RANGE into PRODUCT, which points at the row 0.
void run_range(WorkloadInputs const& inputs, Range range, Sink& sink, ProductRow* product) noexcept {
    switch (inputs.kind) {
        case WorkloadKind::L1ArraySum:
            run_l1_sum(inputs, range, sink);
            break;
        case WorkloadKind::L2MatrixMultiply:
            run_l2_rows(inputs, range, sink, product + range.begin);
            break;
        case WorkloadKind::L3GraphTraversal:
            run_l3_graph(inputs, range, sink);
            break;
        case WorkloadKind::DramStreamFold:
            run_dram_stream(inputs, range, sink);
            break;
        default:
            break;
    }
}

void run_sequential(Workload& w, Sink& sink) noexcept {
    run_range(w.inputs, Range{0, w.inputs.units}, sink, ::foundation::core::foreign_first(w.product.view()));
}

// Runs the work through mint_parallel_for.  The mint asks the rule for a
// decision on each call and starts no more threads than the factor of that
// decision.  A sequential decision runs every shard on the calling thread.
// The matrix multiply splits the rows of its product, so each shard writes
// only its own whole rows.  Each other workload splits the piece table, one
// shard for each piece.  Each shard reads the inputs through a const
// reference, and notes its CPU in the tally.
void run_rule(Workload& w, Sink& sink, PieceTable& pieces, bench::CpuTally& tally) noexcept {
    const BgCtx ctx{eff::testing::bg()};
    if (w.inputs.kind == WorkloadKind::L2MatrixMultiply) {
        ProductRow const* const first_row = ::foundation::core::foreign_first(w.product.view());
        auto region = ::fixy::mint_owned_region(::foundation::core::foreign_first(w.product.view()), w.product.size(),
                                                perm::mint_permission_root<ProductWhole>());
        auto whole = ::fixy::spawn::mint_parallel_for<::fixy::spawn::BodyCoupling::Independent, kPieces>(
            ctx, w.budget, std::move(region),
            [&inputs = ::fixy::as_const(w.inputs), &sink, &tally, first_row](auto& shard) noexcept {
                tally.note_calling_thread();
                if (shard.size() == 0) return;
                auto const begin = static_cast<std::size_t>(shard.data() - first_row);
                run_l2_rows(inputs, Range{begin, begin + shard.size()}, sink, shard.data());
            });
        ::foundation::linear::drop(std::move(whole));
        return;
    }
    auto region = ::fixy::mint_owned_region(pieces.data(), kPieces, perm::mint_permission_root<PieceTableWhole>());
    auto whole = ::fixy::spawn::mint_parallel_for<::fixy::spawn::BodyCoupling::Independent, kPieces>(
        ctx, w.budget, std::move(region), [&inputs = ::fixy::as_const(w.inputs), &sink, &tally](auto& shard) noexcept {
            tally.note_calling_thread();
            for (const std::uint32_t piece : shard) {
                run_range(inputs, split_range(inputs.units, piece, kPieces), sink, nullptr);
            }
        });
    ::foundation::linear::drop(std::move(whole));
}

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: the forced arm is the naive baseline of one std::jthread for each worker")
namespace {

void run_forced_parallel(Workload& w, Sink& sink, bench::CpuTally& tally) {
    ProductRow* const product = ::foundation::core::foreign_first(w.product.view());
    std::vector<std::jthread> threads(w.forced_workers);
    for (std::size_t worker = 0; worker < w.forced_workers; ++worker) {
        threads[worker] = std::jthread{[&w, &sink, &tally, product, worker](std::stop_token) noexcept {
            tally.note_calling_thread();
            const Range r = split_range(w.inputs.units, worker, w.forced_workers);
            run_range(w.inputs, r, sink, product);
        }};
    }
}

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

[[nodiscard]] Workload make_l1(std::size_t forced_workers) {
    constexpr std::size_t units = (16 * KiB) / sizeof(std::uint64_t);
    auto const alloc = eff::testing::test().alloc;
    return Workload{
        .name = "L1d.array_sum.16KiB",
        .budget =
            fc::WorkBudget{
                .read_bytes = 16 * KiB,
                .write_bytes = 16 * KiB,
                .item_count = units,
            },
        .forced_workers = forced_workers,
        .inputs =
            WorkloadInputs{
                .kind = WorkloadKind::L1ArraySum,
                .units = units,
                .l1 = ::fixy::mint_heap_array_with<std::uint64_t>(
                    alloc, units, [](std::size_t i) noexcept -> std::uint64_t { return i ^ 0x9E3779B9ULL; }),
            },
    };
}

[[nodiscard]] Workload make_l2(std::size_t forced_workers) {
    constexpr std::size_t n = kMatrixOrder;
    auto const alloc = eff::testing::test().alloc;
    return Workload{
        .name = "L2.matrix_multiply.512KB",
        .budget =
            fc::WorkBudget{
                .read_bytes = 2 * n * n * sizeof(double),
                .write_bytes = n * n * sizeof(double),
                .item_count = n * n,
            },
        .forced_workers = forced_workers,
        .inputs =
            WorkloadInputs{
                .kind = WorkloadKind::L2MatrixMultiply,
                .units = n,
                .matrix_a = ::fixy::mint_heap_array_with<double>(
                    alloc, n * n, [](std::size_t i) noexcept { return static_cast<double>((i % 17) + 1) * 0.03125; }),
                .matrix_b = ::fixy::mint_heap_array_with<double>(
                    alloc, n * n, [](std::size_t i) noexcept { return static_cast<double>((i % 29) + 1) * 0.015625; }),
                .matrix_n = n,
            },
        .product = ::fixy::mint_heap_array(alloc, n, ProductRow{}),
    };
}

[[nodiscard]] Workload make_l3(std::size_t forced_workers) {
    constexpr std::size_t bytes = 16 * MiB;
    constexpr std::size_t entries = bytes / sizeof(std::uint32_t);
    static_assert((entries & (entries - 1)) == 0);
    auto const alloc = eff::testing::test().alloc;
    return Workload{
        .name = "L3.graph_traversal.16MiB",
        .budget =
            fc::WorkBudget{
                .read_bytes = bytes,
                .write_bytes = 0,
                .item_count = entries,
            },
        .forced_workers = forced_workers,
        .inputs =
            WorkloadInputs{
                .kind = WorkloadKind::L3GraphTraversal,
                .units = entries,
                .graph_next = ::fixy::mint_heap_array_with<std::uint32_t>(
                    alloc, entries,
                    [](std::size_t i) noexcept { return static_cast<std::uint32_t>((i + 257) & (entries - 1)); }),
            },
    };
}

[[nodiscard]] Workload make_dram(std::size_t forced_workers) {
    constexpr std::size_t bytes = GiB;
    constexpr std::size_t elems = bytes / sizeof(std::uint64_t);
    auto const alloc = eff::testing::test().alloc;
    return Workload{
        .name = "DRAM.stream_fold.1GiB",
        .budget =
            fc::WorkBudget{
                .read_bytes = bytes,
                .write_bytes = 0,
                .item_count = bytes / 64,
            },
        .forced_workers = forced_workers,
        .inputs =
            WorkloadInputs{
                .kind = WorkloadKind::DramStreamFold,
                .units = bytes / 64,
                .dram = ::fixy::mint_heap_array_with<std::uint64_t>(alloc, elems,
                                                                    [](std::size_t i) noexcept -> std::uint64_t {
                                                                        if (i % 8 != 0) return 0;
                                                                        return (i * 0x9E3779B97F4A7C15ULL) ^ (i >> 7);
                                                                    }),
            },
    };
}

// True when the arm starts threads for this workload.  The rule arm starts
// them only for a parallel decision, and the forced arm always does.
[[nodiscard]] bool starts_threads(Strategy strategy, const Workload& w) noexcept {
    switch (strategy) {
        case Strategy::Sequential:
            return false;
        case Strategy::Rule:
            return fc::ParallelismRule::recommend(w.budget).is_parallel();
        case Strategy::ForcedParallel:
            return true;
        default:
            return false;
    }
}

// An arm that starts threads runs under Run::fan_out, so its threads can
// spread over the CPUs that isolcpus did not isolate.  Under the pin to
// one CPU they would all run on that CPU.  An arm that runs on the calling
// thread pins to one CPU, the CPU on which main built the data, so a
// parallel arm is compared against the best sequential time.
[[nodiscard]] bench::Report measure_once(std::string name, Strategy strategy, Workload& w, PieceTable& pieces,
                                         std::size_t samples) {
    bench::Run run{name};
    const bool is_threaded = starts_threads(strategy, w);
    if (is_threaded) {
        (void)run.fan_out();
    } else if (const int core = bench::env_core(); core >= 0) {
        (void)run.core(core);
    }
    bench::CpuTally tally{};
    bench::Report report =
        run.samples(samples).warmup(std::max<std::size_t>(2, samples / 6)).batch(1).max_wall_ms(8000).measure([&] {
            // A new sink for each sample, so the fold of one sample starts at 0.
            Sink sink{};
            switch (strategy) {
                case Strategy::Sequential:
                    run_sequential(w, sink);
                    break;
                case Strategy::Rule:
                    run_rule(w, sink, pieces, tally);
                    break;
                case Strategy::ForcedParallel:
                    run_forced_parallel(w, sink, tally);
                    break;
                default:
                    break;
            }
            bench::do_not_optimize(sink.load_acquire());
        });
    if (is_threaded) tally.print(::fixy::mint_foreign_text(name.data(), name.size()));
    return report;
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

    // The count of the usable CPUs is the count of the process when the
    // library loads, so the placement pin below does not change it.
    const auto& topo = ::fixy::topology::Topology::instance();
    const std::size_t default_forced_workers =
        std::max<std::size_t>(2, std::min<std::size_t>(1024, topo.process_cpu_count() * 32));
    const std::size_t forced_workers = env_usize("CRUCIBLE_NO_REGRESSION_FORCED_WORKERS", default_forced_workers);
    const std::size_t samples = std::max<std::size_t>(10, env_usize("CRUCIBLE_NO_REGRESSION_SAMPLES", 12));

    std::printf("=== parallelism rule no-regression ===\n");
    std::printf("  samples=%zu forced_workers=%zu pieces=%zu\n", samples, forced_workers, kPieces);
    std::printf("  gates: small tiers forced>=sequential/1.05, "
                "rule<=forced*1.05, and inline overhead<=5%% or %.0fns\n",
                kInlineAbsoluteToleranceNs);
    std::printf("         large tiers rule<=sequential/%.1f and "
                "rule<=forced*0.50\n\n",
                kParallelWinFactor);

    PieceTable pieces = make_piece_table();

    // The data is built from the CPU that the sequential arm measures on,
    // so its pages are on the NUMA node of that CPU.  The sequential arm
    // then reads local memory, its best placement.
    bench::Run placement{"no_regression.placement"};
    if (const int core = bench::env_core(); core >= 0) {
        (void)placement.core(core);
    }
    bench::Run::PinResult placement_pin = placement.pin_calling_thread();
    std::printf("  data built on cpu%d\n\n", placement_pin.cpu.raw());

    std::array<Workload, 4> workloads{make_l1(forced_workers), make_l2(forced_workers), make_l3(forced_workers),
                                      make_dram(forced_workers)};

    // The data is in place, so the pin ends.  Each Run pins again, and a
    // fan-out Run starts from the whole affinity of the thread.
    const ::fixy::Result<::fixy::Unit, ::foundation::os::Errno> restored = placement_pin.restore();
    for (::foundation::os::Errno const error : restored.err()) {
        std::fprintf(stderr, "  warning: the placement pin stays (errno %d), so a fan-out arm runs on one CPU\n",
                     ::foundation::os::error_number(error));
    }

    // Three rows for each workload: the sequential arm, the rule arm and the
    // forced arm.
    ::fixy::HeapArray<bench::Report> reports =
        ::fixy::mint_heap_array_with<bench::Report>(::foundation::effects::testing::test().alloc, workloads.size() * 3,
                                                    [](std::size_t) noexcept { return bench::Report{}; });

    int failures = 0;
    int reruns = 0;
    std::printf("=== measuring ===\n");
    for (std::size_t i = 0; i < workloads.size(); ++i) {
        Workload& w = workloads[i];
        Trio trio = run_workload(w, pieces, samples);
        *reports.get(i * 3 + 0).expect("the sequential row of a workload") = std::move(trio.sequential);
        *reports.get(i * 3 + 1).expect("the rule row of a workload") = std::move(trio.rule);
        *reports.get(i * 3 + 2).expect("the forced row of a workload") = std::move(trio.forced);
    }

    bench::emit_reports_text(reports.view());

    std::printf("\n=== no-regression gates ===\n");
    for (std::size_t i = 0; i < workloads.size(); ++i) {
        const bench::Report& seq = *reports.get(i * 3 + 0).expect("the sequential row of a workload");
        const bench::Report& rul = *reports.get(i * 3 + 1).expect("the rule row of a workload");
        const bench::Report& frc = *reports.get(i * 3 + 2).expect("the forced row of a workload");
        const Workload& w = workloads[i];
        const double seq_p50 = seq.pct.p50;
        const double rul_p50 = rul.pct.p50;
        const double frc_p50 = frc.pct.p50;
        const auto dec = fc::ParallelismRule::recommend(w.budget);
        std::printf("  %-26s tier=%u rule=%s factor=%zu "
                    "rule/seq=%.3fx rule/forced=%.3fx\n",
                    w.name, static_cast<unsigned>(dec.tier), dec.is_parallel() ? "parallel" : "sequential", dec.factor,
                    ratio(rul_p50, seq_p50), ratio(rul_p50, frc_p50));

        const bool small = w.inputs.kind == WorkloadKind::L1ArraySum || w.inputs.kind == WorkloadKind::L2MatrixMultiply;
        if (small && dec.is_parallel()) {
            std::printf("  FAIL %-26s expected sequential decision for small tier\n", w.name);
            ++failures;
        }

        // A row with a variation above 5 % asks for a second run of the bench,
        // and its workload gets no gate in this run.  The variation is no
        // failure (the rule of 5 % of CLAUDE.md).
        const bool noisy = seq.noisy(0.05) || rul.noisy(0.05) || frc.noisy(0.05);
        if (noisy) {
            if (seq.noisy(0.05))
                std::printf("  RUN AGAIN %-26s sequential cv=%.1f%% > 5%%\n", w.name, seq.pct.cv * 100.0);
            if (rul.noisy(0.05)) std::printf("  RUN AGAIN %-26s rule cv=%.1f%% > 5%%\n", w.name, rul.pct.cv * 100.0);
            if (frc.noisy(0.05)) std::printf("  RUN AGAIN %-26s forced cv=%.1f%% > 5%%\n", w.name, frc.pct.cv * 100.0);
            ++reruns;
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
        }
        // A sequential decision runs the rule arm inline: for a small tier, for
        // each tier on one CPU, such as on a claim of one core of loop bench,
        // and for a set whose work does not pay for the starts of two threads
        // (THE START COST of fixy/concurrent/ParallelismRule.h).  Its gate is
        // the overhead against the sequential arm.  A parallel decision must
        // beat the two other arms.
        if (!dec.is_parallel()) {
            if (rul_p50 > seq_p50 * kGateTolerance && (rul_p50 - seq_p50) > kInlineAbsoluteToleranceNs) {
                std::printf("  FAIL %-26s rule inline overhead exceeded budget: %.3fx\n", w.name,
                            ratio(rul_p50, seq_p50));
                ++failures;
            }
        } else {
            if (rul_p50 * kParallelWinFactor > seq_p50) {
                std::printf("  FAIL %-26s rule did not beat sequential by %.1fx: %.3fx\n", w.name, kParallelWinFactor,
                            ratio(rul_p50, seq_p50));
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
        std::printf("  PASS - the parallelism rule stayed within the cache-tier gates.  Workloads to run again for a "
                    "variation above 5%%, with no gate: %d.\n",
                    reruns);
    } else {
        std::printf("  FAIL - %d no-regression gate(s) failed.\n", failures);
    }

    bench::emit_reports_json(reports.view(), bench::env_json());
    return failures == 0 ? 0 : 1;
}
