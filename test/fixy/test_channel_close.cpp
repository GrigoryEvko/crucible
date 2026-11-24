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
//
// Each handle lives in the body of a frame door.  A case that reads the
// channel after the end of a producer ends that producer in the body, so the
// read runs in the frame.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/ChannelEnd.h>
#include <fixy/concurrent/PermissionedChannel.h>
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

// The value of the producer door of a merge.  The pool lends unless a
// drained access runs, and no test lends then, so an empty lend ends the
// process.
template <class Value>
[[nodiscard]] Value expect_lent(::fixy::Option<Value> lent) noexcept {
    return std::move(lent).expect("the channel lends a producer while no exclusive is out");
}

template <class Producer>
void push_or_spin(Producer& producer, int item) noexcept {
    while (!producer.try_push(item)) {
        CRUCIBLE_SPIN_PAUSE;
    }
}

// Moves the handle into a new handle.  The parameter ends as a moved-from
// handle before the next statement.
template <class Handle>
[[nodiscard]] Handle take_over(Handle handle) noexcept {
    Handle moved{std::move(handle)};
    return moved;
}

// Ends a producer handle of a merge, which is not linear, as the end of its
// scope does.  The parameter takes the handle and ends before the next
// statement.
template <class Producer>
void end_producer(Producer /*ended*/) noexcept {}

// ── One thread ───────────────────────────────────────────────────────────

// A fresh channel is open.  The end of the producer closes it after its
// items.  A release keeps the channel open, and a moved-from handle ends with
// no effect.  The consumer moves into the frame of the last producer, which
// reads the end of the stream after the end of that producer.
[[nodiscard]] int spsc_close_and_release() noexcept {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::wire, SpscTag>>();
    using Spsc = cc::channel_t<int, 8, decltype(root)>;
    Spsc channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Spsc::producer_tag, Spsc::consumer_tag>(std::move(root));
    return cc::with_consumer(
        channel, std::move(consumer_perm),
        [&channel, producer_perm = std::move(producer_perm)](auto consumer) mutable noexcept {
            int failures = expect_code("a fresh SPSC channel", code_of(consumer.try_receive()), kEmpty);
            auto token = cc::with_producer(channel, std::move(producer_perm), [](auto producer) noexcept {
                for (int item = 0; item < 3; ++item) {
                    push_or_spin(producer, item);
                }
                return std::move(producer).release();
            });
            failures += expect_code("a channel whose producer gave its token back", code_of(consumer.try_receive()), 0);
            failures += expect_code("the second item after the release", code_of(consumer.try_receive()), 1);
            failures += expect_code("the third item after the release", code_of(consumer.try_receive()), 2);
            failures += expect_code("an empty channel whose producer gave its token back",
                                    code_of(consumer.try_receive()), kEmpty);

            failures += cc::with_producer(
                channel, std::move(token), [&channel, consumer = std::move(consumer)](auto producer) mutable noexcept {
                    // The parameter of take_over ends as a moved-from handle,
                    // and the target lives on.
                    auto target = take_over(std::move(producer));
                    int last = expect_code("a channel after the end of a moved-from handle",
                                           code_of(consumer.try_receive()), kEmpty);
                    push_or_spin(target, 8);
                    ::foundation::linear::drop(std::move(target));
                    last +=
                        expect_code("the item before the end of the last handle", code_of(consumer.try_receive()), 8);
                    last += expect_code("a receive after the last item", code_of(consumer.try_receive()), kClosed);
                    last +=
                        expect_code("a second receive after the last item", code_of(consumer.try_receive()), kClosed);
                    if (!channel.is_closed()) {
                        ::fixy::report(::fixy::Sink::Err, "the SPSC channel is not closed after its producer ended\n");
                        ++last;
                    }
                    ::foundation::linear::drop(std::move(consumer));
                    return last;
                });
            return failures;
        });
}

// The last of two producers closes the channel.  A producer that ends while
// a different producer lives leaves it open.  A lend after the end opens it
// again, and a withdrawal and a deposit of the producer root keep the state.
// The consumer moves into the frame of the second producer and then into the
// frame of the later one, and each of them ends its producer before the read
// of the end.
[[nodiscard]] int mpsc_last_producer_closes() noexcept {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::merge, MpscTag>>();
    using Mpsc = cc::channel_t<int, 8, decltype(root)>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Mpsc::producer_tag, Mpsc::consumer_tag>(std::move(root));
    Mpsc channel{std::move(producer_root)};
    // A pop of an MPSC channel is a merge, so it takes the tag of Nondet.
    const eff::cap::Nondet merge = eff::testing::test().nondet;
    return cc::with_consumer(channel, std::move(consumer_perm), [&channel, merge](auto consumer) noexcept {
        int failures = expect_code("a fresh MPSC channel", code_of(consumer.try_receive(merge)), kEmpty);
        failures += expect_lent(cc::with_producer(
            channel, merge, [&channel, merge, consumer = std::move(consumer)](auto second) mutable noexcept {
                static_cast<void>(expect_lent(
                    cc::with_producer(channel, merge, [](auto first) noexcept { push_or_spin(first, 1); })));
                push_or_spin(second, 2);
                int steps = expect_code("the item of the first producer", code_of(consumer.try_receive(merge)), 1);
                steps += expect_code("the item of the second producer", code_of(consumer.try_receive(merge)), 2);
                steps += expect_code("a channel with one live producer", code_of(consumer.try_receive(merge)), kEmpty);
                end_producer(std::move(second));
                steps +=
                    expect_code("a channel after its last producer", code_of(consumer.try_receive(merge)), kClosed);

                channel.deposit_producer_root(
                    channel.try_withdraw_producer_root(merge).expect("the root comes out with no producer out"));
                steps += expect_code("a closed channel after the root came back", code_of(consumer.try_receive(merge)),
                                     kClosed);
                steps += expect_lent(cc::with_producer(
                    channel, merge, [merge, consumer = std::move(consumer)](auto later) mutable noexcept {
                        int later_steps = expect_code("a channel that a later lend opened",
                                                      code_of(consumer.try_receive(merge)), kEmpty);
                        push_or_spin(later, 3);
                        end_producer(std::move(later));
                        later_steps +=
                            expect_code("the item of the later producer", code_of(consumer.try_receive(merge)), 3);
                        later_steps += expect_code("a channel after the later producer",
                                                   code_of(consumer.try_receive(merge)), kClosed);
                        ::foundation::linear::drop(std::move(consumer));
                        return later_steps;
                    }));
                return steps;
            }));
        return failures;
    });
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
// the channel ahead closes before the stage object ends.  The consumer reads
// the channel in the frame of the producer, after the run and before the end
// of the stage object.
[[nodiscard]] int stage_run_ends_its_handles() noexcept {
    const ::fixy::HotFgCtx ctx = eff::testing::foreground();
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::merge, MpscTag>>();
    using OutChannel = cc::channel_t<int, 8, decltype(root)>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<OutChannel::producer_tag, OutChannel::consumer_tag>(std::move(root));
    OutChannel out_channel{std::move(producer_root)};
    const eff::cap::Nondet merge = eff::testing::test().nondet;
    return cc::with_consumer(
        out_channel, std::move(consumer_perm), [&ctx, &out_channel, merge](auto consumer) noexcept {
            return expect_lent(cc::with_producer(
                out_channel, merge, [&ctx, merge, consumer = std::move(consumer)](auto producer) mutable noexcept {
                    auto stage = cc::mint_stage<&push_one_item<IdleConsumer, decltype(producer)>>(ctx, IdleConsumer{},
                                                                                                  std::move(producer));
                    std::move(stage).run();
                    int failures = expect_code("the item of the stage", code_of(consumer.try_receive(merge)), 5);
                    failures += expect_code("the channel after the run of the stage returned",
                                            code_of(consumer.try_receive(merge)), kClosed);
                    ::foundation::linear::drop(std::move(consumer));
                    return failures;
                }));
        });
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
    // which is not linear, ends with its scope.  The thread calls the feeder
    // one time as an rvalue, so the call operator gives up its handle.
    void operator()() && noexcept {
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

// The count of one drain: the items that it received, and its failures.
struct DrainCount {
    int received = 0;
    int failures = 0;
};

// The consumer of one round, on a thread of its own: it receives each item
// until Closed and checks the order of the items of each producer.  An Empty
// after the end of each producer is a failure, and the drain then stops.  It
// gives back its count, which the caller reads after the join.  Brand is the
// fresh brand of the Ref of the count.
template <class Consumer, class Brand>
struct RoundDrain {
    Consumer handle;
    ::fixy::Ref<Atomic<int>, Brand> ended_producers;
    int producers = 0;
    ::foundation::core::FixedArray<int, kMpscProducers> next_of_producer{};

    RoundDrain(Consumer&& consumer, ::fixy::Ref<Atomic<int>, Brand> ended, int producer_count) noexcept
        : handle{std::move(consumer)}, ended_producers{ended}, producers{producer_count} {}

    // The thread calls the drain one time as an rvalue, and the drain ends
    // its handle at the drop door.
    [[nodiscard]] DrainCount operator()() && noexcept {
        int received = 0;
        int failures = 0;
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
                for (auto const slot : ::fixy::Index<kMpscProducers>::check(static_cast<std::size_t>(producer))) {
                    if (next_of_producer.at(slot) != code % kItemsOfProducer) ++failures;
                    next_of_producer.with_element(slot, [](int& next) noexcept { ++next; });
                }
                ++received;
            }
        }
        ::foundation::linear::drop(std::move(handle));
        return DrainCount{.received = received, .failures = failures};
    }
};

// Zero when the drain received each item of each producer.  Else it reports
// the count and gives the failures.
[[nodiscard]] int expect_drained(::fixy::TextView what, DrainCount const& drain, int producers) noexcept {
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
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::wire, SpscTag>>();
    using Spsc = cc::channel_t<int, 8, decltype(root)>;
    Spsc channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Spsc::producer_tag, Spsc::consumer_tag>(std::move(root));
    Atomic<int> ended_producers{0};
    return cc::with_ends(channel, std::move(producer_perm), std::move(consumer_perm),
                         [&ended_producers](auto producer, auto consumer) noexcept {
                             RoundFeeder feed{std::move(producer), ::foundation::core::to(ended_producers), 0};
                             RoundDrain drain{std::move(consumer), ::foundation::core::to(ended_producers), 1};
                             // The door owns the bodies, and it ends each
                             // linear body after the join.
                             auto const joined =
                                 ::fixy::thread::run_joined(::fixy::BgDrainCtx{eff::testing::bg()},
                                                            ::fixy::thread::ThreadOptions{}, std::move(feed),
                                                            std::move(drain))
                                     .expect("the test starts the producer thread and the consumer thread");
                             return expect_drained("an SPSC round", joined.at(::fixy::index<1>()), 1);
                         });
}

// One MPSC round: three producers push their items and end, and the
// consumer receives each item, in the order of each producer, before Closed.
// The frame of each producer opens in the frame of the one before it, and the
// innermost body takes the consumer and the outer producers by move.
[[nodiscard]] int mpsc_round() noexcept {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::merge, MpscTag>>();
    using Mpsc = cc::channel_t<int, 8, decltype(root)>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Mpsc::producer_tag, Mpsc::consumer_tag>(std::move(root));
    Mpsc channel{std::move(producer_root)};
    Atomic<int> ended_producers{0};
    const eff::cap::Nondet merge = eff::testing::test().nondet;
    return cc::with_consumer(
        channel, std::move(consumer_perm), [&channel, &ended_producers, merge](auto consumer) noexcept {
            return expect_lent(cc::with_producer(
                channel, merge,
                [&channel, &ended_producers, merge,
                 consumer = std::move(consumer)](auto first_producer) mutable noexcept {
                    return expect_lent(cc::with_producer(
                        channel, merge,
                        [&channel, &ended_producers, merge, consumer = std::move(consumer),
                         first_producer = std::move(first_producer)](auto second_producer) mutable noexcept {
                            return expect_lent(cc::with_producer(
                                channel, merge,
                                [&ended_producers, consumer = std::move(consumer),
                                 first_producer = std::move(first_producer),
                                 second_producer = std::move(second_producer)](auto third_producer) mutable noexcept {
                                    RoundFeeder first{std::move(first_producer),
                                                      ::foundation::core::to(ended_producers), 0};
                                    RoundFeeder second{std::move(second_producer),
                                                       ::foundation::core::to(ended_producers), kItemsOfProducer};
                                    RoundFeeder third{std::move(third_producer),
                                                      ::foundation::core::to(ended_producers), 2 * kItemsOfProducer};
                                    RoundDrain drain{std::move(consumer), ::foundation::core::to(ended_producers),
                                                     kMpscProducers};
                                    auto const joined =
                                        ::fixy::thread::run_joined(
                                            ::fixy::BgDrainCtx{eff::testing::bg()}, ::fixy::thread::ThreadOptions{},
                                            std::move(first), std::move(second), std::move(third), std::move(drain))
                                            .expect("the test starts the producer threads and the consumer thread");
                                    return expect_drained("an MPSC round", joined.at(::fixy::index<3>()),
                                                          kMpscProducers);
                                }));
                        }));
                }));
        });
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
    auto start_root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::wire, StartTag>>();
    auto link_root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::wire, LinkTag>>();
    auto fan_root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::merge, FanTag>>();
    auto sink_root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::wire, SinkTag>>();
    using StartChannel = cc::channel_t<int, 8, decltype(start_root)>;
    using LinkChannel = cc::channel_t<int, 8, decltype(link_root)>;
    using FanChannel = cc::channel_t<int, 8, decltype(fan_root)>;
    using SinkChannel = cc::channel_t<int, 8, decltype(sink_root)>;

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
    cc::with_producer(start, std::move(start_producer_perm),
                      [](auto producer) noexcept { ::foundation::linear::drop(std::move(producer)); });

    // The stages, the pipeline and its run.  The innermost frame below calls
    // it with the handle of each end of each channel.  Each handle names the
    // scope of its frame, so each stage is a template over its handles.
    auto const run_stages = [&ctx, &coordinator](auto start_in, auto link_out, auto link_in, auto fan_out, auto fan_in,
                                                 auto sink_out, auto sink_in) noexcept {
        auto source_stage = cc::mint_stage<&threaded_source<decltype(start_in), decltype(link_out)>>(
            ctx, std::move(start_in), std::move(link_out));
        auto middle_stage = cc::mint_stage<&threaded_middle<decltype(link_in), decltype(fan_out)>>(
            ctx, std::move(link_in), std::move(fan_out));
        auto sink_stage = cc::mint_stage<&threaded_sink<decltype(fan_in), decltype(sink_out)>>(ctx, std::move(fan_in),
                                                                                               std::move(sink_out));
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
        failures += expect_code("the channel after the sink", code_of(sink_in.try_receive()), kClosed);
        ::foundation::linear::drop(std::move(sink_in));
        return failures;
    };

    // A frame door of each channel opens its ends, and each inner body takes
    // the handles of the outer frames and the tokens of the inner frames by
    // move.
    return cc::with_ends(
        sink, std::move(sink_producer_perm), std::move(sink_consumer_perm),
        [&, start_consumer_perm = std::move(start_consumer_perm), link_producer_perm = std::move(link_producer_perm),
         link_consumer_perm = std::move(link_consumer_perm),
         fan_consumer_perm = std::move(fan_consumer_perm)](auto sink_out, auto sink_in) mutable noexcept {
            return cc::with_ends(
                link, std::move(link_producer_perm), std::move(link_consumer_perm),
                [&, start_consumer_perm = std::move(start_consumer_perm),
                 fan_consumer_perm = std::move(fan_consumer_perm), sink_out = std::move(sink_out),
                 sink_in = std::move(sink_in)](auto link_out, auto link_in) mutable noexcept {
                    return cc::with_consumer(
                        start, std::move(start_consumer_perm),
                        [&, fan_consumer_perm = std::move(fan_consumer_perm), sink_out = std::move(sink_out),
                         sink_in = std::move(sink_in), link_out = std::move(link_out),
                         link_in = std::move(link_in)](auto start_in) mutable noexcept {
                            return cc::with_consumer(
                                fan, std::move(fan_consumer_perm),
                                [&, sink_out = std::move(sink_out), sink_in = std::move(sink_in),
                                 link_out = std::move(link_out), link_in = std::move(link_in),
                                 start_in = std::move(start_in)](auto fan_in) mutable noexcept {
                                    return expect_lent(cc::with_producer(
                                        fan, eff::testing::test().nondet,
                                        [&, sink_out = std::move(sink_out), sink_in = std::move(sink_in),
                                         link_out = std::move(link_out), link_in = std::move(link_in),
                                         start_in = std::move(start_in),
                                         fan_in = std::move(fan_in)](auto fan_out) mutable noexcept {
                                            return run_stages(std::move(start_in), std::move(link_out),
                                                              std::move(link_in), std::move(fan_out), std::move(fan_in),
                                                              std::move(sink_out), std::move(sink_in));
                                        }));
                                });
                        });
                });
        });
}

// The close door of a producer handle ends the stream before the end of the
// scope of the handle.  The wire producer closes the channel at once.  The
// first of two merge producers leaves the channel open, and the second closes
// it, and each close gives its pool share back at once.
[[nodiscard]] int producers_close_before_their_scope_ends() noexcept {
    int failures = 0;
    {
        auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::wire, SpscTag>>();
        using Wire = cc::channel_t<int, 8, decltype(root)>;
        Wire channel{};
        auto [producer_perm, consumer_perm] =
            perm::mint_permission_split<Wire::producer_tag, Wire::consumer_tag>(std::move(root));
        failures += cc::with_ends(
            channel, std::move(producer_perm), std::move(consumer_perm),
            [&channel](auto producer, auto consumer) noexcept {
                push_or_spin(producer, 4);
                std::move(producer).close();
                int wire_failures = 0;
                if (!channel.is_closed()) {
                    ::fixy::report(::fixy::Sink::Err,
                                   "the wire channel is not closed after the close of its producer\n");
                    ++wire_failures;
                }
                wire_failures += expect_code("the item before the close", code_of(consumer.try_receive()), 4);
                wire_failures += expect_code("a receive after the close", code_of(consumer.try_receive()), kClosed);
                ::foundation::linear::drop(std::move(consumer));
                return wire_failures;
            });
    }
    {
        auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::merge, MpscTag>>();
        using Merge = cc::channel_t<int, 8, decltype(root)>;
        auto [producer_root, consumer_perm] =
            perm::mint_permission_split<Merge::producer_tag, Merge::consumer_tag>(std::move(root));
        Merge channel{std::move(producer_root)};
        const eff::cap::Nondet merge = eff::testing::test().nondet;
        failures += cc::with_consumer(channel, std::move(consumer_perm), [&channel, merge](auto consumer) noexcept {
            return expect_lent(cc::with_producer(
                channel, merge, [&channel, merge, consumer = std::move(consumer)](auto first) mutable noexcept {
                    return expect_lent(cc::with_producer(
                        channel, merge,
                        [&channel, merge, consumer = std::move(consumer),
                         first = std::move(first)](auto second) mutable noexcept {
                            push_or_spin(first, 5);
                            std::move(first).close();
                            int merge_failures = 0;
                            if (channel.outstanding_producers() != 1) {
                                ::fixy::report(::fixy::Sink::Err,
                                               "the close of a merge producer kept its pool share\n");
                                ++merge_failures;
                            }
                            merge_failures +=
                                expect_code("the item of the closed producer", code_of(consumer.try_receive(merge)), 5);
                            merge_failures += expect_code("a channel with one producer left",
                                                          code_of(consumer.try_receive(merge)), kEmpty);
                            std::move(second).close();
                            merge_failures += expect_code("a channel after the close of each producer",
                                                          code_of(consumer.try_receive(merge)), kClosed);
                            if (channel.outstanding_producers() != 0) {
                                ::fixy::report(::fixy::Sink::Err,
                                               "the close of the last merge producer kept its pool share\n");
                                ++merge_failures;
                            }
                            ::foundation::linear::drop(std::move(consumer));
                            return merge_failures;
                        }));
                }));
        });
    }
    return failures;
}

}  // namespace

int main() {
    int failures = 0;
    failures += spsc_close_and_release();
    failures += mpsc_last_producer_closes();
    failures += producers_close_before_their_scope_ends();
    failures += stage_run_ends_its_handles();
    failures += rounds_receive_each_item_before_closed();
    failures += threaded_pipeline_without_sentinel();
    return failures == 0 ? 0 : 1;
}
