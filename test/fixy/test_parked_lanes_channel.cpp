// The lanes form of the parked channel (fixy/concurrent/ParkedChannel.h over a
// lanes channel of fixy/concurrent/PermissionedChannel.h).  Each producer
// thread opens the frame of its own lane, and one consumer reads the lanes in
// the order of the turn.  A lane of eight cells publishes its tail at each two
// pops, so a producer that parks on a full lane wakes only at a publish.
//
// Each case that tests a notify first waits until the other side is in the
// count of the waiters, and then waits some more turns, so the waiter reaches
// its park.  Without the notify, the waiter stays parked, and the test then
// fails at the timeout of the test preset.  The notifies are: the publish at
// the interval of a lane, the publish of each lane at an Empty answer of a
// receive and of a wait with no pop, the push, the end of a lane, and the
// close of the consumer for each lane.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/ParkedChannel.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <fixy/os/ThreadTasks.h>
#include <fixy/os/Time.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include "../foundation/philox_stream.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

namespace eff = ::foundation::effects;
namespace cc = ::fixy::concurrent;
namespace perm = ::foundation::permissions;

using ParkAtOnce = cc::SpinThenPark<0>;
using Item = std::uint64_t;
using ReceiveResult = ::fixy::Result<Item, cc::NoItem>;

struct LanesTag {};
using RootTag = cc::channel_tag::Whole<cc::ChannelForm::lanes, LanesTag>;

constexpr cc::LaneCount kLanes{3};
using Lane = cc::LaneIndex<kLanes>;
constexpr Lane kFirst{::fixy::index<0>()};
constexpr Lane kSecond{::fixy::index<1>()};
constexpr Lane kThird{::fixy::index<2>()};
constexpr std::size_t kLaneCells = 8;
// The turns of the pause after a thread sees a waiter in a count.
constexpr std::uint64_t kSettleTurns = 20000;
// The bound of a wait that ends at its deadline, and of a wait that must not.
constexpr std::uint64_t kSoonNanos = 1000000;
constexpr std::uint64_t kLateNanos = 60000000000;

template <class Root>
using ParkedLanes = cc::ParkedChannel<cc::channel_t<Item, kLaneCells, Root, kLanes>>;

[[nodiscard]] eff::cap::Nondet nondet_of(::fixy::BgLoadCtx const& ctx) noexcept {
    return eff::extract_bare_from_ctx<eff::Effect::Nondet>(ctx);
}

// The item of a lane: its index in the high word and its place in the low
// word.
[[nodiscard]] Item item_of(Lane lane, std::uint64_t place) noexcept {
    return (std::uint64_t{lane.raw()} << 32) | place;
}

// The pause after a thread sees a waiter in a count, so the waiter goes from
// its last test of the condition to its park.
void settle() noexcept {
    for (std::uint64_t turn = 0; turn < kSettleTurns; ++turn) {
        CRUCIBLE_SPIN_PAUSE;
    }
}

void wait_until(::fixy::Atomic<bool> const& flag) noexcept {
    while (!flag.load_acquire()) {
        CRUCIBLE_SPIN_PAUSE;
    }
}

template <class Parked>
void wait_for_a_parked_producer(Parked const& channel, eff::cap::Nondet nondet, Lane lane) noexcept {
    while (!channel.has_parked_producer(nondet, lane)) {
        CRUCIBLE_SPIN_PAUSE;
    }
    settle();
}

template <class Parked>
void wait_for_a_parked_consumer(Parked const& channel, eff::cap::Nondet nondet) noexcept {
    while (!channel.has_parked_consumer(nondet)) {
        CRUCIBLE_SPIN_PAUSE;
    }
    settle();
}

// A delay of a side: short for most items and long for some, so the other
// side waits.
void delay(::crucible::test::PhiloxStream& delays) noexcept {
    std::uint64_t const draw = delays.below(100);
    std::uint64_t const turns = draw < 90 ? delays.below(200) : 2000 + delays.below(10000);
    for (std::uint64_t turn = 0; turn < turns; ++turn) {
        CRUCIBLE_SPIN_PAUSE;
    }
}

// The lent value of a producer frame.  A test lends while the root is in the
// pool, so an empty lend ends the process.
template <class Value>
[[nodiscard]] Value expect_lent(::fixy::Option<Value>&& lent) noexcept {
    return std::move(lent).expect("the channel lends a producer while its root is in the pool");
}

// The counts of the items of the lanes of a case.
using LaneLengths = ::fixy::FixedArray<std::uint64_t, static_cast<std::size_t>(kLanes)>;

// The order of the merged stream: one item of each lane that still holds one,
// in the order of the lanes.
class Oracle final {
public:
    explicit Oracle(LaneLengths const& lengths) noexcept : lengths_{lengths} {}

    // The next item of the stream, or the end.
    [[nodiscard]] ReceiveResult next() noexcept {
        for (std::uint64_t step = 0; step < 2 * lane_count; ++step) {
            Lane const lane = Lane::check(lane_).expect("the lane of the oracle is below the count of the lanes");
            std::uint64_t const place = place_;
            lane_ = (lane_ + 1) % lane_count;
            if (lane_ == 0) ++place_;
            if (place < lengths_.at(lane)) return item_of(lane, place);
        }
        return ::fixy::err(cc::NoItem::Closed);
    }

private:
    static constexpr std::uint64_t lane_count = static_cast<std::uint64_t>(kLanes);
    LaneLengths lengths_{};
    std::uint64_t lane_ = 0;
    std::uint64_t place_ = 0;
};

// How fast a consumer takes the items.
enum class Pace : std::uint8_t {
    at_once,
    with_delays,
};

// Receives the rest of the stream in the order of the oracle, then the end,
// and closes the consumer.  The count of the items is the result.
template <class Consumer>
[[nodiscard]] std::uint64_t receive_rest(::fixy::BgLoadCtx const& ctx, Consumer consumer, Oracle oracle,
                                         Pace pace) noexcept {
    ::crucible::test::PhiloxStream delays{91};
    std::uint64_t received = 0;
    while (true) {
        ReceiveResult const got = consumer.template receive_waiting<ParkAtOnce>(ctx);
        CRUCIBLE_FATAL_INVARIANT(got == oracle.next());
        if (got.is_err()) break;
        ++received;
        if (pace == Pace::with_delays) delay(delays);
    }
    std::move(consumer).close(ctx);
    return received;
}

// The body of the producer thread of a lane: it opens the frame of the lane
// and gives the handle to the body, which ends it.
template <class Parked, class Body>
[[nodiscard]] auto feed(Parked& channel, Lane lane, Body body) noexcept {
    return [&channel, lane, body](::fixy::BgLoadCtx const& bg) noexcept {
        static_cast<void>(expect_lent(cc::with_producer(
            channel, nondet_of(bg), lane, [&bg, &body](auto handle) noexcept { body(bg, std::move(handle)); })));
    };
}

// The steps of the producers of a case that the other threads wait for.
struct LaneFlags {
    // The producer of the first lane pushed each item that it pushes at once.
    ::fixy::Atomic<bool> has_filled{false};
    // The producer of the second lane pushed its last item.
    ::fixy::Atomic<bool> has_pushed{false};
    // The count of the lanes with no item that their producers ended.
    ::fixy::Atomic<std::uint64_t> ended_lanes{0};
    // The consumer took the item that its case waits for.
    ::fixy::Atomic<bool> has_taken{false};
};

// The producer of a lane with no item ends its lane.
struct EndLane {
    LaneFlags& flags;

    void operator()(::fixy::BgLoadCtx const& bg, auto handle) const noexcept {
        std::move(handle).close(bg);
        static_cast<void>(flags.ended_lanes.fetch_add_acq_rel(1));
    }
};

// Waits until count producers of lanes with no item ended their lanes, so no
// end of a lane wakes the consumer after its park.
void wait_for_ended_lanes(LaneFlags const& flags, std::uint64_t count) noexcept {
    while (flags.ended_lanes.load_acquire() < count) {
        CRUCIBLE_SPIN_PAUSE;
    }
}

// Three producers push through lanes of eight cells with push_waiting, and a
// slow consumer takes each item with receive_waiting.  The lanes have different
// lengths, and the third ends first, so the producers of the other two wait
// for room.  The items come in the order of the oracle, and then the end.
void expect_three_lanes_through_full_lanes(::fixy::BgLoadCtx const& ctx) {
    auto root = perm::mint_permission_root<RootTag>();
    using Parked = ParkedLanes<decltype(root)>;
    static_assert(Parked::is_lanes && Parked::is_pooled && !Parked::is_merge && !Parked::moves_its_items);
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Parked::producer_tag, Parked::consumer_tag>(std::move(root));
    Parked channel{std::move(producer_root)};
    LaneLengths const lengths{700, 1000, 300};
    auto push_each = [&lengths](Lane lane, std::uint64_t seed) noexcept {
        return [&lengths, lane, seed](::fixy::BgLoadCtx const& bg, auto handle) noexcept {
            ::crucible::test::PhiloxStream delays{seed};
            CRUCIBLE_FATAL_INVARIANT(handle.lane().raw() == lane.raw());
            for (std::uint64_t place = 0; place < lengths.at(lane); ++place) {
                CRUCIBLE_FATAL_INVARIANT(handle.template push_waiting<ParkAtOnce>(bg, item_of(lane, place)).is_ok());
                if (place % 64 == 0) delay(delays);
            }
            std::move(handle).close(bg);
        };
    };
    std::uint64_t const received = cc::with_consumer(
        channel, std::move(consumer_perm), [&ctx, &channel, &push_each, &lengths](auto consumer) noexcept {
            auto drain = [&lengths, owned = std::move(consumer)](::fixy::BgLoadCtx const& bg) mutable noexcept {
                return receive_rest(bg, std::move(owned), Oracle{lengths}, Pace::with_delays);
            };
            auto joined = ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{},
                                                     feed(channel, kFirst, push_each(kFirst, 92)),
                                                     feed(channel, kSecond, push_each(kSecond, 93)),
                                                     feed(channel, kThird, push_each(kThird, 94)), std::move(drain))
                              .expect("the test starts the three producers and the consumer");
            return joined.at(::fixy::index<3>());
        });
    CRUCIBLE_FATAL_INVARIANT(received == 2000);
}

// The producer of a lane fills it, parks for one more item, and then reports
// its push.
struct ParkOnFullLane {
    Lane lane;
    LaneFlags& flags;

    void operator()(::fixy::BgLoadCtx const& bg, auto handle) const noexcept {
        std::uint64_t place = 0;
        while (handle.try_push(bg, item_of(lane, place)).is_ok()) {
            ++place;
        }
        CRUCIBLE_FATAL_INVARIANT(place == kLaneCells);
        CRUCIBLE_FATAL_INVARIANT(handle.template push_waiting<ParkAtOnce>(bg, item_of(lane, place)).is_ok());
        flags.has_pushed.store_release(true);
        std::move(handle).close(bg);
    }
};

// How the consumer of the case below finds that the lane of its turn is empty.
enum class EmptyDoor : std::uint8_t {
    receive_waiting,
    wait_for_item_until,
};

// The consumer takes one item of the first lane and one item of the full
// second lane, so it frees one cell of the second lane and publishes nothing.
// Then the first lane is empty, and its next item comes only after the producer
// of the second lane pushed.  So the Empty answer must publish the tail of the
// second lane and wake its producer.
void expect_an_empty_answer_wakes_the_producer_of_another_lane(::fixy::BgLoadCtx const& ctx, EmptyDoor door) {
    auto root = perm::mint_permission_root<RootTag>();
    using Parked = ParkedLanes<decltype(root)>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Parked::producer_tag, Parked::consumer_tag>(std::move(root));
    Parked channel{std::move(producer_root)};
    LaneFlags flags{};
    LaneLengths const lengths{2, kLaneCells + 1, 0};
    auto push_after_the_second_lane = [&flags](::fixy::BgLoadCtx const& bg, auto handle) noexcept {
        CRUCIBLE_FATAL_INVARIANT(handle.try_push(bg, item_of(kFirst, 0)).is_ok());
        wait_until(flags.has_pushed);
        CRUCIBLE_FATAL_INVARIANT(handle.try_push(bg, item_of(kFirst, 1)).is_ok());
        std::move(handle).close(bg);
    };
    std::uint64_t const received = cc::with_consumer(
        channel, std::move(consumer_perm),
        [&ctx, &channel, &flags, &push_after_the_second_lane, &lengths, door](auto consumer) noexcept {
            auto drain = [&channel, &lengths, door,
                          owned = std::move(consumer)](::fixy::BgLoadCtx const& bg) mutable noexcept {
                wait_for_a_parked_producer(channel, nondet_of(bg), kSecond);
                Oracle oracle{lengths};
                CRUCIBLE_FATAL_INVARIANT(owned.template receive_waiting<ParkAtOnce>(bg) == oracle.next());
                CRUCIBLE_FATAL_INVARIANT(owned.try_receive(bg) == oracle.next());
                CRUCIBLE_FATAL_INVARIANT(channel.has_parked_producer(nondet_of(bg), kSecond)
                                         && "a pop below the interval published a tail");
                if (door == EmptyDoor::wait_for_item_until) {
                    auto const clock = ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::Monotonic>(bg);
                    ::fixy::futex::Deadline const late =
                        ::fixy::futex::Deadline::after(clock, nondet_of(bg), kLateNanos).expect("the clock reads");
                    CRUCIBLE_FATAL_INVARIANT(owned.template wait_for_item_until<ParkAtOnce>(bg, late)
                                             == cc::EventWaitEnd::notified);
                }
                return receive_rest(bg, std::move(owned), oracle, Pace::at_once);
            };
            auto joined = ::fixy::thread::run_joined(
                              ctx, ::fixy::thread::ThreadOptions{}, feed(channel, kFirst, push_after_the_second_lane),
                              feed(channel, kSecond, ParkOnFullLane{.lane = kSecond, .flags = flags}),
                              feed(channel, kThird, EndLane{.flags = flags}), std::move(drain))
                              .expect("the test starts the three producers and the consumer");
            return joined.at(::fixy::index<3>());
        });
    // The consumer took two of the items before the rest.
    CRUCIBLE_FATAL_INVARIANT(received == kLaneCells + 1);
}

// The consumer takes two items of the full second lane, which reaches the
// interval, so the pop publishes the tail and wakes the producer.  The consumer
// then calls no door of the channel until the producer reports its push.
void expect_the_interval_wakes_the_producer(::fixy::BgLoadCtx const& ctx) {
    auto root = perm::mint_permission_root<RootTag>();
    using Parked = ParkedLanes<decltype(root)>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Parked::producer_tag, Parked::consumer_tag>(std::move(root));
    Parked channel{std::move(producer_root)};
    LaneFlags flags{};
    LaneLengths const lengths{6, kLaneCells + 1, 0};
    auto push_six = [&flags](::fixy::BgLoadCtx const& bg, auto handle) noexcept {
        for (std::uint64_t place = 0; place < 6; ++place) {
            CRUCIBLE_FATAL_INVARIANT(handle.try_push(bg, item_of(kFirst, place)).is_ok());
        }
        flags.has_filled.store_release(true);
        std::move(handle).close(bg);
    };
    std::uint64_t const received = cc::with_consumer(
        channel, std::move(consumer_perm), [&ctx, &channel, &flags, &push_six, &lengths](auto consumer) noexcept {
            auto drain = [&channel, &flags, &lengths,
                          owned = std::move(consumer)](::fixy::BgLoadCtx const& bg) mutable noexcept {
                wait_until(flags.has_filled);
                wait_for_ended_lanes(flags, 1);
                wait_for_a_parked_producer(channel, nondet_of(bg), kSecond);
                Oracle oracle{lengths};
                for (std::uint64_t taken = 0; taken < 4; ++taken) {
                    CRUCIBLE_FATAL_INVARIANT(owned.try_receive(bg) == oracle.next());
                }
                wait_until(flags.has_pushed);
                return receive_rest(bg, std::move(owned), oracle, Pace::at_once);
            };
            auto joined =
                ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, feed(channel, kFirst, push_six),
                                           feed(channel, kSecond, ParkOnFullLane{.lane = kSecond, .flags = flags}),
                                           feed(channel, kThird, EndLane{.flags = flags}), std::move(drain))
                    .expect("the test starts the three producers and the consumer");
            return joined.at(::fixy::index<3>());
        });
    // The consumer took four of the items before the rest.
    CRUCIBLE_FATAL_INVARIANT(received == 2 + kLaneCells + 1);
}

// The consumer parks while the lane of its turn is empty and open, although the
// second lane holds an item.  The end of the first lane wakes it, and the item
// of the second lane comes.
void expect_the_end_of_a_lane_wakes_the_consumer(::fixy::BgLoadCtx const& ctx) {
    auto root = perm::mint_permission_root<RootTag>();
    using Parked = ParkedLanes<decltype(root)>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Parked::producer_tag, Parked::consumer_tag>(std::move(root));
    Parked channel{std::move(producer_root)};
    LaneFlags flags{};
    LaneLengths const lengths{0, 1, 0};
    auto end_after_the_park = [&channel](::fixy::BgLoadCtx const& bg, auto handle) noexcept {
        wait_for_a_parked_consumer(channel, nondet_of(bg));
        std::move(handle).close(bg);
    };
    auto push_one = [&flags](::fixy::BgLoadCtx const& bg, auto handle) noexcept {
        CRUCIBLE_FATAL_INVARIANT(handle.try_push(bg, item_of(kSecond, 0)).is_ok());
        std::move(handle).close(bg);
        flags.has_pushed.store_release(true);
    };
    std::uint64_t const received = cc::with_consumer(
        channel, std::move(consumer_perm),
        [&ctx, &channel, &flags, &end_after_the_park, &push_one, &lengths](auto consumer) noexcept {
            auto drain = [&flags, &lengths, owned = std::move(consumer)](::fixy::BgLoadCtx const& bg) mutable noexcept {
                // No other change of the channel wakes the consumer after its
                // park.
                wait_until(flags.has_pushed);
                wait_for_ended_lanes(flags, 1);
                return receive_rest(bg, std::move(owned), Oracle{lengths}, Pace::at_once);
            };
            auto joined =
                ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{},
                                           feed(channel, kFirst, end_after_the_park), feed(channel, kSecond, push_one),
                                           feed(channel, kThird, EndLane{.flags = flags}), std::move(drain))
                    .expect("the test starts the three producers and the consumer");
            return joined.at(::fixy::index<3>());
        });
    CRUCIBLE_FATAL_INVARIANT(received == 1);
}

// The consumer parks on an empty lane of its turn, and the push of that lane
// wakes it.  The producer ends its lane only after the consumer took the item.
void expect_a_push_wakes_the_consumer(::fixy::BgLoadCtx const& ctx) {
    auto root = perm::mint_permission_root<RootTag>();
    using Parked = ParkedLanes<decltype(root)>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Parked::producer_tag, Parked::consumer_tag>(std::move(root));
    Parked channel{std::move(producer_root)};
    LaneFlags flags{};
    auto push_after_the_park = [&channel, &flags](::fixy::BgLoadCtx const& bg, auto handle) noexcept {
        wait_for_a_parked_consumer(channel, nondet_of(bg));
        CRUCIBLE_FATAL_INVARIANT(handle.try_push(bg, item_of(kFirst, 0)).is_ok());
        wait_until(flags.has_taken);
        std::move(handle).close(bg);
    };
    cc::with_consumer(
        channel, std::move(consumer_perm), [&ctx, &channel, &flags, &push_after_the_park](auto consumer) noexcept {
            auto drain = [&flags, owned = std::move(consumer)](::fixy::BgLoadCtx const& bg) mutable noexcept {
                wait_for_ended_lanes(flags, 2);
                CRUCIBLE_FATAL_INVARIANT(owned.template receive_waiting<ParkAtOnce>(bg)
                                         == ReceiveResult{item_of(kFirst, 0)});
                flags.has_taken.store_release(true);
                CRUCIBLE_FATAL_INVARIANT(owned.template receive_waiting<ParkAtOnce>(bg)
                                         == ReceiveResult{::fixy::err(cc::NoItem::Closed)});
                std::move(owned).close(bg);
            };
            static_cast<void>(::fixy::thread::run_joined(
                                  ctx, ::fixy::thread::ThreadOptions{}, feed(channel, kFirst, push_after_the_park),
                                  feed(channel, kSecond, EndLane{.flags = flags}),
                                  feed(channel, kThird, EndLane{.flags = flags}), std::move(drain))
                                  .expect("the test starts the three producers and the consumer"));
        });
}

// Three producers park, each on its own full lane.  The close of the consumer
// wakes each of them, and each ends with ConsumerGone.
void expect_a_consumer_close_wakes_each_lane(::fixy::BgLoadCtx const& ctx) {
    auto root = perm::mint_permission_root<RootTag>();
    using Parked = ParkedLanes<decltype(root)>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Parked::producer_tag, Parked::consumer_tag>(std::move(root));
    Parked channel{std::move(producer_root)};
    ::fixy::Atomic<std::uint64_t> gone{0};
    auto wait_for_the_close = [&gone](Lane lane) noexcept {
        return [&gone, lane](::fixy::BgLoadCtx const& bg, auto handle) noexcept {
            std::uint64_t place = 0;
            while (handle.try_push(bg, item_of(lane, place)).is_ok()) {
                ++place;
            }
            CRUCIBLE_FATAL_INVARIANT(
                handle.template push_waiting<ParkAtOnce>(bg, item_of(lane, place))
                    .is_err_and([](cc::PushRefusal why) noexcept { return why == cc::PushRefusal::ConsumerGone; }));
            static_cast<void>(gone.fetch_add_acq_rel(1));
            std::move(handle).close(bg);
        };
    };
    cc::with_consumer(channel, std::move(consumer_perm), [&ctx, &channel, &wait_for_the_close](auto consumer) noexcept {
        auto close_after_the_waits = [&channel,
                                      owned = std::move(consumer)](::fixy::BgLoadCtx const& bg) mutable noexcept {
            for (Lane const lane : ::fixy::FixedArray<Lane, 3>{kFirst, kSecond, kThird}) {
                wait_for_a_parked_producer(channel, nondet_of(bg), lane);
            }
            std::move(owned).close(bg);
        };
        static_cast<void>(::fixy::thread::run_joined(
                              ctx, ::fixy::thread::ThreadOptions{}, feed(channel, kFirst, wait_for_the_close(kFirst)),
                              feed(channel, kSecond, wait_for_the_close(kSecond)),
                              feed(channel, kThird, wait_for_the_close(kThird)), std::move(close_after_the_waits))
                              .expect("the test starts the three producers and the consumer"));
    });
    CRUCIBLE_FATAL_INVARIANT(gone.load_acquire() == 3);
}

// The doors of the two producers and of the consumer on one thread: the wait
// with no pop waits for the lane of the turn and not for another lane, a timed
// push of a full lane ends with Full, and a pop of each door takes the next
// item of the turn.
template <class FirstRef, class SecondRef, class Consumer>
void check_doors(::fixy::BgLoadCtx const& ctx, FirstRef first, SecondRef second, Consumer consumer) noexcept {
    auto const clock = ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::Monotonic>(ctx);
    auto const after = [&ctx, &clock](std::uint64_t nanos) noexcept {
        return ::fixy::futex::Deadline::after(clock, nondet_of(ctx), nanos).expect("the clock reads");
    };
    for (std::uint64_t place = 0; place < kLaneCells; ++place) {
        CRUCIBLE_FATAL_INVARIANT(second->try_push(ctx, item_of(kSecond, place)).is_ok());
    }
    CRUCIBLE_FATAL_INVARIANT(
        second->template push_waiting_until<ParkAtOnce>(ctx, item_of(kSecond, kLaneCells), after(kSoonNanos))
            .is_err_and([](cc::PushRefusal why) noexcept { return why == cc::PushRefusal::Full; }));
    CRUCIBLE_FATAL_INVARIANT(consumer.template wait_for_item_until<ParkAtOnce>(ctx, after(kSoonNanos))
                             == cc::EventWaitEnd::deadline_passed);
    CRUCIBLE_FATAL_INVARIANT(consumer.template receive_waiting_until<ParkAtOnce>(ctx, after(kSoonNanos))
                                 .is_err_and([](cc::NoItem why) noexcept { return why == cc::NoItem::Empty; }));
    CRUCIBLE_FATAL_INVARIANT(first->try_push(ctx, item_of(kFirst, 0)).is_ok());
    CRUCIBLE_FATAL_INVARIANT(consumer.template wait_for_item_until<ParkAtOnce>(ctx, after(kLateNanos))
                             == cc::EventWaitEnd::notified);
    CRUCIBLE_FATAL_INVARIANT(consumer.try_pop(ctx) == ::fixy::some(item_of(kFirst, 0)));
    CRUCIBLE_FATAL_INVARIANT(consumer.try_pop(ctx) == ::fixy::some(item_of(kSecond, 0)));
    // The third lane is open and empty, so the turn waits for it.
    CRUCIBLE_FATAL_INVARIANT(consumer.try_pop(ctx).is_none() && !consumer.is_closed());
    std::move(consumer).close(ctx);
}

void expect_the_doors_of_one_thread(::fixy::BgLoadCtx const& ctx) {
    auto root = perm::mint_permission_root<RootTag>();
    using Parked = ParkedLanes<decltype(root)>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Parked::producer_tag, Parked::consumer_tag>(std::move(root));
    Parked channel{std::move(producer_root)};
    static_cast<void>(expect_lent(cc::with_producer(
        channel, nondet_of(ctx), kFirst,
        [&ctx, &channel, token = std::move(consumer_perm)](auto first) mutable noexcept {
            static_cast<void>(expect_lent(cc::with_producer(
                channel, nondet_of(ctx), kSecond,
                [&ctx, &channel, first_ref = ::fixy::to(first),
                 token = std::move(token)](auto second) mutable noexcept {
                    cc::with_consumer(channel, std::move(token),
                                      [&ctx, first_ref, second_ref = ::fixy::to(second)](auto consumer) noexcept {
                                          check_doors(ctx, first_ref, second_ref, std::move(consumer));
                                      });
                    std::move(second).close(ctx);
                })));
            std::move(first).close(ctx);
        })));
}

}  // namespace

int main() {
    ::fixy::BgLoadCtx const ctx{eff::testing::bg()};
    expect_three_lanes_through_full_lanes(ctx);
    expect_an_empty_answer_wakes_the_producer_of_another_lane(ctx, EmptyDoor::receive_waiting);
    expect_an_empty_answer_wakes_the_producer_of_another_lane(ctx, EmptyDoor::wait_for_item_until);
    expect_the_interval_wakes_the_producer(ctx);
    expect_the_end_of_a_lane_wakes_the_consumer(ctx);
    expect_a_push_wakes_the_consumer(ctx);
    expect_a_consumer_close_wakes_each_lane(ctx);
    expect_the_doors_of_one_thread(ctx);
    return 0;
}
