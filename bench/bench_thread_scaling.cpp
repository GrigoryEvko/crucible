// The scaling rows of three primitives of the base, from one thread to many.
//
// THE OPS
//   shared add  fetch_add_acq_rel of foundation/core/Atomic.h on one cell that
//               each thread of the row writes
//   own add     the same add on a cell of each thread, each cell on a cache
//               line of its own: the control with no shared line
//   mpsc push   try_push of fixy/concurrent/MpscRing.h from each thread of the
//               row but the last, and try_pop on the last thread, the one
//               consumer.  A push into a full ring waits for the consumer
//
// THE THREADS OF A ROW
//   A row of N threads runs the op on N threads at the same time, one thread
//   on the CPU of each slot 0 to N - 1 of the spread of the run (THE SPREAD
//   OF A RUN of bench/bench_harness.h).  Slot 0 is the main thread.  Each
//   other slot is a worker of mint_owned_thread of fixy/os/OwnedThread.h,
//   with the CPU of its slot as its affinity.
//
// THE PHASES OF A ROW
//   1. The window: each thread runs the op and counts it for kWindowNs.  The
//      row gives the ops of the producers of all threads in one second, and
//      the efficiency: that throughput over the throughput of one thread
//      times the count of the threads.  For mpsc push, the base is the row of
//      one producer and the consumer, times the count of the producers.  The
//      pause check of the harness reads the threads over the window.
//      The window is bench::kWindows consecutive windows of equal time, one
//      phase each (THE WINDOWS of bench/bench_harness.h).  Each thread counts
//      its ops in each window, and the row gives the median and the spread of
//      the throughput of the windows.  Each window has the pause check of the
//      time that the main thread lost in it.
//   2. The latency: the workers go on, and the main thread runs the op under
//      Run::measure, one op in each sample.  Its report gives p50, p99 and
//      the maximum of one op.  The harness takes the cost of the clock off
//      each sample, so an op of a few nanoseconds reads at the resolution of
//      the clock.  The JSON tail of the bench gives the latency report of
//      each row.
//
// THE COUNTS OF THREADS
//   With no argument, the rows have 1, 2, 4, 8 and 16 threads, up to the
//   count of the slots.  The bench states eight cores, so loop bench gives it
//   one L3 group of bench.slice.  Each argument is one more count of threads
//   after the rows of 1 and 2 threads, which are the bases of the
//   efficiency.  A count above the slots or above kMaxThreads ends the run.
//
// THE COMMANDS
//   1 to 8 threads, one L3 group:   python3 utils/bin/loop bench bench_thread_scaling
//   16 threads, two L3 groups:      python3 utils/bin/loop bench --cores 16 bench_thread_scaling
//   32 threads, the shared CPUs:    python3 utils/bin/loop heavy -j 32 -- build-bench/bench/bench_thread_scaling 32
//   64 to 224 threads, on a host with no other work:
//                                   build-bench/bench/bench_thread_scaling 64 128 224
//   A run with no claim puts its threads on CPUs that other work can use,
//   so each row of that run is noisy.  The pause check reads at most
//   kPauseThreads threads of the harness.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/MpscRing.h>
#include <fixy/os/Args.h>
#include <fixy/os/OwnedThread.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Quarantine.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <utility>

#include "bench_harness.h"

namespace {

namespace eff = ::foundation::effects;

// The context that starts the workers: Bg for the threads, Alloc for their frames.
using Starter = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;
using Item = std::uint64_t;

// The threads of one row, at most.  The command of the owner runs 224.
constexpr std::size_t kMaxThreads = 256;
// The rows of one op, at most, and the latency reports of the three ops.
constexpr std::size_t kMaxRows = 16;
constexpr std::size_t kMaxReports = 3 * kMaxRows;
constexpr std::uint64_t kWindowNs = 500000000;
constexpr std::size_t kLatencySamples = 1000000;
constexpr std::size_t kLatencyWarmup = 10000;
constexpr std::size_t kLatencyMs = 1000;
// The ops of a worker between two reads of the phase, and of the main
// thread between two reads of the clock.
constexpr std::uint64_t kOpsPerCheck = 64;
constexpr std::uint64_t kOpsPerClock = 1024;
constexpr std::size_t kRingCapacity = 4096;
enum class OpKind : std::uint8_t {
    SharedAdd,
    OwnAdd,
    MpscPush,
};

// The phase of a row (THE PHASES OF A ROW).  The main thread writes it.  The
// window has one phase for each of its windows, so a worker reads the window
// in the one cell that it reads for the phase.
enum class Phase : std::uint32_t {
    Wait,
    Window0,
    Window1,
    Window2,
    Window3,
    Window4,
    Latency,
    Stop,
};
static_assert(static_cast<std::uint32_t>(Phase::Latency) - static_cast<std::uint32_t>(Phase::Window0)
                  == bench::kWindows,
              "the window of a row has one phase for each window of the harness");

using WindowIndex = ::fixy::Index<bench::kWindows>;

// The window of a phase, or none for a phase outside the window.
[[nodiscard]] ::fixy::Option<WindowIndex> window_of(Phase phase) noexcept {
    std::uint32_t const raw = static_cast<std::uint32_t>(phase);
    std::uint32_t const first = static_cast<std::uint32_t>(Phase::Window0);
    if (raw < first) return ::fixy::none;
    return WindowIndex::check(raw - first);
}

// The phase of a window.
[[nodiscard]] Phase phase_of(WindowIndex window) noexcept {
    return static_cast<Phase>(static_cast<std::uint32_t>(Phase::Window0) + window.raw());
}

// The role of the thread of a slot in a row.
enum class Role : std::uint8_t {
    Producer,
    Consumer,
};

struct ThreadCountTag final {};
using ThreadCount = ::fixy::Id<ThreadCountTag, std::uint32_t>;

// The line that one thread writes: its own cell, its ops in each window, and
// whether it published them.
struct alignas(64) ThreadLine {
    ::fixy::Atomic<std::uint64_t> cell{};
    ::fixy::FixedArray<::fixy::Atomic<std::uint64_t>, bench::kWindows> window_ops{};
    ::fixy::Atomic<bool> is_published{false};
};

struct alignas(64) PhaseLine {
    ::fixy::Atomic<Phase> phase{Phase::Wait};
};

struct alignas(64) CountLine {
    ::fixy::Atomic<std::uint64_t> value{};
};

// The state that the threads of one row share.  The main thread owns it, and
// it outlives each worker of the row.
struct RowState {
    PhaseLine phase{};
    // The workers that wait for the window.
    CountLine ready{};
    CountLine shared{};
    ::fixy::FixedArray<ThreadLine, kMaxThreads> lines{};
    ::fixy::concurrent::MpscRing<Item, kRingCapacity> ring{};
};

[[nodiscard]] Phase read_phase(RowState const& state) noexcept { return state.phase.phase.load_acquire(); }

[[nodiscard]] ThreadLine& line_of(RowState& state, bench::ThreadSlot slot) noexcept {
    return *state.lines.get(slot.raw()).expect("a slot of a row is below kMaxThreads");
}

// One op of a producer.  A push into a full ring waits, and it stops when the
// row stops, so no producer waits for a consumer that ended.
template <OpKind Kind>
[[gnu::always_inline]] inline void run_op(RowState& state, ThreadLine& line) noexcept {
    if constexpr (Kind == OpKind::SharedAdd) {
        static_cast<void>(state.shared.value.fetch_add_acq_rel(1));
    } else if constexpr (Kind == OpKind::OwnAdd) {
        static_cast<void>(line.cell.fetch_add_acq_rel(1));
    } else {
        while (!state.ring.try_push(Item{1})) {
            if (read_phase(state) == Phase::Stop) return;
            CRUCIBLE_SPIN_PAUSE;
        }
    }
}

// The loop of a producer worker: the ops of each window, which it publishes,
// then the ops of the latency phase until the stop.  A run of kOpsPerCheck
// ops counts in the window of the phase that the worker read before it.
template <OpKind Kind>
void run_producer(RowState& state, ThreadLine& line) noexcept {
    static_cast<void>(state.ready.value.fetch_add_acq_rel(1));
    while (read_phase(state) == Phase::Wait)
        CRUCIBLE_SPIN_PAUSE;
    ::fixy::FixedArray<std::uint64_t, bench::kWindows> ops{};
    for (::fixy::Option<WindowIndex> window = window_of(read_phase(state)); window.is_some();
         window = window_of(read_phase(state))) {
        for (std::uint64_t op = 0; op < kOpsPerCheck; ++op)
            run_op<Kind>(state, line);
        for (WindowIndex const index : window)
            ops.at(index) += kOpsPerCheck;
    }
    for (WindowIndex const index : ops.indices())
        line.window_ops.at(index).store_release(ops.at(index));
    line.is_published.store_release(true);
    while (read_phase(state) != Phase::Stop) {
        for (std::uint64_t op = 0; op < kOpsPerCheck; ++op)
            run_op<Kind>(state, line);
    }
}

// The loop of the consumer of mpsc push: it pops until the stop, and it
// counts no op of the window.
void run_consumer(RowState& state, ThreadLine& line) noexcept {
    // A pop of an MPSC ring is a merge of the producers, so it takes Nondet.
    eff::cap::Nondet const merge = eff::testing::test().nondet;
    line.is_published.store_release(true);
    static_cast<void>(state.ready.value.fetch_add_acq_rel(1));
    while (read_phase(state) != Phase::Stop) {
        for (std::uint64_t pop = 0; pop < kOpsPerCheck; ++pop) {
            if (state.ring.try_pop(merge).is_none()) CRUCIBLE_SPIN_PAUSE;
        }
    }
}

// What the window of the main thread measured: its ops, its wall time and the
// time that it lost in each window, and the pause of the whole window.
struct Window {
    ::fixy::FixedArray<std::uint64_t, bench::kWindows> ops{};
    ::fixy::FixedArray<std::uint64_t, bench::kWindows> wall_ns{};
    ::fixy::FixedArray<std::uint64_t, bench::kWindows> lost_ns{};
    bench::Pause pause{};
};

// The window of the main thread on the CPU of slot 0 (THE PHASES OF A ROW).
// It starts each window, runs the op for its part of kWindowNs, and then
// starts the latency phase.  The check of each window reads two clocks and no
// file of procfs, so it does not change the basis of the pause of the whole
// window (THE WINDOWS of bench/bench_harness.h).
template <OpKind Kind>
[[nodiscard]] Window run_window(RowState& state, ThreadLine& line, bench::CpuId cpu) noexcept {
    bench::CoreHold const hold{cpu};
    ::fixy::InitLoadCtx const startup{eff::testing::init()};
    eff::cap::Nondet const nondet = eff::testing::test().nondet;
    auto const clock = ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::Monotonic>(startup);
    auto const now = [&clock, &nondet]() noexcept {
        return clock.read(nondet).expect("a read of the monotonic clock").peek();
    };
    bench::PauseReading const before = bench::detail::read_pause();
    Window window{};
    bench::WindowClock window_clock = bench::detail::read_window_clock();
    std::uint64_t start = now();
    for (WindowIndex const index : ::fixy::IndexRange<bench::kWindows>{}) {
        state.phase.phase.store_release(phase_of(index));
        std::uint64_t end = start;
        std::uint64_t ops = 0;
        while (end - start < kWindowNs / bench::kWindows) {
            for (std::uint64_t op = 0; op < kOpsPerClock; ++op)
                run_op<Kind>(state, line);
            ops += kOpsPerClock;
            end = now();
        }
        bench::WindowClock const window_end = bench::detail::read_window_clock();
        window.ops.at(index) = ops;
        window.wall_ns.at(index) = end - start;
        window.lost_ns.at(index) =
            bench::detail::lost_between(::fixy::side_as<bench::side::ReadingBefore>(window_clock),
                                        ::fixy::side_as<bench::side::ReadingAfter>(window_end));
        window_clock = window_end;
        start = end;
    }
    state.phase.phase.store_release(Phase::Latency);
    bench::PauseReading const after = bench::detail::read_pause();
    window.pause = bench::detail::pause_between(::fixy::side_as<bench::side::ReadingBefore>(before),
                                                ::fixy::side_as<bench::side::ReadingAfter>(after));
    return window;
}

// The result of one row.
struct RowResult {
    ThreadCount threads{1};
    // The ops of the producers in one second, in hundredths of a million.
    std::uint64_t centi_mops = 0;
    // The throughput of the producers in each window, in millions of ops in
    // one second, summed up by its median and its spread, and the count of
    // the windows in which the main thread lost more than the bound of the
    // pause check (THE WINDOWS of bench/bench_harness.h).
    bench::WindowSpread windows{};
    std::size_t paused_windows = 0;
    bench::Pause window_pause{};
    bench::Report latency{};
};

// The literal name of the latency report of an op.
template <OpKind Kind>
[[nodiscard]] bench::Run run_of() {
    if constexpr (Kind == OpKind::SharedAdd) {
        return bench::Run{"shared add [latency of one op]"};
    } else if constexpr (Kind == OpKind::OwnAdd) {
        return bench::Run{"own add [latency of one op]"};
    } else {
        return bench::Run{"mpsc push [latency of one op]"};
    }
}

// Runs one row of an op on the first threads.count slots of the spread.
template <OpKind Kind>
[[nodiscard]] RowResult run_row(bench::Spread const& spread, ThreadCount threads) {
    RowState state{};
    auto const cpu_of = [&spread](bench::ThreadSlot slot) noexcept {
        return spread.of_slot(slot).expect("a row has at most the slots of the spread");
    };
    std::uint32_t const consumer_slot = Kind == OpKind::MpscPush ? threads.raw() - 1 : threads.raw();
    ::fixy::FixedList<::fixy::thread::OwnedThread, kMaxThreads> workers{};
    for (std::uint32_t slot = 1; slot < threads.raw(); ++slot) {
        Role const role = slot == consumer_slot ? Role::Consumer : Role::Producer;
        ThreadLine& line = line_of(state, bench::ThreadSlot{slot});
        ::fixy::thread::ThreadOptions const options{
            .name = "scaling-worker",
            .affinity = ::fixy::AffinityMask::single(static_cast<std::uint16_t>(cpu_of(bench::ThreadSlot{slot}).raw())),
        };
        ::fixy::thread::OwnedThread worker =
            ::fixy::thread::mint_owned_thread(Starter{eff::testing::bg()}, options, [&state, &line, role]() noexcept {
                if (role == Role::Consumer) {
                    run_consumer(state, line);
                } else {
                    run_producer<Kind>(state, line);
                }
            }).expect("the thread door starts a worker of a row");
        static_cast<void>(workers.push(std::move(worker)).expect("a row has at most kMaxThreads threads"));
    }
    while (state.ready.value.load_acquire() + 1 < threads.raw())
        CRUCIBLE_SPIN_PAUSE;

    ThreadLine& own = line_of(state, bench::kMeasuringSlot);
    bench::CpuId const main_cpu = cpu_of(bench::kMeasuringSlot);
    Window const window = run_window<Kind>(state, own, main_cpu);
    ::fixy::FixedArray<std::uint64_t, bench::kWindows> ops = window.ops;
    for (std::uint32_t slot = 1; slot < threads.raw(); ++slot) {
        ThreadLine const& line = line_of(state, bench::ThreadSlot{slot});
        while (!line.is_published.load_acquire())
            CRUCIBLE_SPIN_PAUSE;
        for (WindowIndex const index : ops.indices())
            ops.at(index) += line.window_ops.at(index).load_acquire();
    }

    std::uint64_t all_ops = 0;
    std::uint64_t all_wall_ns = 0;
    std::size_t paused_windows = 0;
    ::fixy::FixedArray<double, bench::kWindows> window_mops{};
    for (WindowIndex const index : ops.indices()) {
        std::uint64_t const wall_ns = window.wall_ns.at(index);
        all_ops += ops.at(index);
        all_wall_ns += wall_ns;
        window_mops.at(index) =
            wall_ns == 0 ? 0.0 : static_cast<double>(ops.at(index)) * 1000.0 / static_cast<double>(wall_ns);
        paused_windows += window.lost_ns.at(index) > bench::kPauseBoundNs ? std::size_t{1} : std::size_t{0};
    }

    RowResult result{.threads = threads,
                     .centi_mops = all_wall_ns == 0 ? 0 : all_ops * 100000 / all_wall_ns,
                     .windows = bench::spread_of_windows(window_mops.view()),
                     .paused_windows = paused_windows,
                     .window_pause = window.pause};
    result.latency = run_of<Kind>()
                         .core(main_cpu.raw())
                         .batch(1)
                         .samples(kLatencySamples)
                         .warmup(kLatencyWarmup)
                         .max_wall_ms(kLatencyMs)
                         .measure([&state, &own] { run_op<Kind>(state, own); });
    if (threads.raw() <= 16) bench::print_thread_cpus("the row");
    state.phase.phase.store_release(Phase::Stop);
    for (::fixy::thread::OwnedThread& worker : workers) {
        std::move(worker).join();
    }
    return result;
}

// The producers of a row: each thread, but the consumer of mpsc push.
template <OpKind Kind>
[[nodiscard]] std::uint64_t producers_of(ThreadCount threads) noexcept {
    return Kind == OpKind::MpscPush ? threads.raw() - 1 : threads.raw();
}

// The row that the efficiency of each row of an op compares with: its
// producers and its throughput in hundredths of a million ops in a second.
struct EfficiencyBase {
    std::uint64_t producers = 1;
    std::uint64_t centi_mops = 0;
};

// Prints one row: its threads, its throughput, its efficiency over the base
// times the ratio of the producers, the median and the spread of the
// throughput of its windows, its pause, and the report of its latency.  The
// ratios print with two decimals, and the spread with one.  A pause of the
// whole window or of one window over the bound makes the row not valid.
template <OpKind Kind>
void print_row(RowResult const& row, EfficiencyBase const& base) noexcept {
    std::uint64_t const ideal = base.centi_mops * producers_of<Kind>(row.threads) / base.producers;
    std::uint64_t const efficiency = ideal == 0 ? 0 : row.centi_mops * 100 / ideal;
    auto const median = static_cast<std::uint64_t>(row.windows.median * 100.0 + 0.5);
    auto const spread = static_cast<std::uint64_t>(row.windows.spread * 1000.0 + 0.5);
    bool const is_paused = row.window_pause.is_over_bound() || row.paused_windows > 0;
    // The rows of the harness go through stdio, and report() does not.
    bench::detail::flush_standard_output();
    ::fixy::report(::fixy::Sink::Out, "  {} threads: {}.{}{} Mops/s, efficiency {}.{}{}", row.threads.raw(),
                   row.centi_mops / 100, row.centi_mops % 100 / 10, row.centi_mops % 10, efficiency / 100,
                   efficiency % 100 / 10, efficiency % 10);
    ::fixy::report(::fixy::Sink::Out, ", {} windows: median {}.{}{} Mops/s, spread {}.{}%", row.windows.windows,
                   median / 100, median % 100 / 10, median % 10, spread / 10, spread % 10);
    ::fixy::report(::fixy::Sink::Out, ", window pause {} us, other thread delay {} us, {} paused windows{}\n",
                   row.window_pause.own_ns() / 1000, row.window_pause.other.run_delay_ns / 1000, row.paused_windows,
                   is_paused ? ::fixy::TextView{"  NOT VALID"} : ::fixy::TextView{});
    row.latency.print_text();
}

// The rows of one op, then the line of each row.  The first row is the base
// of the efficiency.  The latency report of each row goes to reports, for the
// JSON tail of the bench.
template <OpKind Kind>
void run_op_rows(bench::Spread const& spread, ::fixy::View<ThreadCount const> counts, ::fixy::TextView title,
                 ::fixy::FixedList<bench::Report, kMaxReports>& reports) {
    bench::detail::flush_standard_output();
    ::fixy::report(::fixy::Sink::Out, "\n=== {} ===\n", title);
    ::fixy::FixedList<RowResult, kMaxRows> rows{};
    for (ThreadCount const threads : counts) {
        // A row of mpsc push needs a producer beside its consumer.
        if (producers_of<Kind>(threads) == 0) continue;
        static_cast<void>(rows.push(run_row<Kind>(spread, threads)).expect("an op has at most kMaxRows rows"));
    }
    if (rows.size() == 0) return;
    RowResult const& first = *rows.get(0).expect("the test above found a row");
    EfficiencyBase const base{.producers = producers_of<Kind>(first.threads), .centi_mops = first.centi_mops};
    for (RowResult const& row : rows) {
        print_row<Kind>(row, base);
    }
    for (RowResult& row : rows) {
        bench::add_report(reports, std::move(row.latency));
    }
}

// Reads the counts of the rows (THE COUNTS OF THREADS).
[[nodiscard]] ::fixy::FixedList<ThreadCount, kMaxRows> counts_of(::fixy::os::Args const& arguments,
                                                                 bench::Spread const& spread) noexcept {
    ::fixy::FixedList<ThreadCount, kMaxRows> counts{};
    auto const add = [&counts, &spread](ThreadCount threads) noexcept {
        if (threads.raw() > spread.count() || threads.raw() > kMaxThreads) {
            ::fixy::fatal("a row of {} threads is larger than the {} slots of the run, or than {}", threads.raw(),
                          spread.count(), kMaxThreads);
        }
        static_cast<void>(counts.push(threads).expect("a run has at most kMaxRows counts"));
    };
    add(ThreadCount{1});
    if (spread.count() >= 2) add(ThreadCount{2});
    if (arguments.size() <= 1) {
        for (std::uint32_t threads = 4; threads <= 16 && threads <= spread.count(); threads *= 2)
            add(ThreadCount{threads});
        return counts;
    }
    bool is_program = true;
    for (::fixy::CText const argument : arguments) {
        if (is_program) {
            is_program = false;
            continue;
        }
        std::uint32_t const threads =
            ::fixy::parse<std::uint32_t>(argument.view())
                .match([](std::uint32_t parsed) noexcept { return parsed; },
                       [&argument](::fixy::ParseError) noexcept -> std::uint32_t {
                           ::fixy::fatal("the argument '{}' is not a count of threads", argument.view());
                       });
        if (threads > 2) add(ThreadCount{threads});
    }
    return counts;
}

// The bench with the arguments of main (THE COUNTS OF THREADS).
int run_bench(::fixy::os::Args const& arguments) {
    bench::print_system_info();
    bench::elevate_priority();
    bench::Spread const spread = bench::Spread::of_calling_thread();
    ::fixy::FixedList<ThreadCount, kMaxRows> const counts = counts_of(arguments, spread);
    bench::detail::flush_standard_output();
    if (spread.source() == bench::SpreadSource::Claim) {
        ::fixy::report(::fixy::Sink::Out, "  slots: the {} claimed cores, slot k on core k, from cpu{}\n",
                       spread.count(), spread.of_slot(bench::kMeasuringSlot).value_or(bench::CpuId::none()).raw());
    } else {
        ::fixy::report(::fixy::Sink::Out,
                       "  NOTE: no claimed core.  The {} slots take one CPU of each core of the affinity, then the "
                       "SMT siblings, and other work can use them, so each row is noisy\n",
                       spread.count());
    }
    for (ThreadCount const threads : counts) {
        if (threads.raw() > bench::kPauseThreads) {
            ::fixy::report(::fixy::Sink::Out,
                           "  NOTE: a row of {} threads has more threads than the {} that the pause check reads\n",
                           threads.raw(), bench::kPauseThreads);
        }
    }
    ::fixy::FixedList<bench::Report, kMaxReports> reports{};
    run_op_rows<OpKind::SharedAdd>(spread, counts.view(), "shared add: one cell for all threads", reports);
    run_op_rows<OpKind::OwnAdd>(spread, counts.view(), "own add: one cell on its own line for each thread", reports);
    run_op_rows<OpKind::MpscPush>(spread, counts.view(), "mpsc push: the last thread is the one consumer", reports);
    bench::emit_reports_json(reports.view(), bench::env_json());
    return 0;
}

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: main takes the arguments of the C runtime as a count and an array of texts")
int main(int argc, char** argv) { return run_bench(::fixy::os::args(argc, argv)); }
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
