// Tests of the end of the stream of fixy/concurrent/ChannelEnd.h, on the SPSC
// and the MPSC channel, on the run of one stage and in a pipeline with no
// sentinel.
//
// A drain loop here stops on NoItem::Closed.  Each producer sets a flag of the
// test after its handle ended.  A consumer reads that flag before each
// receive, and an Empty after a set flag is a failure: the consumer would wait
// for an item that no producer can push.  So a defect of the end gives a
// failure with its text, and no wait that does not end.  The tsan preset runs
// the same binary.
//
// Each test mints the roots of its channels in its own body, so each brand
// stays in the scope that made it.  test_channel_identity opens an SPSC
// channel again from a second root of one site.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/ChannelEnd.h>
#include <fixy/concurrent/PermissionedMpscChannel.h>
#include <fixy/concurrent/PermissionedSpscChannel.h>
#include <fixy/concurrent/Pipeline.h>
#include <fixy/concurrent/Stage.h>
#include <fixy/os/ThreadTasks.h>

#include <foundation/Linear.h>
#include <foundation/Platform.h>
#include <foundation/core/Array.h>
#include <foundation/core/Atomic.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

namespace cc = ::fixy::concurrent;
namespace eff = ::foundation::effects;
namespace perm = ::foundation::permissions;
using ::foundation::core::Atomic;

struct SpscTag {};
struct MpscTag {};

// The code of a receive: the item, which is not negative in each test, or
// one of these two codes.
constexpr int kEmpty = -1;
constexpr int kClosed = -2;

template <class Received>
[[nodiscard]] int code_of(Received&& received) noexcept {
    return std::forward<Received>(received).match(
        [](int item) noexcept { return item; },
        [](cc::NoItem why) noexcept { return why == cc::NoItem::Empty ? kEmpty : kClosed; });
}

// Zero when the code is the expected code.  Else it reports the step and
// gives one.
[[nodiscard]] int expect_code(::fixy::TextView what, int got, int want) noexcept {
    if (got == want) return 0;
    ::fixy::report(::fixy::Sink::Err, "{}: the receive gave {}, want {} (-1 is Empty, -2 is Closed)\n", what, got,
                   want);
    return 1;
}

// A producer handle that the pool of the channel lends.  The pool lends
// unless a drained access runs, and no test lends then, so an empty lend
// ends the process.
template <class Channel>
[[nodiscard]] typename Channel::ProducerHandle lend(Channel& channel) noexcept {
    return channel.producer().expect("the channel lends a producer while no exclusive is out");
}

template <class Producer>
void push_or_spin(Producer& producer, int item) noexcept {
    while (!producer.try_push(item)) {
        CRUCIBLE_SPIN_PAUSE;
    }
}

// Moves the handle into a new handle, so the argument ends as a moved-from
// handle after the call.
template <class Handle>
[[nodiscard]] Handle take_over(Handle&& handle) noexcept {
    Handle moved{std::move(handle)};
    return moved;
}

// ── One thread ───────────────────────────────────────────────────────────

// A fresh channel is open.  The end of the producer closes it after its
// items.  A release keeps the channel open, and a moved-from handle ends with
// no effect.
[[nodiscard]] int spsc_close_and_release() noexcept {
    auto root = perm::mint_permission_root<cc::spsc_tag::Whole<SpscTag>>();
    using Spsc = cc::spsc_channel_t<int, 8, decltype(root)>;
    Spsc channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Spsc::producer_tag, Spsc::consumer_tag>(std::move(root));
    auto consumer = channel.consumer(std::move(consumer_perm));
    int failures = expect_code("a fresh SPSC channel", code_of(consumer.try_receive()), kEmpty);
    {
        auto producer = channel.producer(std::move(producer_perm));
        for (int item = 0; item < 3; ++item) {
            push_or_spin(producer, item);
        }
        auto token = std::move(producer).release();
        failures += expect_code("a channel whose producer gave its token back", code_of(consumer.try_receive()), 0);
        failures += expect_code("the second item after the release", code_of(consumer.try_receive()), 1);
        failures += expect_code("the third item after the release", code_of(consumer.try_receive()), 2);
        failures +=
            expect_code("an empty channel whose producer gave its token back", code_of(consumer.try_receive()), kEmpty);

        // The temporary handle ends as a moved-from handle at the end of the
        // statement, and the target lives on.
        auto target = take_over(channel.producer(std::move(token)));
        failures +=
            expect_code("a channel after the end of a moved-from handle", code_of(consumer.try_receive()), kEmpty);
        push_or_spin(target, 8);
        ::foundation::linear::drop(std::move(target));
    }
    failures += expect_code("the item before the end of the last handle", code_of(consumer.try_receive()), 8);
    failures += expect_code("a receive after the last item", code_of(consumer.try_receive()), kClosed);
    failures += expect_code("a second receive after the last item", code_of(consumer.try_receive()), kClosed);
    if (!channel.is_closed()) {
        ::fixy::report(::fixy::Sink::Err, "the SPSC channel is not closed after its producer ended\n");
        ++failures;
    }
    ::foundation::linear::drop(std::move(consumer));
    return failures;
}

// The last of two producers closes the channel.  A producer that ends while
// a different producer lives leaves it open.  A lend after the end opens it
// again, and a drained access keeps the state.
[[nodiscard]] int mpsc_last_producer_closes() noexcept {
    auto root = perm::mint_permission_root<cc::mpsc_tag::Whole<MpscTag>>();
    using Mpsc = cc::mpsc_channel_t<int, 8, decltype(root)>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Mpsc::producer_tag, Mpsc::consumer_tag>(std::move(root));
    Mpsc channel{std::move(producer_root)};
    auto consumer = channel.consumer(std::move(consumer_perm));
    // A pop of an MPSC channel is a merge, so it takes the tag of Nondet.
    const eff::cap::Nondet merge = eff::testing::test().nondet;
    int failures = expect_code("a fresh MPSC channel", code_of(consumer.try_receive(merge)), kEmpty);
    {
        auto second = lend(channel);
        {
            auto first = lend(channel);
            push_or_spin(first, 1);
        }
        push_or_spin(second, 2);
        failures += expect_code("the item of the first producer", code_of(consumer.try_receive(merge)), 1);
        failures += expect_code("the item of the second producer", code_of(consumer.try_receive(merge)), 2);
        failures += expect_code("a channel with one live producer", code_of(consumer.try_receive(merge)), kEmpty);
    }
    failures += expect_code("a channel after its last producer", code_of(consumer.try_receive(merge)), kClosed);

    bool has_run = false;
    if (!channel.with_drained_access([&has_run] { has_run = true; }) || !has_run) {
        ::fixy::report(::fixy::Sink::Err, "the drained access did not run with no producer out\n");
        ++failures;
    }
    failures += expect_code("a closed channel after a drained access", code_of(consumer.try_receive(merge)), kClosed);
    {
        auto later = lend(channel);
        failures += expect_code("a channel that a later lend opened", code_of(consumer.try_receive(merge)), kEmpty);
        push_or_spin(later, 3);
    }
    failures += expect_code("the item of the later producer", code_of(consumer.try_receive(merge)), 3);
    failures += expect_code("a channel after the later producer", code_of(consumer.try_receive(merge)), kClosed);
    ::foundation::linear::drop(std::move(consumer));
    return failures;
}

// A consumer of a stage that never gives an item.  It is not linear, so the
// body below holds it with no consume.
struct IdleChannel {};
struct IdleConsumer {
    using channel_type = IdleChannel;
    [[nodiscard]] ::fixy::Option<int> try_pop() noexcept { return ::fixy::none; }
};

// The body of one stage: it pushes one item through the producer that the
// run gives, and it does not move the producer.  An MPSC producer handle is
// not linear, so the body holds it with no consume.
template <class In, class Out>
void push_one_item(In&&, Out&& out) noexcept {
    push_or_spin(out, 5);
}

// The run of a stage ends the handles of the stage when the body returns, so
// the channel ahead closes before the stage object ends.
[[nodiscard]] int stage_run_ends_its_handles() noexcept {
    const ::fixy::HotFgCtx ctx = eff::testing::foreground();
    auto root = perm::mint_permission_root<cc::mpsc_tag::Whole<MpscTag>>();
    using OutChannel = cc::mpsc_channel_t<int, 8, decltype(root)>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<OutChannel::producer_tag, OutChannel::consumer_tag>(std::move(root));
    OutChannel out_channel{std::move(producer_root)};
    auto consumer = out_channel.consumer(std::move(consumer_perm));

    auto stage = cc::mint_stage<&push_one_item<IdleConsumer, OutChannel::ProducerHandle>>(ctx, IdleConsumer{},
                                                                                          lend(out_channel));
    std::move(stage).run();
    const eff::cap::Nondet merge = eff::testing::test().nondet;
    int failures = expect_code("the item of the stage", code_of(consumer.try_receive(merge)), 5);
    failures +=
        expect_code("the channel after the run of the stage returned", code_of(consumer.try_receive(merge)), kClosed);
    ::foundation::linear::drop(std::move(consumer));
    return failures;
}

// ── Threads, in many rounds ──────────────────────────────────────────────

constexpr int kRounds = 400;
constexpr int kItemsOfProducer = 64;
constexpr int kMpscProducers = 3;

// The producer of one round, on a thread of its own: it pushes base + i for
// each i, in order, ends its handle at the drop door, and counts its end.
// Brand is the fresh brand of the Ref of the count.
template <class Producer, class Brand>
struct RoundFeeder {
    Producer handle;
    ::fixy::Ref<Atomic<int>, Brand> ended_producers;
    int base = 0;

    RoundFeeder(Producer&& producer, ::fixy::Ref<Atomic<int>, Brand> ended, int first_item) noexcept
        : handle{std::move(producer)}, ended_producers{ended}, base{first_item} {}

    // A linear handle ends at the drop door, and an MPSC producer handle,
    // which is not linear, ends with its scope.
    void operator()() noexcept {
        {
            Producer local = std::move(handle);
            for (int item = 0; item < kItemsOfProducer; ++item) {
                push_or_spin(local, base + item);
            }
            if constexpr (::foundation::linear::is_linear_v<Producer>) {
                ::foundation::linear::drop(std::move(local));
            }
        }
        static_cast<void>(ended_producers->fetch_add_acq_rel(1));
    }
};

// The receive of one consumer.  The receive of a merge pole takes the tag of
// Nondet, which a test context gives.
template <class Consumer>
[[nodiscard]] auto receive_from(Consumer& in) noexcept {
    if constexpr (cc::is_merge_consumer_handle_v<Consumer>) {
        return in.try_receive(eff::testing::test().nondet);
    } else {
        return in.try_receive();
    }
}

// The consumer of one round, on a thread of its own: it receives each item
// until Closed and checks the order of the items of each producer.  An Empty
// after the end of each producer is a failure, and the drain then stops.
// Brand is the fresh brand of the Ref of the count.
template <class Consumer, class Brand>
struct RoundDrain {
    Consumer handle;
    ::fixy::Ref<Atomic<int>, Brand> ended_producers;
    int producers = 0;
    ::foundation::core::FixedArray<int, kMpscProducers> next_of_producer{};
    int received = 0;
    int failures = 0;

    RoundDrain(Consumer&& consumer, ::fixy::Ref<Atomic<int>, Brand> ended, int producer_count) noexcept
        : handle{std::move(consumer)}, ended_producers{ended}, producers{producer_count} {}

    void operator()() noexcept {
        bool is_draining = true;
        while (is_draining) {
            bool const had_ended = ended_producers->load_acquire() == producers;
            int const code = code_of(receive_from(handle));
            if (code == kClosed) {
                is_draining = false;
            } else if (code == kEmpty) {
                if (had_ended) {
                    ::fixy::report(::fixy::Sink::Err, "the consumer saw Empty after its producers ended\n");
                    ++failures;
                    is_draining = false;
                }
                CRUCIBLE_SPIN_PAUSE;
            } else {
                int const producer = code / kItemsOfProducer;
                if (producer >= producers) ++failures;
                for (auto const& slot : next_of_producer.view().get(static_cast<std::size_t>(producer))) {
                    if (*slot != code % kItemsOfProducer) ++failures;
                    ++*slot;
                }
                ++received;
            }
        }
    }
};

// Zero when the drain received each item of each producer.  Else it reports
// the count and gives the failures.
template <class Drain>
[[nodiscard]] int expect_drained(::fixy::TextView what, Drain const& drain, int producers) noexcept {
    int failures = drain.failures;
    if (drain.received != producers * kItemsOfProducer) {
        ::fixy::report(::fixy::Sink::Err, "{}: the consumer received {} items before Closed, want {}\n", what,
                       drain.received, producers * kItemsOfProducer);
        ++failures;
    }
    return failures;
}

// One SPSC round: the producer pushes its items and ends, and the consumer
// receives each item, in order, before Closed.
[[nodiscard]] int spsc_round() noexcept {
    auto root = perm::mint_permission_root<cc::spsc_tag::Whole<SpscTag>>();
    using Spsc = cc::spsc_channel_t<int, 8, decltype(root)>;
    Spsc channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Spsc::producer_tag, Spsc::consumer_tag>(std::move(root));
    Atomic<int> ended_producers{0};
    RoundFeeder feed{channel.producer(std::move(producer_perm)), ::foundation::core::to(ended_producers), 0};
    RoundDrain drain{channel.consumer(std::move(consumer_perm)), ::foundation::core::to(ended_producers), 1};
    ::fixy::thread::run_joined(::fixy::BgDrainCtx{eff::testing::bg()}, ::fixy::thread::ThreadOptions{}, feed, drain)
        .expect("the test starts the producer thread and the consumer thread");
    int const failures = expect_drained("an SPSC round", drain, 1);
    ::foundation::linear::drop(std::move(feed));
    ::foundation::linear::drop(std::move(drain));
    return failures;
}

// One MPSC round: three producers push their items and end, and the
// consumer receives each item, in the order of each producer, before Closed.
[[nodiscard]] int mpsc_round() noexcept {
    auto root = perm::mint_permission_root<cc::mpsc_tag::Whole<MpscTag>>();
    using Mpsc = cc::mpsc_channel_t<int, 8, decltype(root)>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Mpsc::producer_tag, Mpsc::consumer_tag>(std::move(root));
    Mpsc channel{std::move(producer_root)};
    Atomic<int> ended_producers{0};
    RoundFeeder first{lend(channel), ::foundation::core::to(ended_producers), 0};
    RoundFeeder second{lend(channel), ::foundation::core::to(ended_producers), kItemsOfProducer};
    RoundFeeder third{lend(channel), ::foundation::core::to(ended_producers), 2 * kItemsOfProducer};
    RoundDrain drain{channel.consumer(std::move(consumer_perm)), ::foundation::core::to(ended_producers),
                     kMpscProducers};
    ::fixy::thread::run_joined(::fixy::BgDrainCtx{eff::testing::bg()}, ::fixy::thread::ThreadOptions{}, first, second,
                               third, drain)
        .expect("the test starts the producer threads and the consumer thread");
    int const failures = expect_drained("an MPSC round", drain, kMpscProducers);
    ::foundation::linear::drop(std::move(drain));
    return failures;
}

[[nodiscard]] int rounds_receive_each_item_before_closed() noexcept {
    for (int round = 0; round < kRounds; ++round) {
        if (int const failures = spsc_round() + mpsc_round(); failures != 0) {
            ::fixy::report(::fixy::Sink::Err, "round {} of {} had {} failures\n", round, kRounds, failures);
            return failures;
        }
    }
    return 0;
}

// ── A pipeline with no sentinel ──────────────────────────────────────────

struct StartTag {};
struct LinkTag {};
struct FanTag {};
struct SinkTag {};

// The flags and the counts of the run.  A stage sets its flag after the end
// of its producer handle, and the next stage reads it.
Atomic<bool> has_source_ended{false};
Atomic<bool> has_middle_ended{false};
Atomic<int> sink_received{0};
Atomic<int> stage_failures{0};

constexpr int kPipelineItems = 64;

// Receives each item of in until Closed, and gives each one to forward.  An
// Empty after the upstream flag is a failure, and the drain then stops.
template <class Consumer, class Forward>
void drain_until_closed(Consumer& in, Atomic<bool> const& has_upstream_ended, ::fixy::TextView stage,
                        Forward forward) noexcept {
    for (;;) {
        bool const had_ended = has_upstream_ended.load_acquire();
        int const code = code_of(receive_from(in));
        if (code == kClosed) return;
        if (code == kEmpty) {
            if (had_ended) {
                ::fixy::report(::fixy::Sink::Err, "the {} stage saw Empty after its upstream stage ended\n", stage);
                static_cast<void>(stage_failures.fetch_add_acq_rel(1));
                return;
            }
            CRUCIBLE_SPIN_PAUSE;
            continue;
        }
        forward(code);
    }
}

// The start channel ends before the run, so its drain ends at once.  The
// source then pushes its items and ends its handle at the drop door, with no
// last message.
template <class In, class Out>
void threaded_source(In&& start, Out&& out) noexcept {
    Atomic<bool> const is_started{true};
    drain_until_closed(start, is_started, "source", [](int) noexcept {});
    ::foundation::linear::drop(std::move(start));
    for (int item = 0; item < kPipelineItems; ++item) {
        push_or_spin(out, item);
    }
    ::foundation::linear::drop(std::move(out));
    has_source_ended.store_release(true);
}

// The fan channel is an MPSC channel, so the producer of the middle stage is
// not linear and ends when its local does.
template <class In, class Out>
void threaded_middle(In&& in, Out&& out) noexcept {
    {
        Out producer = std::move(out);
        drain_until_closed(in, has_source_ended, "middle",
                           [&producer](int item) noexcept { push_or_spin(producer, item); });
    }
    ::foundation::linear::drop(std::move(in));
    has_middle_ended.store_release(true);
}

template <class In, class Out>
void threaded_sink(In&& in, Out&& out) noexcept {
    drain_until_closed(in, has_middle_ended, "sink", [](int item) noexcept {
        if (item != sink_received.load_acquire()) static_cast<void>(stage_failures.fetch_add_acq_rel(1));
        static_cast<void>(sink_received.fetch_add_acq_rel(1));
    });
    ::foundation::linear::drop(std::move(in));
    ::foundation::linear::drop(std::move(out));
}

// A threaded pipeline of three stages with no sentinel.  Each stage ends when
// its upstream channel closes, so the join ends.  The channel after the sink
// closes when the sink ends.
[[nodiscard]] int threaded_pipeline_without_sentinel() noexcept {
    const ::fixy::HotFgCtx ctx = eff::testing::foreground();
    const ::fixy::BgDrainCtx coordinator{eff::testing::bg()};
    auto start_root = perm::mint_permission_root<cc::spsc_tag::Whole<StartTag>>();
    auto link_root = perm::mint_permission_root<cc::spsc_tag::Whole<LinkTag>>();
    auto fan_root = perm::mint_permission_root<cc::mpsc_tag::Whole<FanTag>>();
    auto sink_root = perm::mint_permission_root<cc::spsc_tag::Whole<SinkTag>>();
    using StartChannel = cc::spsc_channel_t<int, 8, decltype(start_root)>;
    using LinkChannel = cc::spsc_channel_t<int, 8, decltype(link_root)>;
    using FanChannel = cc::mpsc_channel_t<int, 8, decltype(fan_root)>;
    using SinkChannel = cc::spsc_channel_t<int, 8, decltype(sink_root)>;

    StartChannel start{};
    LinkChannel link{};
    auto [fan_producer_root, fan_consumer_perm] =
        perm::mint_permission_split<FanChannel::producer_tag, FanChannel::consumer_tag>(std::move(fan_root));
    FanChannel fan{std::move(fan_producer_root)};
    SinkChannel sink{};
    auto [start_producer_perm, start_consumer_perm] =
        perm::mint_permission_split<StartChannel::producer_tag, StartChannel::consumer_tag>(std::move(start_root));
    auto [link_producer_perm, link_consumer_perm] =
        perm::mint_permission_split<LinkChannel::producer_tag, LinkChannel::consumer_tag>(std::move(link_root));
    auto [sink_producer_perm, sink_consumer_perm] =
        perm::mint_permission_split<SinkChannel::producer_tag, SinkChannel::consumer_tag>(std::move(sink_root));

    // The start channel opens and closes before the run.
    ::foundation::linear::drop(start.producer(std::move(start_producer_perm)));
    auto sink_consumer = sink.consumer(std::move(sink_consumer_perm));

    auto source_stage =
        cc::mint_stage<&threaded_source<typename StartChannel::ConsumerHandle, typename LinkChannel::ProducerHandle>>(
            ctx, start.consumer(std::move(start_consumer_perm)), link.producer(std::move(link_producer_perm)));
    auto middle_stage =
        cc::mint_stage<&threaded_middle<typename LinkChannel::ConsumerHandle, typename FanChannel::ProducerHandle>>(
            ctx, link.consumer(std::move(link_consumer_perm)), lend(fan));
    auto sink_stage =
        cc::mint_stage<&threaded_sink<typename FanChannel::ConsumerHandle, typename SinkChannel::ProducerHandle>>(
            ctx, fan.consumer(std::move(fan_consumer_perm)), sink.producer(std::move(sink_producer_perm)));
    auto pipeline =
        cc::mint_pipeline(coordinator, std::move(source_stage), std::move(middle_stage), std::move(sink_stage));
    int failures = 0;
    if (pipeline.will_run_inline()) {
        ::fixy::report(::fixy::Sink::Err, "the pipeline of drain loops runs inline\n");
        ++failures;
    }
    std::move(pipeline).run(coordinator);

    failures += stage_failures.load_acquire();
    if (sink_received.load_acquire() != kPipelineItems) {
        ::fixy::report(::fixy::Sink::Err, "the sink received {} items, want {}\n", sink_received.load_acquire(),
                       kPipelineItems);
        ++failures;
    }
    failures += expect_code("the channel after the sink", code_of(sink_consumer.try_receive()), kClosed);
    ::foundation::linear::drop(std::move(sink_consumer));
    return failures;
}

}  // namespace

int main() {
    int failures = 0;
    failures += spsc_close_and_release();
    failures += mpsc_last_producer_closes();
    failures += stage_run_ends_its_handles();
    failures += rounds_receive_each_item_before_closed();
    failures += threaded_pipeline_without_sentinel();
    return failures == 0 ? 0 : 1;
}
