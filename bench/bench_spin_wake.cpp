// The wake latency of each spin wait of the base, and the cost of a spin for
// the writer of its cache line, with the ratio of each row to its reference.
//
// THE WAKE
//   A waiter on the core of slot 0 waits with one form until a word holds
//   the number of its round.  A writer on the core of slot 1 waits until the
//   waiter starts the round, spins for a delay of about 1 to 3 microseconds,
//   reads the counter of the core, stores the reading into a stamp, and then
//   stores the round into the word.  The waiter reads the counter when its
//   wait ends.  The difference of the two readings is the wake latency of the
//   round.  Each row gives p50, p99 and the maximum of kSamples rounds, three
//   times, and the spread of the three medians.  The counter of an AMD Zen
//   core moves in steps of 10 ns, so each row also gives the mean of the
//   samples from p1 to p99, which resolves less than one step.
//
//   The reference of each row is a loop of an acquire load and
//   CRUCIBLE_SPIN_PAUSE, the state of the art of a spin with no budget.  The
//   ratio is the median of the three medians of the row over that of the
//   reference, in thousandths, and the same ratio of the means.  The
//   reference runs again at the end, and its ratio to the first run gives
//   the drift of the host.
//
// THE PARK
//   A row of a park waits with an event count of fixy/concurrent/EventCount.h,
//   and its writer notifies the count after each store.  The delay of the
//   writer is about 18 to 53 microseconds, so a waiter with the budget 0 sleeps
//   in the kernel before the store.  The reference is a futex wait on the word
//   itself through the futex door, and a wake of one waiter after each store,
//   the least work of a park on Linux.  A second row spins for 1 ms before its
//   park, so it never parks, and its reference is the plain pause loop.
//
// THE NOTIFY
//   A notify of an event count with no waiter is a fence and a load.  The
//   reference is the notify of folly, a fetch-add of the epoch and a test of
//   the bits of the waiters, and the signal of the park of the pipeline
//   before the event count, an exchange of a word.  bench::run gives the
//   percentiles of one call.
//
// THE WRITER
//   The writer adds to a second word in the cache line of the word of the
//   wait, while a reader waits on that word with one arm of the spin-hint
//   door.  A reader that reads the line often takes the line from the writer.
//   bench::run gives the percentiles of one add.  The reference is the same
//   add with no reader.
//
// THE RATE
//   The bench prints the ticks of one second at the rate of the spin budget,
//   and the ticks of one second of the calibration of the harness, which
//   reads the clock for 200 milliseconds.  The difference is the error of the
//   rate of the budget, in parts per million.
//
// THE CORES
//   The bench states CORES 2 in bench/CMakeLists.txt.  loop bench gives it two
//   isolated cores, and each thread pins itself to the core of its slot.  A run
//   with no claim, such as a run under ctest, pins no thread.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/EventCount.h>
#include <fixy/os/Futex.h>
#include <fixy/os/SpinBudget.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Platform.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Sort.h>
#include <foundation/effects/Ctx.h>
#include <foundation/os/SpinHint.h>

#include <cstddef>
#include <cstdint>
#include <utility>

#include "bench_harness.h"

namespace {

namespace cc = ::fixy::concurrent;
namespace core = ::foundation::core;
namespace eff = ::foundation::effects;
namespace os = ::foundation::os;
namespace spin = ::fixy::time;

using core::HeapArray;

constexpr eff::Alloc kAlloc = eff::testing::test().alloc;

// The rounds of one measurement of a row.
constexpr std::size_t kSamples = 20000;

// The measurements of each row.
constexpr std::size_t kRepeats = 3;

// A budget that no round of a spin row reaches: one second.
constexpr spin::SpinBudget kLongBudget{1000000000U};

// A deadline of a word arm that no round reaches: 2^40 ticks.
constexpr std::uint64_t kFarTicks = std::uint64_t{1} << 40;

// Each arm of the spin-hint door.  A row runs only for an arm of the core.
constexpr ::fixy::FixedArray<os::SpinArm, 8> kEachArm{
    os::SpinArm::pause, os::SpinArm::tpause, os::SpinArm::umwait, os::SpinArm::mwaitx,
    os::SpinArm::yield, os::SpinArm::isb,    os::SpinArm::wfe,    os::SpinArm::wfet,
};

struct RoundTag final {};

// The number of one round of a wake row.  Round 0 is the state before the
// first round.
using Round = ::fixy::Id<RoundTag, std::uint32_t>;

struct PermilleTag final {};

// A fraction of the sorted samples of a row, in thousandths: 500 for p50.
using Permille = ::fixy::Id<PermilleTag, std::uint32_t>;

// The word of a wait, and a second word in its cache line.
struct alignas(64) Line {
    core::Atomic<std::uint32_t> word{0};
    core::Atomic<std::uint32_t> neighbor{0};
};

// The handshake of a round: the round that the waiter starts, and the
// counter of the writer at its store.
struct alignas(64) Handshake {
    core::Atomic<std::uint32_t> ready{0};
    core::Atomic<std::uint64_t> stamp{0};
};

// The cores of the two slots of a run, or no core for each slot of a run
// with no claim.
struct RowCores {
    ::fixy::Option<bench::CpuId> waiter{};
    ::fixy::Option<bench::CpuId> writer{};
};

// Pins the calling thread to the core of its slot, when the run has one.
void pin_to(::fixy::Option<bench::CpuId> const& core) noexcept {
    core.match(
        [](bench::CpuId cpu) noexcept {
            bench::pin_calling_thread_to(cpu).expect("the pin of a thread of the bench to a core of its claim");
        },
        []() noexcept {});
}

// The counter of the core, after each instruction before the read.  A round
// reads it on each side, so the reading of the waiter comes after the load
// that saw the store.
[[nodiscard]] std::uint64_t read_ordered_counter() noexcept { return bench::rdtsc_end(); }

// The window of the delay of the writer before its store in a round.
enum class DelayWindow : std::uint8_t {
    // From 2000 to 5999 ticks, about 1 to 3 microseconds on a counter of
    // 2 GHz: a spin waits through it.
    spin,
    // From 40000 to 119980 ticks, about 18 to 53 microseconds: a waiter
    // with no spin budget sleeps in the kernel before the store.
    park,
};

// The delay of the writer before its store in a round.
[[nodiscard]] std::uint64_t delay_ticks_of(Round round, DelayWindow window) noexcept {
    std::uint64_t const spread = (round.raw() * 7919U) % 4000U;
    return window == DelayWindow::spin ? 2000U + spread : 40000U + spread * 20U;
}

// Spins with CRUCIBLE_SPIN_PAUSE for DELAY.
void spin_for(spin::CounterTicks delay) noexcept {
    std::uint64_t const end = os::read_spin_counter() + delay.raw();
    while (os::read_spin_counter() < end) {
        CRUCIBLE_SPIN_PAUSE;
    }
}

// THE FORMS of a wake row.  Each waits until the word of LINE holds ROUND.
// Each wait is a function of its own, aligned to 64 bytes and not inlined.
// The code layout of the bench then moves no row against another: with the
// forms inlined, one form changed its ratio by up to 70 thousandths from one
// build of the bench to the next.

// The reference: an acquire load and a pause in each turn.
struct PlainPauseWait final {
    [[gnu::noinline, gnu::aligned(64)]] void wait(Line const& line, Round round) const noexcept {
        while (line.word.load_acquire() != round.raw()) {
            CRUCIBLE_SPIN_PAUSE;
        }
    }
};

// A loop of acquire loads with no hint, for comparison.
struct BareLoadWait final {
    [[gnu::noinline, gnu::aligned(64)]] void wait(Line const& line, Round round) const noexcept {
        while (line.word.load_acquire() != round.raw()) {}
    }
};

// spin_until with a deadline that no round reaches.
struct SpinUntilWait final {
    ::fixy::BgLoadCtx const& ctx;

    [[gnu::noinline, gnu::aligned(64)]] void wait(Line const& line, Round round) const noexcept {
        eff::cap::Nondet const nondet = eff::extract_bare_from_ctx<eff::Effect::Nondet>(ctx);
        spin::SpinDeadline const deadline = spin::SpinDeadline::after(nondet, kLongBudget);
        static_cast<void>(spin::spin_until(
            nondet, deadline, [&line, round]() noexcept { return line.word.load_acquire() == round.raw(); }));
    }
};

// spin_while_equal with the word arm of the process and a deadline that no
// round reaches.
struct SpinWhileEqualWait final {
    ::fixy::BgLoadCtx const& ctx;

    [[gnu::noinline, gnu::aligned(64)]] void wait(Line const& line, Round round) const noexcept {
        eff::cap::Nondet const nondet = eff::extract_bare_from_ctx<eff::Effect::Nondet>(ctx);
        spin::SpinDeadline const deadline = spin::SpinDeadline::after(nondet, kLongBudget);
        static_cast<void>(spin::spin_while_equal(nondet, line.word, round.raw() - 1U, deadline));
    }
};

// One arm of the spin-hint door, with a deadline that no round reaches.
struct ArmWait final {
    os::SpinArm arm = os::SpinArm::pause;

    [[gnu::noinline, gnu::aligned(64)]] void wait(Line const& line, Round round) const noexcept {
        static_cast<void>(os::wait_for_change(line.word, round.raw() - 1U, os::read_spin_counter() + kFarTicks, arm));
    }
};

// wait_for of an event count with the policy POLICY (THE PARK).
template <class Policy>
struct EventCountWait final {
    ::fixy::BgLoadCtx const& ctx;
    cc::EventCount& events;

    [[gnu::noinline, gnu::aligned(64)]] void wait(Line const& line, Round round) const noexcept {
        cc::wait_for<Policy>(ctx, events,
                             [&line, round]() noexcept { return line.word.load_acquire() == round.raw(); });
    }
};

// The reference of a park: a futex wait on the word itself through the futex
// door (THE PARK).
struct FutexWait final {
    ::fixy::BgLoadCtx const& ctx;

    [[gnu::noinline, gnu::aligned(64)]] void wait(Line const& line, Round round) const noexcept {
        std::uint32_t const before = round.raw() - 1U;
        while (line.word.load_acquire() == before) {
            static_cast<void>(
                ::fixy::futex::wait(ctx, line.word, before).expect("the futex wait of the bench on its own word"));
        }
    }
};

// The writer of a spin row does nothing after its store.
struct NoNotify final {
    void notify(Line const& /*line*/) const noexcept {}
};

// The writer of a row of an event count notifies one waiter.
struct EventCountNotify final {
    ::fixy::BgLoadCtx const& ctx;
    cc::EventCount& events;

    void notify(Line const& /*line*/) const noexcept { events.notify_one(ctx); }
};

// The writer of the reference of a park wakes one waiter of the word.
struct FutexWake final {
    ::fixy::BgLoadCtx const& ctx;

    void notify(Line const& line) const noexcept {
        static_cast<void>(
            ::fixy::futex::wake_one(ctx, line.word).expect("the futex wake of the bench on its own word"));
    }
};

// The waiter of a wake row.  It waits for each round with FORM, and it keeps
// the latency of each wake, which the caller reads after the join.
template <class Form>
struct MeasureWake {
    Line const& line;
    Handshake& handshake;
    RowCores const& cores;
    Form const& form;
    HeapArray<std::uint64_t> latencies;

    void operator()() noexcept {
        pin_to(cores.waiter);
        std::uint32_t number = 0;
        for (std::uint64_t& latency : latencies) {
            ++number;
            handshake.ready.store_release(number);
            form.wait(line, Round{number});
            std::uint64_t const woken = read_ordered_counter();
            latency = core::saturating_sub(woken, handshake.stamp.load_acquire());
        }
    }
};

// The wake latencies of kSamples rounds of FORM, in ticks of the counter.
template <class Form, class Notify>
[[nodiscard]] HeapArray<std::uint64_t> measure_wake(::fixy::BgLoadCtx const& ctx, RowCores const& cores,
                                                    Form const& form, Notify const& after_store,
                                                    DelayWindow window) noexcept {
    Line line{};
    Handshake handshake{};
    MeasureWake<Form> waiter{.line = line,
                             .handshake = handshake,
                             .cores = cores,
                             .form = form,
                             .latencies = core::mint_heap_array(kAlloc, kSamples, std::uint64_t{0})};
    auto writer = [&line, &handshake, &cores, &after_store, window]() noexcept {
        pin_to(cores.writer);
        for (std::uint32_t number = 1; number <= kSamples; ++number) {
            while (handshake.ready.load_acquire() != number) {
                CRUCIBLE_SPIN_PAUSE;
            }
            spin_for(spin::CounterTicks{delay_ticks_of(Round{number}, window)});
            handshake.stamp.store_release(read_ordered_counter());
            line.word.store_release(number);
            after_store.notify(line);
        }
    };
    ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, waiter, writer)
        .expect("the two threads of a wake row start and join");
    return std::move(waiter.latencies);
}

// The samples of a row in ascending order.
[[nodiscard]] HeapArray<std::uint64_t> sorted(HeapArray<std::uint64_t>&& samples) noexcept {
    HeapArray<std::uint64_t> scratch = core::mint_heap_array(kAlloc, samples.size(), std::uint64_t{0});
    samples = core::with_mut(std::move(samples), [&](auto elements) noexcept {
        scratch = core::with_mut(std::move(scratch), [&](auto room) noexcept {
            core::sort_stably(std::move(elements), std::move(room), core::Ascending{})
                .expect("a sort of the samples with a scratch of the same count");
        });
    });
    return std::move(samples);
}

// The sample at the fraction PERMILLE of the sorted samples.
[[nodiscard]] std::uint64_t sample_at(HeapArray<std::uint64_t> const& ascending, Permille permille) noexcept {
    std::size_t const last = ascending.size() - 1;
    std::size_t const position = last * permille.raw() / 1000U;
    return *ascending.view().get(position).expect("the position of a percentile is in the samples");
}

// A count of ticks as the nearest count of nanoseconds.
[[nodiscard]] std::uint64_t nanoseconds_of(spin::CounterTicks ticks) noexcept {
    return static_cast<std::uint64_t>(static_cast<double>(ticks.raw()) * bench::Timer::ns_per_cycle() + 0.5);
}

// The mean of the sorted samples from p1 to p99, in picoseconds.  The
// counter of an AMD Zen core moves in steps of 10 ns, so a p50 has that
// grain.  The mean of many samples resolves less than one step, and the cut
// at p1 and p99 keeps a rare stall of the host out of it.  Complexity:
// linear in the samples.
[[nodiscard]] std::uint64_t trimmed_mean_picoseconds(HeapArray<std::uint64_t> const& ascending) noexcept {
    std::size_t const first = (ascending.size() - 1) / 100U;
    std::size_t const last = (ascending.size() - 1) * 99U / 100U;
    double sum = 0.0;
    for (std::size_t position = first; position <= last; ++position) {
        sum += static_cast<double>(*ascending.view().get(position).expect("the position is in the samples"));
    }
    double const mean_ticks = sum / static_cast<double>(last - first + 1);
    return static_cast<std::uint64_t>(mean_ticks * bench::Timer::ns_per_cycle() * 1000.0 + 0.5);
}

// The median of three values of the measurements of one row.
[[nodiscard]] std::uint64_t middle_of(::fixy::FixedArray<std::uint64_t, kRepeats> const& values) noexcept {
    std::uint64_t const first = values.at(::fixy::index<0>());
    std::uint64_t const second = values.at(::fixy::index<1>());
    std::uint64_t const third = values.at(::fixy::index<2>());
    return ::fixy::max(::fixy::min(first, second), ::fixy::min(::fixy::max(first, second), third));
}

// The medians of the measurements of one row, in ticks, the trimmed means in
// picoseconds, and the name of the row.
struct RowMedians {
    ::fixy::TextView name{};
    ::fixy::FixedArray<std::uint64_t, kRepeats> medians{};
    ::fixy::FixedArray<std::uint64_t, kRepeats> trimmed_means{};

    // The spread of the medians, (high - low) / low, in thousandths.
    [[nodiscard]] std::uint64_t spread_permille() const noexcept {
        std::uint64_t low = ~std::uint64_t{0};
        std::uint64_t high = 0;
        for (std::uint64_t const median : medians) {
            low = ::fixy::min(low, median);
            high = ::fixy::max(high, median);
        }
        return low == 0 ? 0 : (high - low) * 1000U / low;
    }
};

// Measures one wake row kRepeats times, and writes a line for each
// measurement and a line of the spread.
template <class Form, class Notify>
[[nodiscard]] RowMedians report_wake_row(::fixy::BgLoadCtx const& ctx, RowCores const& cores, ::fixy::TextView name,
                                         Form const& form, Notify const& after_store,
                                         DelayWindow window = DelayWindow::spin) noexcept {
    RowMedians row{.name = name};
    for (std::size_t repeat = 0; repeat < kRepeats; ++repeat) {
        HeapArray<std::uint64_t> const ascending = sorted(measure_wake(ctx, cores, form, after_store, window));
        std::uint64_t const median = sample_at(ascending, Permille{500});
        std::uint64_t const trimmed_mean = trimmed_mean_picoseconds(ascending);
        *row.medians.view().get(repeat).expect("the repeat is in the row") = median;
        *row.trimmed_means.view().get(repeat).expect("the repeat is in the row") = trimmed_mean;
        bench::print("{}  wake p50={} ns  p99={} ns  max={} ns  mean p1-p99={} ps\n", name,
                     nanoseconds_of(spin::CounterTicks{median}),
                     nanoseconds_of(spin::CounterTicks{sample_at(ascending, Permille{990})}),
                     nanoseconds_of(spin::CounterTicks{sample_at(ascending, Permille{1000})}), trimmed_mean);
    }
    bench::print("{}  spread of the medians={} permille\n", name, row.spread_permille());
    return row;
}

// A row and its reference.
struct RowPair final {
    RowMedians const& row;
    RowMedians const& reference;
};

// The measure of a row that a ratio compares.
enum class RowMeasure : std::uint8_t {
    median,
    trimmed_mean,
};

// The values of one measure of a row.
[[nodiscard]] ::fixy::FixedArray<std::uint64_t, kRepeats> const& values_of(RowMedians const& row,
                                                                           RowMeasure measure) noexcept {
    return measure == RowMeasure::median ? row.medians : row.trimmed_means;
}

// The middle value of MEASURE of the row over that of its reference, in
// thousandths, or 0 for a reference of 0.
[[nodiscard]] std::uint64_t permille_of(RowPair const& pair, RowMeasure measure) noexcept {
    std::uint64_t const base = middle_of(values_of(pair.reference, measure));
    return base == 0 ? 0 : (middle_of(values_of(pair.row, measure)) * 1000U + base / 2) / base;
}

// Writes the median of the row over the median of its reference, and the
// same ratio of the trimmed means, in thousandths.
void print_ratio(RowPair const& pair) noexcept {
    bench::print("{}  ratio to {}: p50={} permille  mean p1-p99={} permille\n", pair.row.name, pair.reference.name,
                 permille_of(pair, RowMeasure::median), permille_of(pair, RowMeasure::trimmed_mean));
}

// The name of an arm, as the door names it.
[[nodiscard]] ::fixy::TextView name_of(os::SpinArm arm) noexcept {
    switch (arm) {
        case os::SpinArm::pause:
            return "arm pause";
        case os::SpinArm::tpause:
            return "arm tpause";
        case os::SpinArm::umwait:
            return "arm umwait";
        case os::SpinArm::mwaitx:
            return "arm mwaitx";
        case os::SpinArm::yield:
            return "arm yield";
        case os::SpinArm::isb:
            return "arm isb";
        case os::SpinArm::wfe:
            return "arm wfe";
        case os::SpinArm::wfet:
            return "arm wfet";
        default:
            return "arm unknown";
    }
}

// Whether a writer row has a reader on the line.
enum class ReaderPresence : std::uint8_t {
    none,
    waiting,
};

// The thread of a writer row that adds to the neighbor word and measures the
// add.  With a reader, it waits until the reader waits first.  It keeps its
// report, which the caller reads after the join.
struct MeasureWriter {
    Line& line;
    Handshake const& handshake;
    ReaderPresence presence = ReaderPresence::none;
    bench::Report measured{};

    void operator()() noexcept {
        if (presence == ReaderPresence::waiting) {
            while (handshake.ready.load_acquire() == 0) {
                CRUCIBLE_SPIN_PAUSE;
            }
        }
        measured = bench::run("writer add", [this] {
            std::uint32_t const before = line.neighbor.fetch_add_acq_rel(1);
            bench::do_not_optimize(before);
        });
        line.word.store_release(1);
    }
};

// The percentiles of one add of the writer to the neighbor word, with a
// reader that waits on the word with ARM, or with no reader (THE WRITER).
[[nodiscard]] bench::Report measure_writer(::fixy::BgLoadCtx const& ctx, RowCores const& cores, ReaderPresence presence,
                                           ArmWait const& reader_form) noexcept {
    Line line{};
    Handshake handshake{};
    MeasureWriter adder{.line = line, .handshake = handshake, .presence = presence};
    auto reader = [&line, &handshake, &cores, &reader_form, presence]() noexcept {
        if (presence == ReaderPresence::none) return;
        pin_to(cores.writer);
        handshake.ready.store_release(1);
        reader_form.wait(line, Round{1});
    };
    ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, adder, reader)
        .expect("the two threads of a writer row start and join");
    return std::move(adder.measured);
}

// A writer row and the row with no reader.
struct WriterPair final {
    bench::Report const& measured;
    bench::Report const& alone;
};

// Writes one line of a writer row, and its ratio to the row with no reader.
void print_writer_row(::fixy::TextView name, WriterPair const& pair) noexcept {
    double const base = pair.alone.pct.p50;
    std::uint64_t const ratio =
        base > 0.0 ? static_cast<std::uint64_t>(pair.measured.pct.p50 / base * 1000.0 + 0.5) : 0;
    bench::print("{}  add p50={} ps  p99={} ps  max={} ps  ratio to no reader={} permille\n", name,
                 static_cast<std::uint64_t>(pair.measured.pct.p50 * 1000.0 + 0.5),
                 static_cast<std::uint64_t>(pair.measured.pct.p99 * 1000.0 + 0.5),
                 static_cast<std::uint64_t>(pair.measured.pct.max * 1000.0 + 0.5), ratio);
}

// A row of one call and its reference row.
struct CallPair final {
    ::fixy::TextView name;
    bench::Report const& measured;
    ::fixy::TextView reference_name;
    bench::Report const& reference;
};

// Writes one line of a row of one call, and its ratio to its reference.
void print_call_row(CallPair const& pair) noexcept {
    double const base = pair.reference.pct.p50;
    std::uint64_t const ratio =
        base > 0.0 ? static_cast<std::uint64_t>(pair.measured.pct.p50 / base * 1000.0 + 0.5) : 0;
    bench::print("{}  p50={} ps  p99={} ps  max={} ps  ratio to {}={} permille\n", pair.name,
                 static_cast<std::uint64_t>(pair.measured.pct.p50 * 1000.0 + 0.5),
                 static_cast<std::uint64_t>(pair.measured.pct.p99 * 1000.0 + 0.5),
                 static_cast<std::uint64_t>(pair.measured.pct.max * 1000.0 + 0.5), pair.reference_name, ratio);
}

// THE NOTIFY: a notify of an event count with no waiter, the notify of folly
// with no waiter, and the signal of the park before the event count.
void report_notify_rows(::fixy::BgLoadCtx const& ctx) noexcept {
    // The bits of the waiters in the word of folly, below the epoch.
    constexpr std::uint64_t kFollyWaiterMask = 0xFFFFFFFFU;
    constexpr std::uint64_t kFollyAddEpoch = std::uint64_t{1} << 32;
    cc::EventCount events{};
    core::Atomic<std::uint64_t> folly_word{0};
    core::Atomic<std::uint32_t> park_word{0};
    bench::Report const notify = bench::run("EventCount::notify_one", [&] { events.notify_one(ctx); });
    bench::Report const folly = bench::run("folly notify", [&] {
        std::uint64_t const before = folly_word.fetch_add_acq_rel(kFollyAddEpoch);
        bench::do_not_optimize(before & kFollyWaiterMask);
    });
    bench::Report const exchange = bench::run("park signal", [&] {
        std::uint32_t const before = park_word.exchange_acq_rel(0);
        bench::do_not_optimize(before);
    });
    print_call_row(CallPair{.name = "folly notify, no waiter: fetch-add of the epoch",
                            .measured = folly,
                            .reference_name = "itself",
                            .reference = folly});
    print_call_row(CallPair{.name = "park signal: exchange of a word",
                            .measured = exchange,
                            .reference_name = "folly notify",
                            .reference = folly});
    print_call_row(CallPair{.name = "EventCount::notify_one, no waiter: fence and load",
                            .measured = notify,
                            .reference_name = "folly notify",
                            .reference = folly});
}

// THE RATE: the ticks of one second at the rate of the budget and at the
// calibration of the harness, and their difference in parts per million.
void report_rate(::fixy::BgLoadCtx const& ctx) noexcept {
    eff::cap::Nondet const nondet = eff::extract_bare_from_ctx<eff::Effect::Nondet>(ctx);
    std::uint64_t const of_budget =
        spin::ticks_of(spin::tick_rate_of_process(nondet), spin::SpinBudget{1000000000U}).raw();
    auto const of_harness = static_cast<std::uint64_t>(bench::Timer::tsc_freq_hz() + 0.5);
    std::uint64_t const difference = of_budget > of_harness ? of_budget - of_harness : of_harness - of_budget;
    std::uint64_t const parts = of_harness == 0 ? 0 : difference * 1000000U / of_harness;
    bench::print("rate  budget={} ticks/s  harness={} ticks/s  difference={} ppm\n", of_budget, of_harness, parts);
}

}  // namespace

int main() {
    bench::elevate_priority();
    ::fixy::BgLoadCtx const ctx{eff::testing::bg()};
    bench::Cores const cores = bench::Cores::of_calling_thread();
    RowCores const row_cores{.waiter = cores.of_slot(bench::ThreadSlot{0}),
                             .writer = cores.of_slot(bench::ThreadSlot{1})};
    report_rate(ctx);

    RowMedians const plain = report_wake_row(ctx, row_cores, "plain pause loop", PlainPauseWait{}, NoNotify{});
    RowMedians const bare = report_wake_row(ctx, row_cores, "bare load loop", BareLoadWait{}, NoNotify{});
    print_ratio(RowPair{.row = bare, .reference = plain});
    RowMedians const until = report_wake_row(ctx, row_cores, "spin_until", SpinUntilWait{ctx}, NoNotify{});
    print_ratio(RowPair{.row = until, .reference = plain});
    RowMedians const while_equal =
        report_wake_row(ctx, row_cores, "spin_while_equal", SpinWhileEqualWait{ctx}, NoNotify{});
    print_ratio(RowPair{.row = while_equal, .reference = plain});
    os::SpinArmSet const arms = os::spin_arms_of_core();
    for (os::SpinArm const arm : kEachArm) {
        if (!arms.has(arm)) continue;
        RowMedians const of_arm = report_wake_row(ctx, row_cores, name_of(arm), ArmWait{.arm = arm}, NoNotify{});
        print_ratio(RowPair{.row = of_arm, .reference = plain});
    }
    // THE PARK.
    cc::EventCount events{};
    RowMedians const event_spin =
        report_wake_row(ctx, row_cores, "EventCount spin then park, 1 ms",
                        EventCountWait<cc::SpinThenPark<1000000>>{.ctx = ctx, .events = events},
                        EventCountNotify{.ctx = ctx, .events = events});
    print_ratio(RowPair{.row = event_spin, .reference = plain});
    RowMedians const futex_park =
        report_wake_row(ctx, row_cores, "futex wait and wake", FutexWait{ctx}, FutexWake{ctx}, DelayWindow::park);
    RowMedians const event_park = report_wake_row(ctx, row_cores, "EventCount park",
                                                  EventCountWait<cc::SpinThenPark<0>>{.ctx = ctx, .events = events},
                                                  EventCountNotify{.ctx = ctx, .events = events}, DelayWindow::park);
    print_ratio(RowPair{.row = event_park, .reference = futex_park});
    report_notify_rows(ctx);
    // The reference again, at the end: its ratio to the first run shows the
    // drift of the host during the bench.
    RowMedians const plain_again =
        report_wake_row(ctx, row_cores, "plain pause loop again", PlainPauseWait{}, NoNotify{});
    print_ratio(RowPair{.row = plain_again, .reference = plain});

    bench::Report const alone = measure_writer(ctx, row_cores, ReaderPresence::none, ArmWait{});
    print_writer_row("writer, no reader", WriterPair{.measured = alone, .alone = alone});
    for (os::SpinArm const arm : kEachArm) {
        if (!arms.has(arm)) continue;
        bench::Report const with_reader = measure_writer(ctx, row_cores, ReaderPresence::waiting, ArmWait{.arm = arm});
        print_writer_row(name_of(arm), WriterPair{.measured = with_reader, .alone = alone});
    }
    return 0;
}
