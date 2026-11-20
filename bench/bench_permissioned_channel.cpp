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
#include <fixy/concurrent/PermissionedChannel.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Linear.h>
#include <foundation/core/Record.h>
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

// The options of the worker of a slot: its name and the CPU of the slot.
[[nodiscard]] ::fixy::thread::ThreadOptions worker_options(bench::Spread const& spread,
                                                           bench::ThreadSlot slot) noexcept {
    return ::fixy::thread::ThreadOptions{
        .name = "channel-worker",
        .affinity = ::fixy::AffinityMask::single(static_cast<std::uint16_t>(cpu_of(spread, slot).raw())),
    };
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
// they stop when the call stops the row.  The run gives the name of the row.
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

// Runs the measure of the main thread with one worker for each body, on the
// slots from the first slot on, in the order of the bodies.  Each worker runs
// beside the main thread through run_beside, and the continuation of each
// call starts the next worker, so the measure runs in the continuation of the
// last one.  Each worker owns the end of its channel, and it moves the end
// onto its own stack, so no object that the main thread writes shares a
// cache line with it.  A worker reaches the state of the row only through
// atomics.  The call returns when the measure
// returns and each worker has joined.
template <class Measure>
[[nodiscard]] RowResult run_beside_workers(bench::Spread const& /*spread*/, bench::ThreadSlot /*slot*/,
                                           Measure& measure) noexcept {
    return measure();
}

template <class Measure, class Body, class... Rest>
[[nodiscard]] RowResult run_beside_workers(bench::Spread const& spread, bench::ThreadSlot slot, Measure& measure,
                                           Body body, Rest... rest) noexcept {
    bench::ThreadSlot const next{slot.raw() + 1};
    auto values = ::fixy::thread::run_beside(Starter{eff::testing::bg()}, worker_options(spread, slot), std::move(body),
                                             [&spread, next, &measure, ... moved = std::move(rest)]() mutable noexcept {
                                                 return run_beside_workers(spread, next, measure, std::move(moved)...);
                                             })
                      .expect("the thread door starts a worker of a row");
    return std::move(values.at(::foundation::core::index<1>()));
}

// The body of a worker that pops each item of its consumer end until the row
// stops, and then ends the end.
template <class Consumer>
[[nodiscard]] auto make_popper(RowControl& control, Consumer&& consumer) noexcept {
    return [&control, owned = std::move(consumer)]() mutable noexcept {
        auto end = std::move(owned);
        run_worker(control,
                   [&control, &end]() noexcept { pop_one(control, [&end]() noexcept { return end.try_pop(); }); });
        ::foundation::linear::drop(std::move(end));
    };
}

// spsc push: the main thread pushes, and the worker of slot 1 pops.
[[nodiscard]] RowResult spsc_push_row(bench::Spread const& spread) {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::wire, SpscBenchTag>>();
    using Spsc = cc::channel_t<Item, kCapacity, decltype(root)>;
    RowControl control{};
    Spsc channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Spsc::producer_tag, Spsc::consumer_tag>(std::move(root));
    auto producer = channel.producer(std::move(producer_perm));
    Item next = 0;
    auto push = [&control, &producer, &next]() noexcept { push_one(control, producer, ++next); };
    auto measure = [&control, &spread, &push]() noexcept {
        return measure_main(control, spread, ThreadCount{2}, bench::Run{"spsc push, 2 threads"}, push);
    };
    RowResult result = run_beside_workers(spread, bench::ThreadSlot{1}, measure,
                                          make_popper(control, channel.consumer(std::move(consumer_perm))));
    ::foundation::linear::drop(std::move(producer));
    return result;
}

// spsc pop: the main thread pops, and the worker of slot 1 pushes.
[[nodiscard]] RowResult spsc_pop_row(bench::Spread const& spread) {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::wire, SpscBenchTag>>();
    using Spsc = cc::channel_t<Item, kCapacity, decltype(root)>;
    RowControl control{};
    Spsc channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Spsc::producer_tag, Spsc::consumer_tag>(std::move(root));
    auto consumer = channel.consumer(std::move(consumer_perm));
    auto pop = [&control, &consumer]() noexcept {
        pop_one(control, [&consumer]() noexcept { return consumer.try_pop(); });
    };
    auto measure = [&control, &spread, &pop]() noexcept {
        return measure_main(control, spread, ThreadCount{2}, bench::Run{"spsc pop, 2 threads"}, pop);
    };
    RowResult result = run_beside_workers(
        spread, bench::ThreadSlot{1}, measure,
        [&control, owned = channel.producer(std::move(producer_perm))]() mutable noexcept {
            auto end = std::move(owned);
            Item next = 0;
            run_worker(control, [&control, &end, &next]() noexcept { push_one(control, end, ++next); });
            ::foundation::linear::drop(std::move(end));
        });
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

// The body of a worker that pushes into its producer end until the row
// stops.  The end goes back to the pool of the channel when the body ends.
template <class Producer>
[[nodiscard]] auto make_pusher(RowControl& control, Producer&& producer) noexcept {
    return [&control, owned = std::move(producer)]() mutable noexcept {
        auto end = std::move(owned);
        Item next = 0;
        run_worker(control, [&control, &end, &next]() noexcept { push_one(control, end, ++next); });
    };
}

// mpsc push: the main thread and the workers of slots 1 to Threads - 2 push,
// and the worker of the last slot pops.  The main thread lends the end of
// each producer worker before the workers start.
template <std::uint32_t Threads, std::size_t... Producers>
[[nodiscard]] RowResult mpsc_push_row(bench::Spread const& spread, bench::Run run,
                                      std::index_sequence<Producers...> /*producers*/) {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::merge, MpscBenchTag>>();
    using Mpsc = cc::channel_t<Item, kCapacity, decltype(root)>;
    RowControl control{};
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Mpsc::producer_tag, Mpsc::consumer_tag>(std::move(root));
    Mpsc channel{std::move(producer_root)};
    eff::cap::Nondet const merge = eff::testing::test().nondet;
    Mpsc::ProducerHandle producer = lend(channel);
    Item next = 0;
    auto push = [&control, &producer, &next]() noexcept { push_one(control, producer, ++next); };
    auto measure = [&control, &spread, &run, &push]() noexcept {
        return measure_main(control, spread, ThreadCount{Threads}, std::move(run), push);
    };
    auto popper = [&control, owned = channel.consumer(std::move(consumer_perm)), merge]() mutable noexcept {
        auto end = std::move(owned);
        run_worker(control, [&control, &end, merge]() noexcept {
            pop_one(control, [&end, merge]() noexcept { return end.try_pop(merge); });
        });
        ::foundation::linear::drop(std::move(end));
    };
    return run_beside_workers(spread, bench::ThreadSlot{1}, measure,
                              make_pusher(control, (static_cast<void>(Producers), lend(channel)))...,
                              std::move(popper));
}

// mpsc pop: the main thread pops, and the workers of slots 1 to Threads - 1
// push.  The main thread lends the end of each worker before the workers
// start.
template <std::uint32_t Threads, std::size_t... Producers>
[[nodiscard]] RowResult mpsc_pop_row(bench::Spread const& spread, bench::Run run,
                                     std::index_sequence<Producers...> /*producers*/) {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::merge, MpscBenchTag>>();
    using Mpsc = cc::channel_t<Item, kCapacity, decltype(root)>;
    RowControl control{};
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Mpsc::producer_tag, Mpsc::consumer_tag>(std::move(root));
    Mpsc channel{std::move(producer_root)};
    auto consumer = channel.consumer(std::move(consumer_perm));
    eff::cap::Nondet const merge = eff::testing::test().nondet;
    auto pop = [&control, &consumer, merge]() noexcept {
        pop_one(control, [&consumer, merge]() noexcept { return consumer.try_pop(merge); });
    };
    auto measure = [&control, &spread, &run, &pop]() noexcept {
        return measure_main(control, spread, ThreadCount{Threads}, std::move(run), pop);
    };
    RowResult result = run_beside_workers(spread, bench::ThreadSlot{1}, measure,
                                          make_pusher(control, (static_cast<void>(Producers), lend(channel)))...);
    ::foundation::linear::drop(std::move(consumer));
    return result;
}

// Prints one row: its threads, the throughput of the main thread, its pause,
// and the report of its latency.
void print_row(RowResult const& row) noexcept {
    bool const is_paused = row.window_pause.is_over_bound();
    bench::print("  {} threads: {}.{}{} Mops/s of the main thread, window pause {} us{}\n", row.threads.raw(),
                 row.centi_mops / 100, row.centi_mops % 100 / 10, row.centi_mops % 10, row.window_pause.own_ns() / 1000,
                 is_paused ? ::fixy::TextView{"  NOT VALID"} : ::fixy::TextView{});
    row.latency.print_text();
}

int run_bench() {
    bench::print_system_info();
    bench::elevate_priority();
    bench::Spread const spread = bench::Spread::of_calling_thread();
    if (spread.source() != bench::SpreadSource::Claim) {
        bench::print("  NOTE: no claimed core.  Other work can use the CPUs of the slots, so each row is noisy\n");
    }
    if (spread.count() < 2) {
        bench::print("  the bench needs two slots at least, and the run has {}\n", spread.count());
        return 1;
    }
    bool const has_four = spread.count() >= 4;
    bench::print("\n=== spsc push ===\n");
    print_row(spsc_push_row(spread));
    bench::print("\n=== spsc pop ===\n");
    print_row(spsc_pop_row(spread));
    bench::print("\n=== mpsc push: the last thread is the one consumer ===\n");
    print_row(mpsc_push_row<2>(spread, bench::Run{"mpsc push, 2 threads"}, std::make_index_sequence<0>{}));
    if (has_four) {
        print_row(mpsc_push_row<4>(spread, bench::Run{"mpsc push, 4 threads"}, std::make_index_sequence<2>{}));
    }
    bench::print("\n=== mpsc pop: the main thread is the one consumer ===\n");
    print_row(mpsc_pop_row<2>(spread, bench::Run{"mpsc pop, 2 threads"}, std::make_index_sequence<1>{}));
    if (has_four) print_row(mpsc_pop_row<4>(spread, bench::Run{"mpsc pop, 4 threads"}, std::make_index_sequence<3>{}));
    return 0;
}

}  // namespace

int main() { return run_bench(); }
