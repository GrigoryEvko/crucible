// The push and the pop of the permissioned channels, across threads.
//
// THE OPS
//   spsc push   try_push of the producer handle of an SPSC channel on the main
//               thread, and try_pop of its consumer handle on one worker
//   spsc pop    try_pop of the consumer handle on the main thread, and try_push
//               of the producer handle on one worker
//   mpsc push   try_push of a producer handle of an MPSC channel on the main
//               thread and on each worker but the last, and try_pop of the
//               consumer handle on the last worker
//   mpsc pop    try_pop of the consumer handle on the main thread, and try_push
//               of a producer handle on each worker
//   A push into a full ring and a pop of an empty ring wait with the spin hint.
//   The wait of a worker stops when the row stops.
//
// THE PHASES OF A ROW
//   1. The window: the main thread runs its op for kWindowNs and counts it.
//      The row gives the ops of the main thread in one second.
//   2. The latency: the workers go on, and the main thread runs its op under
//      Run::measure, one op in each sample.  The report gives p50, p99 and the
//      maximum of one op, with the wait for the other side in it.
//
// THE ROWS
//   spsc push and spsc pop with 2 threads, mpsc push and mpsc pop with 2 and
//   4 threads.  Slot k of the spread holds thread k, and slot 0 is the main
//   thread (THE SPREAD OF A RUN of bench/bench_harness.h).  The bench states
//   four cores, so loop bench gives it one L3 group:
//       python3 utils/bin/loop bench bench_permissioned_channel

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/PermissionedMpscChannel.h>
#include <fixy/concurrent/PermissionedSpscChannel.h>
#include <fixy/os/OwnedThread.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Linear.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <cstdint>
#include <utility>

#include "bench_harness.h"

namespace {

namespace cc = ::fixy::concurrent;
namespace eff = ::foundation::effects;
namespace perm = ::foundation::permissions;

// The context that starts the workers: Bg for the threads, Alloc for their frames.
using Starter = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;
using Item = std::uint64_t;

constexpr std::size_t kCapacity = 4096;
constexpr std::size_t kMaxWorkers = 3;
constexpr std::uint64_t kWindowNs = 500000000;
constexpr std::size_t kLatencySamples = 1000000;
constexpr std::size_t kLatencyWarmup = 10000;
constexpr std::size_t kLatencyMs = 1000;
// The ops of a worker between two reads of the phase, and of the main thread
// between two reads of the clock.
constexpr std::uint64_t kOpsPerCheck = 64;
constexpr std::uint64_t kOpsPerClock = 1024;

// Each row mints the root of its channel in its body, so the brand of the
// root stays in the scope that made it.
struct SpscBenchTag {};
struct MpscBenchTag {};

// The phase of a row.  The main thread writes it.
enum class Phase : std::uint32_t {
    Wait,
    Window,
    Latency,
    Stop,
};

struct alignas(64) PhaseLine {
    ::fixy::Atomic<Phase> phase{Phase::Wait};
};

struct alignas(64) CountLine {
    ::fixy::Atomic<std::uint64_t> value{};
};

// The state that the threads of one row share.  The main thread owns it, and
// it outlives each worker of the row.
struct RowControl {
    PhaseLine phase{};
    // The workers that wait for the window.
    CountLine ready{};
};

struct ThreadCountTag final {};
using ThreadCount = ::fixy::Id<ThreadCountTag, std::uint32_t>;

[[nodiscard]] Phase read_phase(RowControl const& control) noexcept { return control.phase.phase.load_acquire(); }

[[nodiscard]] bench::CpuId cpu_of(bench::Spread const& spread, bench::ThreadSlot slot) noexcept {
    return spread.of_slot(slot).expect("a row has at most the slots of the spread");
}

// Starts the worker of a slot on the CPU of the slot.
template <class Body>
[[nodiscard]] ::fixy::thread::OwnedThread start_worker(bench::Spread const& spread, bench::ThreadSlot slot,
                                                       Body body) noexcept {
    ::fixy::thread::ThreadOptions const options{
        .name = "channel-worker",
        .affinity = ::fixy::AffinityMask::single(static_cast<std::uint16_t>(cpu_of(spread, slot).raw())),
    };
    return ::fixy::thread::mint_owned_thread(Starter{eff::testing::bg()}, options, std::move(body))
        .expect("the thread door starts a worker of a row");
}

// A push that waits while the ring is full.  The wait stops when the row stops.
template <class Producer>
[[gnu::always_inline]] inline void push_one(RowControl const& control, Producer& producer, Item item) noexcept {
    while (!producer.try_push(item)) {
        if (read_phase(control) == Phase::Stop) return;
        CRUCIBLE_SPIN_PAUSE;
    }
}

// A pop that waits while the ring is empty.  The wait stops when the row stops.
template <class Pop>
[[gnu::always_inline]] inline void pop_one(RowControl const& control, Pop const& pop) noexcept {
    for (;;) {
        auto item = pop();
        if (item.is_some()) {
            bench::do_not_optimize(item);
            return;
        }
        if (read_phase(control) == Phase::Stop) return;
        CRUCIBLE_SPIN_PAUSE;
    }
}

// The loop of a worker: it waits for the window, then runs its op until the
// stop.
template <class Op>
void run_worker(RowControl& control, Op const& op) noexcept {
    static_cast<void>(control.ready.value.fetch_add_acq_rel(1));
    while (read_phase(control) == Phase::Wait)
        CRUCIBLE_SPIN_PAUSE;
    while (read_phase(control) != Phase::Stop) {
        for (std::uint64_t count = 0; count < kOpsPerCheck; ++count)
            op();
    }
}

// What the window of the main thread measured.
struct Window {
    std::uint64_t ops = 0;
    std::uint64_t wall_ns = 0;
    bench::Pause pause{};
};

// The result of one row.
struct RowResult {
    ThreadCount threads{1};
    // The ops of the main thread in one second, in hundredths of a million.
    std::uint64_t centi_mops = 0;
    bench::Pause window_pause{};
    bench::Report latency{};
};

// The two phases of the main thread (THE PHASES OF A ROW).  The workers of
// the row run already, one for each thread apart from the main thread, and
// the caller stops and joins them after the call.  The run gives the name of
// the row.
template <class Op>
[[nodiscard]] RowResult measure_main(RowControl& control, bench::Spread const& spread, ThreadCount threads,
                                     bench::Run run, Op const& op) {
    std::uint32_t const workers = threads.raw() - 1;
    while (control.ready.value.load_acquire() < workers)
        CRUCIBLE_SPIN_PAUSE;
    bench::CpuId const main_cpu = cpu_of(spread, bench::kMeasuringSlot);
    Window window{};
    {
        bench::CoreHold const hold{main_cpu};
        ::fixy::InitLoadCtx const startup{eff::testing::init()};
        eff::cap::Nondet const nondet = eff::testing::test().nondet;
        auto const clock = ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::Monotonic>(startup);
        auto const now = [&clock, &nondet]() noexcept {
            return clock.read(nondet).expect("a read of the monotonic clock").peek();
        };
        bench::PauseReading const before = bench::detail::read_pause();
        std::uint64_t const start = now();
        control.phase.phase.store_release(Phase::Window);
        std::uint64_t end = start;
        while (end - start < kWindowNs) {
            for (std::uint64_t count = 0; count < kOpsPerClock; ++count)
                op();
            window.ops += kOpsPerClock;
            end = now();
        }
        control.phase.phase.store_release(Phase::Latency);
        bench::PauseReading const after = bench::detail::read_pause();
        window.wall_ns = end - start;
        window.pause = bench::detail::pause_between(::fixy::side_as<bench::side::ReadingBefore>(before),
                                                    ::fixy::side_as<bench::side::ReadingAfter>(after));
    }
    RowResult result{.threads = threads,
                     .centi_mops = window.wall_ns == 0 ? 0 : window.ops * 100000 / window.wall_ns,
                     .window_pause = window.pause};
    result.latency = run.core(main_cpu.raw())
                         .batch(1)
                         .samples(kLatencySamples)
                         .warmup(kLatencyWarmup)
                         .max_wall_ms(kLatencyMs)
                         .measure([&op] { op(); });
    bench::print_thread_cpus("the row");
    control.phase.phase.store_release(Phase::Stop);
    return result;
}

// spsc push: the main thread pushes, and the worker of slot 1 pops.
[[nodiscard]] RowResult spsc_push_row(bench::Spread const& spread) {
    auto root = perm::mint_permission_root<cc::spsc_tag::Whole<SpscBenchTag>>();
    using Spsc = cc::spsc_channel_t<Item, kCapacity, decltype(root)>;
    RowControl control{};
    Spsc channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Spsc::producer_tag, Spsc::consumer_tag>(std::move(root));
    auto producer = channel.producer(std::move(producer_perm));
    auto consumer = channel.consumer(std::move(consumer_perm));
    ::fixy::thread::OwnedThread worker = start_worker(spread, bench::ThreadSlot{1}, [&control, &consumer]() noexcept {
        run_worker(control, [&control, &consumer]() noexcept {
            pop_one(control, [&consumer]() noexcept { return consumer.try_pop(); });
        });
    });
    Item next = 0;
    RowResult result = measure_main(control, spread, ThreadCount{2}, bench::Run{"spsc push, 2 threads"},
                                    [&control, &producer, &next]() noexcept { push_one(control, producer, ++next); });
    std::move(worker).join();
    ::foundation::linear::drop(std::move(producer));
    ::foundation::linear::drop(std::move(consumer));
    return result;
}

// spsc pop: the main thread pops, and the worker of slot 1 pushes.
[[nodiscard]] RowResult spsc_pop_row(bench::Spread const& spread) {
    auto root = perm::mint_permission_root<cc::spsc_tag::Whole<SpscBenchTag>>();
    using Spsc = cc::spsc_channel_t<Item, kCapacity, decltype(root)>;
    RowControl control{};
    Spsc channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Spsc::producer_tag, Spsc::consumer_tag>(std::move(root));
    auto producer = channel.producer(std::move(producer_perm));
    auto consumer = channel.consumer(std::move(consumer_perm));
    ::fixy::thread::OwnedThread worker = start_worker(spread, bench::ThreadSlot{1}, [&control, &producer]() noexcept {
        Item next = 0;
        run_worker(control, [&control, &producer, &next]() noexcept { push_one(control, producer, ++next); });
    });
    RowResult result = measure_main(
        control, spread, ThreadCount{2}, bench::Run{"spsc pop, 2 threads"},
        [&control, &consumer]() noexcept { pop_one(control, [&consumer]() noexcept { return consumer.try_pop(); }); });
    std::move(worker).join();
    ::foundation::linear::drop(std::move(producer));
    ::foundation::linear::drop(std::move(consumer));
    return result;
}

// A producer handle that the pool of the channel lends.  No exclusive is out
// in the bench, so an empty lend ends the process.  The channel type is a
// template argument, so the brand of the handle comes from the caller.
template <class Channel>
[[nodiscard]] typename Channel::ProducerHandle lend(Channel& channel) noexcept {
    return channel.producer().expect("the channel lends a producer while no exclusive is out");
}

// mpsc push: the main thread and the workers of slots 1 to threads - 2 push,
// and the worker of the last slot pops.
[[nodiscard]] RowResult mpsc_push_row(bench::Spread const& spread, ThreadCount threads, bench::Run run) {
    auto root = perm::mint_permission_root<cc::mpsc_tag::Whole<MpscBenchTag>>();
    using Mpsc = cc::mpsc_channel_t<Item, kCapacity, decltype(root)>;
    RowControl control{};
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Mpsc::producer_tag, Mpsc::consumer_tag>(std::move(root));
    Mpsc channel{std::move(producer_root)};
    auto consumer = channel.consumer(std::move(consumer_perm));
    eff::cap::Nondet const merge = eff::testing::test().nondet;
    ::fixy::FixedList<::fixy::thread::OwnedThread, kMaxWorkers> workers{};
    std::uint32_t const consumer_slot = threads.raw() - 1;
    for (std::uint32_t slot = 1; slot < consumer_slot; ++slot) {
        static_cast<void>(workers
                              .append(start_worker(spread, bench::ThreadSlot{slot},
                                                   [&control, &channel]() noexcept {
                                                       Mpsc::ProducerHandle producer = lend(channel);
                                                       Item next = 0;
                                                       run_worker(control, [&control, &producer, &next]() noexcept {
                                                           push_one(control, producer, ++next);
                                                       });
                                                   }))
                              .expect("a row has at most kMaxWorkers workers"));
    }
    static_cast<void>(workers
                          .append(start_worker(spread, bench::ThreadSlot{consumer_slot},
                                               [&control, &consumer, merge]() noexcept {
                                                   run_worker(control, [&control, &consumer, merge]() noexcept {
                                                       pop_one(control, [&consumer, merge]() noexcept {
                                                           return consumer.try_pop(merge);
                                                       });
                                                   });
                                               }))
                          .expect("a row has at most kMaxWorkers workers"));
    RowResult result{};
    {
        Mpsc::ProducerHandle producer = lend(channel);
        Item next = 0;
        result = measure_main(control, spread, threads, std::move(run),
                              [&control, &producer, &next]() noexcept { push_one(control, producer, ++next); });
    }
    for (::fixy::thread::OwnedThread& worker : workers) {
        std::move(worker).join();
    }
    ::foundation::linear::drop(std::move(consumer));
    return result;
}

// mpsc pop: the main thread pops, and the workers of slots 1 to threads - 1
// push.
[[nodiscard]] RowResult mpsc_pop_row(bench::Spread const& spread, ThreadCount threads, bench::Run run) {
    auto root = perm::mint_permission_root<cc::mpsc_tag::Whole<MpscBenchTag>>();
    using Mpsc = cc::mpsc_channel_t<Item, kCapacity, decltype(root)>;
    RowControl control{};
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Mpsc::producer_tag, Mpsc::consumer_tag>(std::move(root));
    Mpsc channel{std::move(producer_root)};
    auto consumer = channel.consumer(std::move(consumer_perm));
    eff::cap::Nondet const merge = eff::testing::test().nondet;
    ::fixy::FixedList<::fixy::thread::OwnedThread, kMaxWorkers> workers{};
    for (std::uint32_t slot = 1; slot < threads.raw(); ++slot) {
        static_cast<void>(workers
                              .append(start_worker(spread, bench::ThreadSlot{slot},
                                                   [&control, &channel]() noexcept {
                                                       Mpsc::ProducerHandle producer = lend(channel);
                                                       Item next = 0;
                                                       run_worker(control, [&control, &producer, &next]() noexcept {
                                                           push_one(control, producer, ++next);
                                                       });
                                                   }))
                              .expect("a row has at most kMaxWorkers workers"));
    }
    RowResult result = measure_main(control, spread, threads, std::move(run), [&control, &consumer, merge]() noexcept {
        pop_one(control, [&consumer, merge]() noexcept { return consumer.try_pop(merge); });
    });
    for (::fixy::thread::OwnedThread& worker : workers) {
        std::move(worker).join();
    }
    ::foundation::linear::drop(std::move(consumer));
    return result;
}

// Prints one row: its threads, the throughput of the main thread, its pause,
// and the report of its latency.
void print_row(RowResult const& row) noexcept {
    bool const is_paused = row.window_pause.is_over_bound();
    // The rows of the harness go through stdio, and report() does not.
    bench::detail::flush_standard_output();
    ::fixy::report(::fixy::Sink::Out, "  {} threads: {}.{}{} Mops/s of the main thread, window pause {} us{}\n",
                   row.threads.raw(), row.centi_mops / 100, row.centi_mops % 100 / 10, row.centi_mops % 10,
                   row.window_pause.own_ns() / 1000, is_paused ? ::fixy::TextView{"  NOT VALID"} : ::fixy::TextView{});
    row.latency.print_text();
}

int run_bench() {
    bench::print_system_info();
    bench::elevate_priority();
    bench::Spread const spread = bench::Spread::of_calling_thread();
    bench::detail::flush_standard_output();
    if (spread.source() != bench::SpreadSource::Claim) {
        ::fixy::report(::fixy::Sink::Out,
                       "  NOTE: no claimed core.  Other work can use the CPUs of the slots, so each row is noisy\n");
    }
    if (spread.count() < 2) {
        ::fixy::report(::fixy::Sink::Out, "  the bench needs two slots at least, and the run has {}\n", spread.count());
        return 1;
    }
    bool const has_four = spread.count() >= 4;
    ::fixy::report(::fixy::Sink::Out, "\n=== spsc push ===\n");
    print_row(spsc_push_row(spread));
    ::fixy::report(::fixy::Sink::Out, "\n=== spsc pop ===\n");
    print_row(spsc_pop_row(spread));
    ::fixy::report(::fixy::Sink::Out, "\n=== mpsc push: the last thread is the one consumer ===\n");
    print_row(mpsc_push_row(spread, ThreadCount{2}, bench::Run{"mpsc push, 2 threads"}));
    if (has_four) print_row(mpsc_push_row(spread, ThreadCount{4}, bench::Run{"mpsc push, 4 threads"}));
    ::fixy::report(::fixy::Sink::Out, "\n=== mpsc pop: the main thread is the one consumer ===\n");
    print_row(mpsc_pop_row(spread, ThreadCount{2}, bench::Run{"mpsc pop, 2 threads"}));
    if (has_four) print_row(mpsc_pop_row(spread, ThreadCount{4}, bench::Run{"mpsc pop, 4 threads"}));
    return 0;
}

}  // namespace

int main() { return run_bench(); }
