// The rows of the base (THE UNITS of bench_alloc_scaling_rows.h), with the
// box channel rows, which only the base has.

#include <fixy/concurrent/PermissionedBoxChannel.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Linear.h>
#include <foundation/permissions/Permission.h>

#include "bench_alloc_scaling_rows.h"

namespace bench_alloc {

namespace {

namespace perm = ::foundation::permissions;

static_assert(ConstantAdapter<BaseAdapter>, "the base adapter meets the concept of an allocator with a Constant");

// The context that starts the worker of a box channel row: Bg for the
// thread, Alloc for its frame.
using Starter = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;

// The message of the box channel row: 64 bytes, a stamp and its payload.
struct Message {
    std::uint64_t stamp = 0;
    ::fixy::FixedArray<std::uint64_t, 7> payload{};
};

struct BoxRowTag final {};

// The op of the window loop of a box channel row: one push of one Box.
struct BoxPushOp {
    static constexpr std::uint64_t kItemsPerOp = 1;
};

constexpr std::uint64_t kMessageStamp = 0x5354414d50424f58;

// The fatal exit of a message whose stamp is not the stamp of the producer.
[[noreturn]] CRUCIBLE_COLD void refuse_message(std::uint64_t stamp) noexcept {
    ::fixy::fatal("a Box of the box channel row holds the stamp {}, not the stamp of its producer", stamp);
}

// The window of one box channel row: the main thread mints each Box and
// pushes it, and a worker beside it pops each Box and drops it (THE ROWS).
// The worker runs through run_beside of fixy/os/ThreadTasks.h, and its body
// owns the consumer end of the channel.
[[nodiscard]] RowResult run_box_channel_row(bench::Spread const& spread, Placement placement,
                                            bench::CpuId consumer_cpu) {
    auto root = perm::mint_permission_root<
        ::fixy::concurrent::channel_tag::Whole<::fixy::concurrent::ChannelForm::wire, BoxRowTag>>();
    using Channel = ::fixy::concurrent::box_channel_t<Message, kMailboxCells, decltype(root)>;
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<typename Channel::producer_tag, typename Channel::consumer_tag>(std::move(root));
    Channel channel{};
    ::fixy::Box<RowControl> boxed = ::fixy::mint_box<RowControl>(bench_alloc());
    RowControl& control = *boxed;
    RowPlaces const places{.spread = spread, .consumer = ::fixy::some(consumer_cpu)};
    StealTicks const steal_before = read_steal(places, ThreadCount{2});
    MemoryReading memory{.start = read_resident()};
    typename Channel::ProducerHandle producer = channel.producer(std::move(producer_perm));
    // Each Box that the producer minted ends in the consumer, or in the
    // producer when the row stopped while the ring was full.
    std::uint64_t minted = 0;
    std::uint64_t kept = 0;
    auto const push = [&control, &producer, &minted, &kept] [[gnu::always_inline]] () noexcept {
        ::fixy::Box<Message> box = ::fixy::mint_box<Message>(bench_alloc(), Message{.stamp = kMessageStamp});
        ++minted;
        ::fixy::Option<::fixy::Box<Message>> rest = producer.try_push(std::move(box));
        while (rest.is_some()) {
            if (read_phase(control) == Phase::Stop) {
                ++kept;
                return;
            }
            CRUCIBLE_SPIN_PAUSE;
            rest = producer.try_push(std::move(rest).expect("the test above found a Box"));
        }
    };
    bench::CpuId const main_cpu = spread.of_slot(bench::kMeasuringSlot).expect("a row has the slot of the main thread");
    auto measure = [&control, &push, &memory, main_cpu, placement]() noexcept {
        while (control.ready.value.load_acquire() < 1)
            CRUCIBLE_SPIN_PAUSE;
        Window const window = run_window_loop<BoxPushOp>(control, push, main_cpu);
        memory.steady = read_resident();
        ::fixy::FixedArray<double, bench::kWindows> window_mops{};
        std::uint64_t all_items = 0;
        std::uint64_t all_wall_ns = 0;
        for (WindowIndex const index : window.items.indices()) {
            std::uint64_t const wall_ns = window.wall_ns.at(index);
            all_items += window.items.at(index);
            all_wall_ns += wall_ns;
            window_mops.at(index) =
                wall_ns == 0 ? 0.0
                             : static_cast<double>(window.items.at(index)) * 1000.0 / static_cast<double>(wall_ns);
        }
        RowResult result{.threads = ThreadCount{2},
                         .placement = placement,
                         .centi_mops = all_wall_ns == 0 ? 0 : all_items * 100000 / all_wall_ns,
                         .windows = bench::spread_of_windows(window_mops.view()),
                         .paused_windows = bench::detail::paused_windows_of(window.lost_ns.view(), window.pause.basis),
                         .window_pause = window.pause};
        for (::fixy::Index<kLatencyReports> const report : kLatencyBatches.indices()) {
            std::size_t const batch = kLatencyBatches.at(report);
            ::fixy::FixedText<64> detail{};
            if (batch == 1) {
                static_cast<void>(::fixy::format(detail, "latency of one push, 2 threads, {}", name_of(placement))
                                      .expect("the detail of a report fits in 64 characters"));
            } else {
                static_cast<void>(::fixy::format(detail, "mean of {} pushes, 2 threads, {}", batch, name_of(placement))
                                      .expect("the detail of a report fits in 64 characters"));
            }
            result.latency.at(report) = run_of(AllocatorKind::base, RowKind::box_channel, detail.view())
                                            .core(main_cpu.raw())
                                            .batch(batch)
                                            .samples(kLatencySamples)
                                            .warmup(kLatencyWarmup)
                                            .max_wall_ms(kLatencyMs)
                                            .measure([&push] { push(); });
        }
        control.phase.phase.store_release(Phase::Stop);
        return result;
    };
    // The worker owns the consumer end: it pops each Box and drops it until
    // the row stops, then it takes each Box that the ring still holds.  It
    // gives the count of the Box values that it dropped.
    auto popper = [&control, owned = channel.consumer(std::move(consumer_perm))]() mutable noexcept {
        auto end = std::move(owned);
        std::uint64_t dropped = 0;
        static_cast<void>(control.ready.value.fetch_add_acq_rel(1));
        auto const drop = [&dropped](::fixy::Box<Message>&& box) noexcept {
            if (box->stamp != kMessageStamp) [[unlikely]]
                refuse_message(box->stamp);
            ::fixy::Box<Message> const ended{std::move(box)};
            ++dropped;
        };
        while (read_phase(control) != Phase::Stop) {
            ::fixy::Option<::fixy::Box<Message>> popped = end.try_pop();
            if (popped.is_none()) {
                CRUCIBLE_SPIN_PAUSE;
                continue;
            }
            drop(std::move(popped).expect("the test above found a Box"));
        }
        for (::fixy::Option<::fixy::Box<Message>> popped = end.try_pop(); popped.is_some(); popped = end.try_pop())
            drop(std::move(popped).expect("the loop test found a Box"));
        ::foundation::linear::drop(std::move(end));
        return dropped;
    };
    ::fixy::thread::ThreadOptions const options{
        .name = "alloc-box-pop",
        .affinity = ::fixy::AffinityMask::single(static_cast<std::uint16_t>(consumer_cpu.raw())),
    };
    auto values = ::fixy::thread::run_beside(Starter{eff::testing::bg()}, options, std::move(popper), measure)
                      .expect("the thread door starts the worker of a box channel row");
    std::uint64_t const dropped = values.at(::foundation::core::index<0>());
    RowResult result = std::move(values.at(::foundation::core::index<1>()));
    ::foundation::linear::drop(std::move(producer));
    if (minted != dropped + kept) [[unlikely]]
        ::fixy::fatal("the box channel row minted {} Box values, the consumer dropped {} and the producer kept {}",
                      minted, dropped, kept);
    memory.end = read_resident();
    std::size_t harness = 0;
    for (bench::Report const& report : result.latency)
        harness += sample_bytes_of(report).raw();
    memory.harness = ByteCount{harness};
    result.memory = memory;
    result.steal_ms = steal_ms_between(::fixy::side_as<bench::side::ReadingBefore>(steal_before),
                                       ::fixy::side_as<bench::side::ReadingAfter>(read_steal(places, ThreadCount{2})));
    return result;
}

}  // namespace

void run_box_channel_rows(bench::Spread const& spread, RunRecord& record) {
    bench::detail::flush_standard_output();
    ::fixy::report(::fixy::Sink::Out,
                   "\n=== base box-channel: a Box of 64 bytes across a PermissionedBoxChannel ===\n");
    ::fixy::FixedList<RowResult, kMaxRows> rows{};
    ::fixy::FixedArray<Placement, 2> const placements{Placement::same_l3, Placement::other_l3};
    for (Placement const placement : placements) {
        ::fixy::Option<bench::CpuId> consumer = consumer_cpu_of(spread, placement);
        if (consumer.is_none()) continue;
        static_cast<void>(rows.append(run_box_channel_row(spread, placement,
                                                          std::move(consumer).expect("the test above found a CPU")))
                              .expect("a workload has at most kMaxRows rows"));
    }
    if (rows.size() == 0) return;
    using BoxRow = Pair<BaseAdapter>;
    EfficiencyBase const base{.producers = 1,
                              .centi_mops = rows.get(0).expect("the test above found a row")->centi_mops};
    for (RowResult& row : rows) {
        print_row<BoxRow>(row, base);
        double const item_ns = row.windows.median > 0.0 ? 1000.0 / row.windows.median : 0.0;
        add_summary(record, Summary{.allocator = AllocatorKind::base,
                                    .row = RowKind::box_channel,
                                    .threads = row.threads,
                                    .placement = row.placement,
                                    .item_ns = item_ns});
        for (bench::Report& report : row.latency)
            bench::add_report(record.reports, std::move(report));
    }
}

void run_base_rows(bench::Spread const& spread, Arguments const& arguments, RunRecord& record) {
    run_allocator<BaseAdapter>(spread, arguments, record);
}

}  // namespace bench_alloc
