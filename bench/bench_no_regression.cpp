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

#include "bench_harness.h"

#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

namespace fc = ::fixy::concurrent;
namespace eff = ::foundation::effects;

// The permission over the piece table that the rule arm splits into
// shards.  A region scope door mints it.
struct PieceTableWhole {
    using permission_row = eff::Row<>;
    using scope_door = ::fixy::RegionScopeDoor;
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

[[nodiscard]] constexpr ::fixy::TextView strategy_name(Strategy s) noexcept {
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
    const std::size_t safe_count = ::fixy::max(std::size_t{1}, count);
    const std::size_t chunk = (total + safe_count - 1) / safe_count;
    const std::size_t begin = ::fixy::min(total, index * chunk);
    const std::size_t end = ::fixy::min(total, begin + chunk);
    return Range{begin, end};
}

// The decimal value of the variable name of the environment, or fallback
// when the variable is not set, is empty, is not a decimal number as a whole,
// or is zero.
[[nodiscard]] std::size_t env_usize(::fixy::TextView name, std::size_t fallback) noexcept {
    return bench::env_value(name).match(
        [fallback](::fixy::TextView text) noexcept {
            return ::fixy::parse<std::size_t>(text).match(
                [fallback](std::size_t value) noexcept { return value == 0 ? fallback : value; },
                [fallback](::fixy::ParseError) noexcept { return fallback; });
        },
        [fallback]() noexcept { return fallback; });
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
    ::fixy::TextView name{};
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
// the matrix multiply splits into shards.  A region scope door mints it.
struct ProductWhole {
    using permission_row = eff::Row<>;
    using scope_door = ::fixy::RegionScopeDoor;
};

// The piece table that the rule arm splits.  Element i names piece i
// of the work, and a shard runs the pieces of its slice.
using PieceTable = ::fixy::FixedArray<std::uint32_t, kPieces>;

[[nodiscard]] PieceTable make_piece_table() noexcept {
    PieceTable table{};
    for (::fixy::Index<kPieces> const piece : table.indices()) {
        table.set(piece, static_cast<std::uint32_t>(piece.raw()));
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
        out[i - rows.begin] = ::fixy::build_fixed_array<double, ProductRow::size()>(
            [matrix_a, matrix_b, n, i, &acc] [[gnu::always_inline]] (auto run) noexcept {
                double* const row_out = ::foundation::core::foreign_first(run);
                for (std::size_t j = 0; j < n; ++j) {
                    double sum = 0.0;
                    for (std::size_t k = 0; k < n; ++k) {
                        sum += matrix_a[i * n + k] * matrix_b[k * n + j];
                    }
                    row_out[j] = sum;
                    acc += sum;
                }
            });
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

// The units of the workload on the calling thread.  ROWS is the run of the
// rows of the product in the frame of measure_once.
template <typename RowsBrand>
void run_sequential(WorkloadInputs const& inputs, Sink& sink, ::fixy::ViewMut<ProductRow, RowsBrand>& rows) noexcept {
    run_range(inputs, Range{0, inputs.units}, sink, ::foundation::core::foreign_first(rows));
}

// The options of the threads of the rule arm (the cores of a run of
// bench_harness.h).  On a claim of loop bench of at least two cores, the
// measuring thread of a fan-out takes the core of slot 0, and the threads of
// the decision take the first cores of the order of the claim apart from it,
// the quietest first, one core for each thread through the placement
// OneCpuEach.  No load balancer reaches an isolated core, so with no placement
// each thread stays on the core of the measuring thread.  With no claim, as
// under ctest, the threads keep the affinity of the process.
[[nodiscard]] ::fixy::thread::ThreadOptions rule_thread_options(bench::Cores const& cores,
                                                                fc::WorkBudget budget) noexcept {
    if (cores.count() < 2) return ::fixy::thread::ThreadOptions{};
    using CoreCount = ::fixy::Quantity<::fixy::unit::Element, std::size_t>;
    const bench::CpuId measuring = cores.of_slot(bench::kMeasuringSlot).value_or(bench::CpuId::none());
    const crucible::warden::CpuSet threads =
        cores.first_slots_apart(measuring, CoreCount{fc::ParallelismRule::recommend(budget).factor});
    return ::fixy::thread::ThreadOptions{.affinity = bench::detail::mask_of(threads),
                                         .placement = ::fixy::thread::ThreadPlacement::OneCpuEach};
}

// Runs the work through mint_parallel_for.  The mint asks the rule for a
// decision on each call and starts no more threads than the factor of that
// decision.  A sequential decision runs every shard on the calling thread.
// The matrix multiply splits the rows of its product, so each shard writes
// only its own whole rows.  Each other workload splits the piece table, one
// shard for each piece.  Each shard reads the inputs through a const
// reference, and notes its CPU in the tally.  The threads take the cores of
// rule_thread_options.  ROWS is the ViewMut of the rows of the product in the
// frame of measure_once.  A region scope door covers the rows for the matrix
// multiply, and the function gives ROWS back.  A region scope door covers
// the run that the frame door of the piece table lends.
template <typename RowsBrand>
[[nodiscard]] ::fixy::ViewMut<ProductRow, RowsBrand>
run_rule(WorkloadInputs const& inputs, fc::WorkBudget budget, ::fixy::ViewMut<ProductRow, RowsBrand> rows, Sink& sink,
         PieceTable& pieces, bench::CpuTally& tally, bench::Cores const& cores) noexcept {
    const BgCtx ctx{eff::testing::bg()};
    const ::fixy::TestRunnerCtx test{eff::testing::test()};
    const ::fixy::thread::ThreadOptions options = rule_thread_options(cores, budget);
    if (inputs.kind == WorkloadKind::L2MatrixMultiply) {
        return ::fixy::with_region<ProductWhole>(
            std::move(rows), [&ctx, &test, budget, &options, &inputs, &sink, &tally](auto region) noexcept {
                return ::fixy::spawn::mint_parallel_for_with_thread_options<::fixy::spawn::BodyCoupling::Independent,
                                                                            kPieces>(
                    ctx, test, budget, std::move(region),
                    [&inputs, &sink, &tally](auto shard, auto place) noexcept {
                        tally.note_calling_thread();
                        if (shard.size() == 0) return;
                        run_l2_rows(inputs, Range{place.first(), place.first() + shard.size()}, sink,
                                    ::foundation::core::foreign_first(shard));
                    },
                    options);
            });
    }
    auto [table, table_key] =
        ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<std::uint32_t, PieceTable::size()>, pieces);
    table.with_mut(table_key, [&ctx, &test, budget, &options, &inputs, &sink, &tally](auto run) noexcept {
        static_cast<void>(::fixy::with_region<PieceTableWhole>(
            std::move(run), [&ctx, &test, budget, &options, &inputs, &sink, &tally](auto region) noexcept {
                return ::fixy::spawn::mint_parallel_for_with_thread_options<::fixy::spawn::BodyCoupling::Independent,
                                                                            kPieces>(
                    ctx, test, budget, std::move(region),
                    [&inputs, &sink, &tally](auto shard, auto place) noexcept {
                        static_cast<void>(place);
                        tally.note_calling_thread();
                        for (const std::uint32_t piece : shard) {
                            run_range(inputs, split_range(inputs.units, piece, kPieces), sink, nullptr);
                        }
                    },
                    options);
            }));
    });
    return rows;
}

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

// The count of the workers of the forced arm.
struct ForcedWorkersTag final {};
using ForcedWorkers = ::fixy::Id<ForcedWorkersTag, std::size_t>;

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: the forced arm is the naive baseline of one std::jthread for each worker")
namespace {

// On a claim of loop bench of at least two cores, the worker k pins itself to
// the core of the slot 1 + k modulo the count of the cores of the slots from
// 1, as the threads of the rule arm take them.  With no claim each worker
// keeps the affinity of the process.  ROWS is the run of the rows of the
// product in the frame of measure_once.
template <typename RowsBrand>
void run_forced_parallel(WorkloadInputs const& inputs, ForcedWorkers workers,
                         ::fixy::ViewMut<ProductRow, RowsBrand>& rows, Sink& sink, bench::CpuTally& tally,
                         bench::Cores const& cores) {
    const std::size_t forced_workers = workers.raw();
    ProductRow* const product = ::foundation::core::foreign_first(rows);
    const std::size_t worker_cores = cores.count() >= 2 ? cores.count() - 1 : 0;
    std::vector<std::jthread> threads(forced_workers);
    for (std::size_t worker = 0; worker < forced_workers; ++worker) {
        const bench::CpuId core =
            worker_cores == 0 ? bench::CpuId::none()
                              : cores.of_slot(bench::ThreadSlot{static_cast<std::uint32_t>(1 + worker % worker_cores)})
                                    .value_or(bench::CpuId::none());
        threads[worker] =
            std::jthread{[&inputs, &sink, &tally, product, forced_workers, worker, core](std::stop_token) noexcept {
                if (core.is_valid())
                    bench::pin_calling_thread_to(core).expect("the pin of a worker to a core of the claim of its run");
                tally.note_calling_thread();
                const Range r = split_range(inputs.units, worker, forced_workers);
                run_range(inputs, r, sink, product);
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
// one CPU they would all run on that CPU.  On a claim of loop bench, whose
// cores are all isolated, the measuring thread takes the core of slot 0, and
// each thread of the arm takes a core of the slots from 1 (rule_thread_options
// and run_forced_parallel).  An arm that runs on the calling thread pins to
// one CPU, the CPU on which main built the data, so a parallel arm is
// compared against the best sequential time.
[[nodiscard]] bench::Report measure_once(bench::RunName const& name, Strategy strategy, Workload& w, PieceTable& pieces,
                                         std::size_t samples, bench::Cores const& cores) {
    bench::Run run{name};
    const bool is_threaded = starts_threads(strategy, w);
    if (is_threaded) {
        (void)run.fan_out();
    } else if (const int core = bench::env_core(); core >= 0) {
        (void)run.core(core);
    }
    bench::CpuTally tally{};
    WorkloadInputs const& inputs = w.inputs;
    fc::WorkBudget const budget = w.budget;
    ForcedWorkers const workers{w.forced_workers};
    bench::Report report{};
    // One frame of the rows of the product holds the whole measure, outside
    // its timed region.  The threads of an arm write the rows at the same
    // time, but each thread writes only the whole rows of its own range, so
    // no two threads write one row.  The join of the threads at the end of
    // each arm orders their writes before the next sample and the end of the
    // frame.
    w.product = ::fixy::with_mut(std::move(w.product), [&run, &report, &tally, &pieces, &cores, &inputs, budget,
                                                        workers, samples, strategy](auto rows) noexcept {
        report = run.samples(samples)
                     .warmup(::fixy::max(std::size_t{2}, samples / 6))
                     .batch(1)
                     .max_wall_ms(8000)
                     .measure([&] {
                         // A new sink for each sample, so the fold of one sample starts at 0.
                         Sink sink{};
                         switch (strategy) {
                             case Strategy::Sequential:
                                 run_sequential(inputs, sink, rows);
                                 break;
                             case Strategy::Rule:
                                 rows = run_rule(inputs, budget, std::move(rows), sink, pieces, tally, cores);
                                 break;
                             case Strategy::ForcedParallel:
                                 run_forced_parallel(inputs, workers, rows, sink, tally, cores);
                                 break;
                             default:
                                 break;
                         }
                         bench::do_not_optimize(sink.load_acquire());
                     });
    });
    if (is_threaded) tally.print(name.view());
    return report;
}

[[nodiscard]] bench::Report measure_stable(bench::RunName const& name, Strategy strategy, Workload& w,
                                           PieceTable& pieces, std::size_t samples, bench::Cores const& cores) {
    bench::Report best;
    double best_cv = ::fixy::infinity_of<double>;
    for (int attempt = 0; attempt < 3; ++attempt) {
        bench::Report current = measure_once(name, strategy, w, pieces, samples, cores);
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

[[nodiscard]] Trio run_workload(Workload& w, PieceTable& pieces, std::size_t samples, bench::Cores const& cores) {
    auto make_name = [&](Strategy s) { return bench::format_name("no_regression.{}.{}", w.name, strategy_name(s)); };
    return Trio{
        .sequential = measure_stable(make_name(Strategy::Sequential), Strategy::Sequential, w, pieces, samples, cores),
        .rule = measure_stable(make_name(Strategy::Rule), Strategy::Rule, w, pieces, samples, cores),
        .forced =
            measure_stable(make_name(Strategy::ForcedParallel), Strategy::ForcedParallel, w, pieces, samples, cores),
    };
}

[[nodiscard]] double ratio(double lhs, double rhs) noexcept { return rhs > 0.0 ? lhs / rhs : 0.0; }

}  // namespace

int main() {
    // The cores of the claim of loop bench, read before a Run pins the thread
    // (the cores of a run of bench_harness.h).
    const bench::Cores cores = bench::Cores::of_calling_thread();
    bench::print_system_info();
    bench::elevate_priority();

    // The count of the usable CPUs is the count of the process when the
    // library loads, so the placement pin below does not change it.
    const auto& topo = ::fixy::topology::Topology::instance();
    const std::size_t default_forced_workers =
        ::fixy::max(std::size_t{2}, ::fixy::min(std::size_t{1024}, std::size_t{topo.process_cpu_count()} * 32));
    const std::size_t forced_workers = env_usize("CRUCIBLE_NO_REGRESSION_FORCED_WORKERS", default_forced_workers);
    const std::size_t samples = ::fixy::max(std::size_t{10}, env_usize("CRUCIBLE_NO_REGRESSION_SAMPLES", 12));

    using ::fixy::Align;
    using ::fixy::Fixed;
    using ::fixy::Padded;
    using ::fixy::TextView;
    using Name = Padded<26, Align::Left>;
    bench::print("=== parallelism rule no-regression ===\n");
    bench::print("  samples={} forced_workers={} pieces={}\n", samples, forced_workers, kPieces);
    bench::print("  gates: small tiers forced>=sequential/1.05, "
                 "rule<=forced*1.05, and inline overhead<=5% or {}ns\n",
                 Fixed<0>{kInlineAbsoluteToleranceNs});
    bench::print("         large tiers rule<=sequential/{} and "
                 "rule<=forced*0.50\n\n",
                 Fixed<1>{kParallelWinFactor});

    PieceTable pieces = make_piece_table();

    // The data is built from the CPU that the sequential arm measures on,
    // so its pages are on the NUMA node of that CPU.  The sequential arm
    // then reads local memory, its best placement.
    bench::Run placement{"no_regression.placement"};
    if (const int core = bench::env_core(); core >= 0) {
        (void)placement.core(core);
    }
    bench::Run::PinResult placement_pin = placement.pin_calling_thread();
    bench::print("  data built on cpu{}\n\n", placement_pin.cpu.raw());

    constexpr std::size_t kWorkloads = 4;
    ::fixy::FixedArray<Workload, kWorkloads> workloads{make_l1(forced_workers), make_l2(forced_workers),
                                                       make_l3(forced_workers), make_dram(forced_workers)};

    // The data is in place, so the pin ends.  Each Run pins again, and a
    // fan-out Run starts from the whole affinity of the thread.
    const ::fixy::Result<::fixy::Unit, ::foundation::os::Errno> restored = placement_pin.restore();
    for (::foundation::os::Errno const error : restored.err()) {
        ::fixy::report(::fixy::Sink::Err,
                       "  warning: the placement pin stays (errno {}), so a fan-out arm runs on one CPU\n",
                       ::foundation::os::error_number(error));
    }

    // Three rows for each workload: the sequential arm, the rule arm and the
    // forced arm.
    ::fixy::HeapArray<bench::Report> reports =
        ::fixy::mint_heap_array_with<bench::Report>(::foundation::effects::testing::test().alloc, workloads.size() * 3,
                                                    [](std::size_t) noexcept { return bench::Report{}; });

    int failures = 0;
    int reruns = 0;
    bench::print("=== measuring ===\n");
    // The frame of each workload holds its three measures, and a frame of the
    // rows inside it writes the three rows of the workload.
    for (::fixy::Index<kWorkloads> const workload : workloads.indices()) {
        const std::size_t i = workload.raw();
        Workload w = workloads.exchange(workload, Workload{});
        Trio trio = run_workload(w, pieces, samples, cores);
        workloads.set(workload, std::move(w));
        reports = ::fixy::with_mut(std::move(reports), [&trio, i](auto rows) noexcept {
            auto sequential_row = rows.get_mut(i * 3 + 0).expect("the sequential row of a workload");
            *sequential_row = std::move(trio.sequential);
            auto rule_row = rows.get_mut(i * 3 + 1).expect("the rule row of a workload");
            *rule_row = std::move(trio.rule);
            auto forced_row = rows.get_mut(i * 3 + 2).expect("the forced row of a workload");
            *forced_row = std::move(trio.forced);
        });
    }

    bench::emit_reports_text(reports.view());

    bench::print("\n=== no-regression gates ===\n");
    for (::fixy::Index<kWorkloads> const workload : workloads.indices()) {
        const std::size_t i = workload.raw();
        const bench::Report& seq = *reports.get(i * 3 + 0).expect("the sequential row of a workload");
        const bench::Report& rul = *reports.get(i * 3 + 1).expect("the rule row of a workload");
        const bench::Report& frc = *reports.get(i * 3 + 2).expect("the forced row of a workload");
        const Workload& w = workloads.at(workload);
        const double seq_p50 = seq.pct.p50;
        const double rul_p50 = rul.pct.p50;
        const double frc_p50 = frc.pct.p50;
        const auto dec = fc::ParallelismRule::recommend(w.budget);
        bench::print("  {} tier={} rule={} factor={} "
                     "rule/seq={}x rule/forced={}x\n",
                     Name{w.name}, static_cast<unsigned>(dec.tier),
                     dec.is_parallel() ? TextView{"parallel"} : TextView{"sequential"}, dec.factor,
                     Fixed<3>{ratio(rul_p50, seq_p50)}, Fixed<3>{ratio(rul_p50, frc_p50)});

        const bool small = w.inputs.kind == WorkloadKind::L1ArraySum || w.inputs.kind == WorkloadKind::L2MatrixMultiply;
        if (small && dec.is_parallel()) {
            bench::print("  FAIL {} expected sequential decision for small tier\n", Name{w.name});
            ++failures;
        }

        // A row with a variation above 5 % asks for a second run of the bench,
        // and its workload gets no gate in this run.  The variation is no
        // failure (the rule of 5 % of CLAUDE.md).
        const bool noisy = seq.noisy(0.05) || rul.noisy(0.05) || frc.noisy(0.05);
        if (noisy) {
            if (seq.noisy(0.05))
                bench::print("  RUN AGAIN {} sequential cv={}% > 5%\n", Name{w.name}, Fixed<1>{seq.pct.cv * 100.0});
            if (rul.noisy(0.05))
                bench::print("  RUN AGAIN {} rule cv={}% > 5%\n", Name{w.name}, Fixed<1>{rul.pct.cv * 100.0});
            if (frc.noisy(0.05))
                bench::print("  RUN AGAIN {} forced cv={}% > 5%\n", Name{w.name}, Fixed<1>{frc.pct.cv * 100.0});
            ++reruns;
            continue;
        }

        if (small) {
            if (frc_p50 * kGateTolerance < seq_p50) {
                bench::print("  FAIL {} forced parallel beat sequential by >5%: {}x\n", Name{w.name},
                             Fixed<3>{ratio(frc_p50, seq_p50)});
                ++failures;
            }
            if (rul_p50 > frc_p50 * kGateTolerance) {
                bench::print("  FAIL {} rule slower than forced by >5%: {}x\n", Name{w.name},
                             Fixed<3>{ratio(rul_p50, frc_p50)});
                ++failures;
            }
        }
        // A sequential decision runs the rule arm inline: for a small tier, for
        // each tier on one CPU, such as on a claim of one core of loop bench,
        // and for a set whose work does not pay for the starts of two threads
        // (the start cost of fixy/concurrent/ParallelismRule.h).  Its gate is
        // the overhead against the sequential arm.  A parallel decision must
        // beat the two other arms.
        if (!dec.is_parallel()) {
            if (rul_p50 > seq_p50 * kGateTolerance && (rul_p50 - seq_p50) > kInlineAbsoluteToleranceNs) {
                bench::print("  FAIL {} rule inline overhead exceeded budget: {}x\n", Name{w.name},
                             Fixed<3>{ratio(rul_p50, seq_p50)});
                ++failures;
            }
        } else {
            if (rul_p50 * kParallelWinFactor > seq_p50) {
                bench::print("  FAIL {} rule did not beat sequential by {}x: {}x\n", Name{w.name},
                             Fixed<1>{kParallelWinFactor}, Fixed<3>{ratio(rul_p50, seq_p50)});
                ++failures;
            }
            if (rul_p50 > frc_p50 * 0.50) {
                bench::print("  FAIL {} rule did not beat forced by 2x: {}x\n", Name{w.name},
                             Fixed<3>{ratio(rul_p50, frc_p50)});
                ++failures;
            }
        }
    }

    bench::print("\n=== verdict ===\n");
    if (failures == 0) {
        bench::print("  PASS - the parallelism rule stayed within the cache-tier gates.  Workloads to run again for "
                     "a variation above 5%, with no gate: {}.\n",
                     reruns);
    } else {
        bench::print("  FAIL - {} no-regression gate(s) failed.\n", failures);
    }

    bench::emit_reports_json(reports.view(), bench::env_json());
    return failures == 0 ? 0 : 1;
}
