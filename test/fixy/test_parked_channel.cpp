// The parked form of a wire (fixy/concurrent/ParkedChannel.h): each door of
// an end notifies the count that its change can end a wait of, so no caller
// pairs a wait with a count.  A lost wake leaves a thread parked, and the test
// then fails at the timeout of the test preset.  Each producer and each
// consumer runs through run_beside of fixy/os/ThreadTasks.h.
//
// A case that tests a notify first waits until the other side is in the
// count of the waiters (has_parked_producer, has_parked_consumer), and then
// waits some more turns, so the waiter reaches its park before the condition
// changes.  Without the notify, the waiter then stays parked.  Each door that
// notifies has such a case: each pop door at the stride items of the full
// wire, the two push doors at the stride items of the slow producer, the
// close of the producer at the end of the slow producer, and the close of
// the consumer in the case of a consumer that ends.  A receive that waited for
// its item has the wire of one cell.  There the producer must park before the
// consumer wakes, which the schedule gives in most of the items.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/ParkedChannel.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <fixy/os/ThreadTasks.h>
#include <fixy/os/Time.h>
#include <foundation/Linear.h>
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
using PushResult = ::fixy::Result<::fixy::Unit, cc::PushRefusal>;
using ReceiveResult = ::fixy::Result<Item, cc::NoItem>;

struct WireTag {};

constexpr std::uint64_t kItems = 2000;
// A side waits for a parked waiter of the other side before each item of
// this stride.
constexpr std::uint64_t kParkStride = 64;
// The items of the case of a producer that parks before the first consumer.
constexpr Item kStartItems = 64;
// The turns of the pause after a thread sees a waiter in a count.
constexpr std::uint64_t kSettleTurns = 20000;
// The bound of one timed receive of the pop door receive_waiting_until.
constexpr std::uint64_t kTimedStepNanos = 1000000;

// A delay of a side: short for most items and long for some, so the other
// side waits.
void delay(::crucible::test::PhiloxStream& delays) noexcept {
    std::uint64_t const draw = delays.below(100);
    std::uint64_t const turns = draw < 90 ? delays.below(200) : 2000 + delays.below(10000);
    for (std::uint64_t turn = 0; turn < turns; ++turn) {
        CRUCIBLE_SPIN_PAUSE;
    }
}

// The pause after a thread sees a waiter in a count, so the waiter goes from
// its last test of the condition to its park.
void settle() noexcept {
    for (std::uint64_t turn = 0; turn < kSettleTurns; ++turn) {
        CRUCIBLE_SPIN_PAUSE;
    }
}

[[nodiscard]] eff::cap::Nondet nondet_of(::fixy::BgLoadCtx const& ctx) noexcept {
    return eff::extract_bare_from_ctx<eff::Effect::Nondet>(ctx);
}

// Waits until a producer is in the count of the waiters of room, and then
// until it parks.
template <class Parked>
void wait_for_a_parked_producer(Parked const& channel, eff::cap::Nondet nondet) noexcept {
    while (!channel.has_parked_producer(nondet)) {
        CRUCIBLE_SPIN_PAUSE;
    }
    settle();
}

// Waits until a consumer is in the count of the waiters of items, and then
// until it parks.
template <class Parked>
void wait_for_a_parked_consumer(Parked const& channel, eff::cap::Nondet nondet) noexcept {
    while (!channel.has_parked_consumer(nondet)) {
        CRUCIBLE_SPIN_PAUSE;
    }
    settle();
}

// Waits until the consumer took count items.
void wait_until_taken(::fixy::Atomic<std::uint64_t> const& taken, std::uint64_t count) noexcept {
    while (taken.load_acquire() < count) {
        CRUCIBLE_SPIN_PAUSE;
    }
}

[[nodiscard]] bool is_refused_as(PushResult const& pushed, cc::PushRefusal expected) noexcept {
    return pushed.is_err_and([expected](cc::PushRefusal why) noexcept { return why == expected; });
}

// Pushes items from next with try_push until the ring refuses one.  The result
// is the refusal, and next is then the item that the ring refused.
template <class Producer>
[[nodiscard]] cc::PushRefusal fill(::fixy::BgLoadCtx const& ctx, Producer& producer, Item& next) noexcept {
    for (;;) {
        PushResult const pushed = producer.try_push(ctx, next);
        if (pushed.is_err()) {
            return is_refused_as(pushed, cc::PushRefusal::Full) ? cc::PushRefusal::Full : cc::PushRefusal::ConsumerGone;
        }
        ++next;
    }
}

// A door of a parked end takes no event count.
template <class Handle>
concept ReceivesOnACount = requires(Handle& handle, ::fixy::BgLoadCtx const& ctx, cc::EventCount& events) {
    handle.template receive_waiting<ParkAtOnce>(ctx, events);
};

// How the consumer of a test takes each item.
enum class PopDoor : std::uint8_t {
    receive_waiting,
    receive_waiting_until,
    try_receive,
    try_pop,
};

// Whether the consumer of a test waits after each item.
enum class Pace : std::uint8_t {
    with_delays,
    at_once,
};

// Whether the consumer of a test waits for a parked producer before each
// stride item.
enum class ParkWait : std::uint8_t {
    none,
    producer_at_stride,
};

// How a consumer of a test takes each item, and its seed of delays.
struct ConsumerPlan {
    PopDoor door = PopDoor::receive_waiting;
    Pace pace = Pace::at_once;
    ParkWait park_wait = ParkWait::none;
    std::uint64_t seed = 0;
};

struct Received {
    std::uint64_t count = 0;
    bool is_ordered = true;
};

// One take of an item through door: the item, no item yet, or the end.
// try_pop gives no end, so it reads the closed state when the ring is empty.
// The producer can push its last item after that pop and then close, so a
// closed stream gets one more pop, which keeps that item.
template <class Consumer, class Clock>
[[nodiscard]] ReceiveResult take_once(::fixy::BgLoadCtx const& ctx, Clock const& clock, Consumer& consumer,
                                      PopDoor door) noexcept {
    if (door == PopDoor::receive_waiting) return consumer.template receive_waiting<ParkAtOnce>(ctx);
    if (door == PopDoor::receive_waiting_until) {
        ::fixy::futex::Deadline const deadline =
            ::fixy::futex::Deadline::after(clock, nondet_of(ctx), kTimedStepNanos).expect("the clock reads");
        return consumer.template receive_waiting_until<ParkAtOnce>(ctx, deadline);
    }
    if (door == PopDoor::try_receive) return consumer.try_receive(ctx);
    ::fixy::Option<Item> got = consumer.try_pop(ctx);
    if (got.is_some()) return std::move(got).expect("the test above found an item");
    if (!consumer.is_closed()) return ::fixy::err(cc::NoItem::Empty);
    ::fixy::Option<Item> last = consumer.try_pop(ctx);
    if (last.is_some()) return std::move(last).expect("the test above found an item");
    return ::fixy::err(cc::NoItem::Closed);
}

// Takes each item by plan until the end of the stream.  With
// ParkWait::producer_at_stride, the consumer waits for a parked producer
// before each stride item that leaves the producer an item to park on, so
// the pop of that item must notify room.
template <class Parked, class Consumer>
[[nodiscard]] Received receive_each(::fixy::BgLoadCtx const& ctx, Parked const& channel, Consumer& consumer,
                                    ConsumerPlan const& plan) noexcept {
    auto const clock = ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::Monotonic>(ctx);
    ::crucible::test::PhiloxStream delays{plan.seed};
    Received result{};
    for (;;) {
        bool const is_parked_stride = plan.park_wait == ParkWait::producer_at_stride && result.count % kParkStride == 0
                                   && result.count + Parked::channel_capacity < kItems;
        if (is_parked_stride) wait_for_a_parked_producer(channel, nondet_of(ctx));
        ReceiveResult received = take_once(ctx, clock, consumer, plan.door);
        while (received.is_err_and([](cc::NoItem why) noexcept { return why == cc::NoItem::Empty; })) {
            CRUCIBLE_SPIN_PAUSE;
            received = take_once(ctx, clock, consumer, plan.door);
        }
        if (received.is_err()) break;
        result.is_ordered =
            result.is_ordered && std::move(received).expect("the test above found an item") == result.count;
        ++result.count;
        if (plan.pace == Pace::with_delays) delay(delays);
    }
    return result;
}

// A wire of Capacity cells: the producer waits for room with push_waiting, and
// the consumer takes each item through the door of plan.  Before each stride
// item of a slow consumer, the consumer waits until the producer parks on the
// full ring, so the pop must wake it.  A fast consumer parks before most items.
// On a wire of one cell, the producer then often parks for its next item before
// the consumer wakes, so a receive that waited must wake the producer too.  No
// caller notifies a count.  Each item comes in order, and then the end.
template <std::size_t Capacity>
void expect_each_item_through_a_full_wire(::fixy::BgLoadCtx const& ctx, ConsumerPlan const& plan) {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::wire, WireTag>>();
    using Parked = cc::ParkedChannel<cc::channel_t<Item, Capacity, decltype(root)>>;
    Parked channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<typename Parked::producer_tag, typename Parked::consumer_tag>(std::move(root));
    cc::with_ends(channel, std::move(producer_perm), std::move(consumer_perm),
                  [&ctx, &channel, &plan](auto producer, auto consumer) noexcept {
                      static_assert(!ReceivesOnACount<decltype(consumer)>);
                      auto push_each = [handle = std::move(producer)](::fixy::BgLoadCtx const& bg) mutable noexcept {
                          for (Item item = 0; item < kItems; ++item) {
                              CRUCIBLE_FATAL_INVARIANT(handle.template push_waiting<ParkAtOnce>(bg, item).is_ok());
                          }
                          std::move(handle).close(bg);
                      };
                      auto values = ::fixy::thread::run_beside(
                                        ctx, ::fixy::thread::ThreadOptions{}, std::move(push_each),
                                        [&ctx, &channel, &plan, owned = std::move(consumer)]() mutable noexcept {
                                            Received const got = receive_each(ctx, channel, owned, plan);
                                            std::move(owned).close(ctx);
                                            return got;
                                        })
                                        .expect("the thread door starts a producer");
                      Received const received = values.at(::fixy::index<1>());
                      CRUCIBLE_FATAL_INVARIANT(received.is_ordered);
                      CRUCIBLE_FATAL_INVARIANT(received.count == kItems);
                  });
}

// A slow producer and a fast consumer that parks on items.  Before each
// stride item, the producer waits until the consumer took each item before it
// and parks on the empty ring.  Then it pushes the item through try_push or
// push_waiting in turn, and waits until the consumer took it, so each push
// door must wake the consumer.  Before its close, the producer waits for the
// park again, so the close must wake the consumer too.  The consumer counts
// each item after the wait that gave it ended, so a consumer in the count
// after that count waits for the next item.
void expect_a_slow_producer_wakes_a_parked_consumer(::fixy::BgLoadCtx const& ctx) {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::wire, WireTag>>();
    using Parked = cc::ParkedChannel<cc::channel_t<Item, 4, decltype(root)>>;
    Parked channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Parked::producer_tag, Parked::consumer_tag>(std::move(root));
    // The count of the items that the consumer took, for the producer.
    ::fixy::Atomic<std::uint64_t> taken{0};
    cc::with_ends(channel, std::move(producer_perm), std::move(consumer_perm),
                  [&ctx, &channel, &taken](auto producer, auto consumer) noexcept {
                      auto push_slowly = [&channel, &taken,
                                          handle = std::move(producer)](::fixy::BgLoadCtx const& bg) mutable noexcept {
                          ::crucible::test::PhiloxStream delays{21};
                          eff::cap::Nondet const nondet = nondet_of(bg);
                          for (Item item = 0; item < kItems; ++item) {
                              if (item % kParkStride != 0) {
                                  CRUCIBLE_FATAL_INVARIANT(handle.template push_waiting<ParkAtOnce>(bg, item).is_ok());
                                  delay(delays);
                                  continue;
                              }
                              wait_until_taken(taken, item);
                              wait_for_a_parked_consumer(channel, nondet);
                              bool const is_try_push = (item / kParkStride) % 2 == 0;
                              PushResult const pushed = is_try_push
                                                          ? handle.try_push(bg, item)
                                                          : handle.template push_waiting<ParkAtOnce>(bg, item);
                              CRUCIBLE_FATAL_INVARIANT(pushed.is_ok());
                              wait_until_taken(taken, item + 1);
                          }
                          wait_until_taken(taken, kItems);
                          wait_for_a_parked_consumer(channel, nondet);
                          std::move(handle).close(bg);
                      };
                      auto values =
                          ::fixy::thread::run_beside(
                              ctx, ::fixy::thread::ThreadOptions{}, std::move(push_slowly),
                              [&ctx, &taken, owned = std::move(consumer)]() mutable noexcept {
                                  Received got{};
                                  ReceiveResult received = owned.template receive_waiting<ParkAtOnce>(ctx);
                                  while (received.is_ok()) {
                                      got.is_ordered =
                                          got.is_ordered
                                          && std::move(received).expect("the test above found an item") == got.count;
                                      ++got.count;
                                      taken.store_release(got.count);
                                      received = owned.template receive_waiting<ParkAtOnce>(ctx);
                                  }
                                  std::move(owned).close(ctx);
                                  return got;
                              })
                              .expect("the thread door starts a producer");
                      Received const received = values.at(::fixy::index<1>());
                      CRUCIBLE_FATAL_INVARIANT(received.is_ordered);
                      CRUCIBLE_FATAL_INVARIANT(received.count == kItems);
                  });
}

// A producer that parks on a full ring ends with ConsumerGone when the
// consumer closes, and its item stays with it.  The consumer closes only
// after it sees the producer park, so its close must wake the producer.  A
// push after the close of the consumer gives ConsumerGone too, so a loop of
// try_push ends.
void expect_consumer_gone_ends_a_parked_producer(::fixy::BgLoadCtx const& ctx) {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::wire, WireTag>>();
    using Parked = cc::ParkedChannel<cc::channel_t<Item, 2, decltype(root)>>;
    Parked channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Parked::producer_tag, Parked::consumer_tag>(std::move(root));
    cc::with_ends(
        channel, std::move(producer_perm), std::move(consumer_perm),
        [&ctx, &channel](auto producer, auto consumer) noexcept {
            auto fill_then_wait = [handle = std::move(producer)](::fixy::BgLoadCtx const& bg) mutable noexcept {
                Item item = 0;
                bool const is_full = fill(bg, handle, item) == cc::PushRefusal::Full;
                bool const is_gone =
                    is_refused_as(handle.template push_waiting<ParkAtOnce>(bg, item), cc::PushRefusal::ConsumerGone);
                bool const is_gone_again = is_refused_as(handle.try_push(bg, item), cc::PushRefusal::ConsumerGone);
                std::move(handle).close(bg);
                return is_full && is_gone && is_gone_again;
            };
            auto values = ::fixy::thread::run_beside(ctx, ::fixy::thread::ThreadOptions{}, std::move(fill_then_wait),
                                                     [&ctx, &channel, owned = std::move(consumer)]() mutable noexcept {
                                                         wait_for_a_parked_producer(channel, nondet_of(ctx));
                                                         std::move(owned).close(ctx);
                                                     })
                              .expect("the thread door starts a producer");
            CRUCIBLE_FATAL_INVARIANT(values.at(::fixy::index<0>()));
        });
}

// A release of the consumer gives the token back and keeps the stream, so it
// is no end of the consumer.  A producer that parks on a full ring stays
// parked through the release, and it reads no ConsumerGone.  The next
// consumer of the same token opens, and its first pop wakes the producer,
// which ends with its push.  The consumer gets each item and then the end.
void expect_a_parked_producer_waits_through_a_consumer_release(::fixy::BgLoadCtx const& ctx) {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::wire, WireTag>>();
    using Parked = cc::ParkedChannel<cc::channel_t<Item, 2, decltype(root)>>;
    Parked channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Parked::producer_tag, Parked::consumer_tag>(std::move(root));
    // A producer that ends its wait at the release ends the wait of the
    // consumer below, so a defect fails the test and does not hang it.
    ::fixy::Atomic<bool> is_producer_done{false};
    auto fill_then_wait = [&channel, &is_producer_done,
                           token = std::move(producer_perm)](::fixy::BgLoadCtx const& bg) mutable noexcept {
        bool const is_pushed = cc::with_producer(channel, std::move(token), [&bg](auto producer) noexcept {
            Item item = 0;
            bool const is_full = fill(bg, producer, item) == cc::PushRefusal::Full;
            bool const is_waited = producer.template push_waiting<ParkAtOnce>(bg, item).is_ok();
            std::move(producer).close(bg);
            return is_full && is_waited;
        });
        is_producer_done.store_release(true);
        return is_pushed;
    };
    auto values = ::fixy::thread::run_beside(
                      ctx, ::fixy::thread::ThreadOptions{}, std::move(fill_then_wait),
                      [&ctx, &channel, &is_producer_done, token = std::move(consumer_perm)]() mutable noexcept {
                          eff::cap::Nondet const nondet = nondet_of(ctx);
                          auto returned =
                              cc::with_consumer(channel, std::move(token), [&channel, nondet](auto consumer) noexcept {
                                  wait_for_a_parked_producer(channel, nondet);
                                  return std::move(consumer).release();
                              });
                          settle();
                          bool const is_still_parked =
                              channel.has_parked_producer(nondet) && !is_producer_done.load_acquire();
                          return cc::with_consumer(
                              channel, std::move(returned), [&ctx, &channel, is_still_parked](auto consumer) noexcept {
                                  Received const got = receive_each(ctx, channel, consumer, ConsumerPlan{.seed = 51});
                                  std::move(consumer).close(ctx);
                                  return is_still_parked && got.count == Parked::channel_capacity + 1 && got.is_ordered;
                              });
                      })
                      .expect("the thread door starts a producer");
    CRUCIBLE_FATAL_INVARIANT(values.at(::fixy::index<0>()));
    CRUCIBLE_FATAL_INVARIANT(values.at(::fixy::index<1>()));
}

// A push to a full ring after a release of the consumer reads Full, and no
// ConsumerGone: a release keeps the stream, so the next consumer of the token
// pops.  That consumer pops one item and closes, and the push then goes in.
void expect_a_push_after_a_consumer_release_reads_full(::fixy::BgLoadCtx const& ctx) {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::wire, WireTag>>();
    using Parked = cc::ParkedChannel<cc::channel_t<Item, 2, decltype(root)>>;
    Parked channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Parked::producer_tag, Parked::consumer_tag>(std::move(root));
    cc::with_producer(
        channel, std::move(producer_perm),
        [&ctx, &channel, token = std::move(consumer_perm)](auto producer) mutable noexcept {
            Item item = 0;
            CRUCIBLE_FATAL_INVARIANT(fill(ctx, producer, item) == cc::PushRefusal::Full);
            auto returned = cc::with_consumer(channel, std::move(token),
                                              [](auto consumer) noexcept { return std::move(consumer).release(); });
            CRUCIBLE_FATAL_INVARIANT(is_refused_as(producer.try_push(ctx, item), cc::PushRefusal::Full));
            cc::with_consumer(channel, std::move(returned), [&ctx](auto consumer) noexcept {
                CRUCIBLE_FATAL_INVARIANT(std::move(consumer.try_pop(ctx)).expect("the ring is full") == 0);
                std::move(consumer).close(ctx);
            });
            CRUCIBLE_FATAL_INVARIANT(producer.try_push(ctx, item).is_ok());
            std::move(producer).close(ctx);
        });
}

// No consumer yet is not a consumer that ended.  A producer that fills the
// ring before the first consumer opens reads Full, and its push_waiting
// parks.  The consumer opens only after it sees the producer park, so its
// first pop must wake the producer.  The producer ends with its pushes, and
// the consumer gets each item and then the end.
void expect_a_producer_parks_before_the_first_consumer(::fixy::BgLoadCtx const& ctx) {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::wire, WireTag>>();
    using Parked = cc::ParkedChannel<cc::channel_t<Item, 4, decltype(root)>>;
    Parked channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Parked::producer_tag, Parked::consumer_tag>(std::move(root));
    // A producer that does not park ends the wait of the consumer below, so a
    // defect fails the test and does not hang it.
    ::fixy::Atomic<bool> is_producer_done{false};
    auto fill_then_push = [&channel, &is_producer_done,
                           token = std::move(producer_perm)](::fixy::BgLoadCtx const& bg) mutable noexcept {
        bool const is_each_pushed = cc::with_producer(channel, std::move(token), [&bg](auto producer) noexcept {
            Item item = 0;
            bool is_pushed = fill(bg, producer, item) == cc::PushRefusal::Full;
            for (; item < kStartItems; ++item) {
                is_pushed = producer.template push_waiting<ParkAtOnce>(bg, item).is_ok() && is_pushed;
            }
            std::move(producer).close(bg);
            return is_pushed;
        });
        is_producer_done.store_release(true);
        return is_each_pushed;
    };
    auto values = ::fixy::thread::run_beside(
                      ctx, ::fixy::thread::ThreadOptions{}, std::move(fill_then_push),
                      [&ctx, &channel, &is_producer_done, token = std::move(consumer_perm)]() mutable noexcept {
                          eff::cap::Nondet const nondet = nondet_of(ctx);
                          while (!channel.has_parked_producer(nondet) && !is_producer_done.load_acquire()) {
                              CRUCIBLE_SPIN_PAUSE;
                          }
                          settle();
                          return cc::with_consumer(channel, std::move(token), [&ctx, &channel](auto consumer) noexcept {
                              Received const got = receive_each(ctx, channel, consumer, ConsumerPlan{.seed = 31});
                              std::move(consumer).close(ctx);
                              return got.count == kStartItems && got.is_ordered;
                          });
                      })
                      .expect("the thread door starts a producer");
    CRUCIBLE_FATAL_INVARIANT(values.at(::fixy::index<0>()));
    CRUCIBLE_FATAL_INVARIANT(values.at(::fixy::index<1>()));
}

// A release keeps the stream open: the consumer that parks after the release
// receives the item of the next producer of the token, and then the end of
// its close.
void expect_release_keeps_the_stream_open(::fixy::BgLoadCtx const& ctx) {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::wire, WireTag>>();
    using Parked = cc::ParkedChannel<cc::channel_t<Item, 4, decltype(root)>>;
    Parked channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Parked::producer_tag, Parked::consumer_tag>(std::move(root));
    auto returned = cc::with_producer(channel, std::move(producer_perm), [&ctx](auto producer) noexcept {
        CRUCIBLE_FATAL_INVARIANT(producer.try_push(ctx, Item{0}).is_ok());
        return std::move(producer).release();
    });
    cc::with_consumer(
        channel, std::move(consumer_perm),
        [&ctx, &channel, held = std::move(returned)](auto consumer) mutable noexcept {
            auto push_late = [&channel, token = std::move(held)](::fixy::BgLoadCtx const& bg) mutable noexcept {
                cc::with_producer(channel, std::move(token), [&bg](auto producer) noexcept {
                    CRUCIBLE_FATAL_INVARIANT(producer.try_push(bg, Item{1}).is_ok());
                    std::move(producer).close(bg);
                });
            };
            auto values = ::fixy::thread::run_beside(ctx, ::fixy::thread::ThreadOptions{}, std::move(push_late),
                                                     [&ctx, &channel, owned = std::move(consumer)]() mutable noexcept {
                                                         Received const got = receive_each(
                                                             ctx, channel, owned,
                                                             ConsumerPlan{.pace = Pace::with_delays, .seed = 41});
                                                         std::move(owned).close(ctx);
                                                         return got.count == 2 && got.is_ordered;
                                                     })
                              .expect("the thread door starts a producer");
            CRUCIBLE_FATAL_INVARIANT(values.at(::fixy::index<1>()));
        });
}

// The timed receive gives NoItem::Empty at its deadline when no producer
// pushes, and the item when one is in the ring.
void expect_the_timed_receive_ends_at_its_deadline(::fixy::BgLoadCtx const& ctx) {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::wire, WireTag>>();
    using Parked = cc::ParkedChannel<cc::channel_t<Item, 4, decltype(root)>>;
    Parked channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Parked::producer_tag, Parked::consumer_tag>(std::move(root));
    cc::with_ends(channel, std::move(producer_perm), std::move(consumer_perm),
                  [&ctx](auto producer, auto consumer) noexcept {
                      auto const clock = ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::Monotonic>(ctx);
                      eff::cap::Nondet const nondet = nondet_of(ctx);
                      ::fixy::futex::Deadline const soon =
                          ::fixy::futex::Deadline::after(clock, nondet, kTimedStepNanos).expect("the clock reads");
                      ReceiveResult const empty = consumer.template receive_waiting_until<ParkAtOnce>(ctx, soon);
                      CRUCIBLE_FATAL_INVARIANT(
                          empty.is_err_and([](cc::NoItem why) noexcept { return why == cc::NoItem::Empty; }));
                      CRUCIBLE_FATAL_INVARIANT(producer.try_push(ctx, Item{3}).is_ok());
                      ::fixy::futex::Deadline const later =
                          ::fixy::futex::Deadline::after(clock, nondet, kTimedStepNanos).expect("the clock reads");
                      ReceiveResult got = consumer.template receive_waiting_until<ParkAtOnce>(ctx, later);
                      CRUCIBLE_FATAL_INVARIANT(std::move(got).expect("the ring holds an item") == 3);
                      std::move(producer).close(ctx);
                      std::move(consumer).close(ctx);
                  });
}

// The timed push gives PushRefusal::Full at its deadline when no consumer
// pops, and it pushes when a pop freed a cell.
void expect_the_timed_push_ends_at_its_deadline(::fixy::BgLoadCtx const& ctx) {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::wire, WireTag>>();
    using Parked = cc::ParkedChannel<cc::channel_t<Item, 2, decltype(root)>>;
    Parked channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Parked::producer_tag, Parked::consumer_tag>(std::move(root));
    cc::with_ends(
        channel, std::move(producer_perm), std::move(consumer_perm), [&ctx](auto producer, auto consumer) noexcept {
            auto const clock = ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::Monotonic>(ctx);
            eff::cap::Nondet const nondet = nondet_of(ctx);
            Item item = 0;
            CRUCIBLE_FATAL_INVARIANT(fill(ctx, producer, item) == cc::PushRefusal::Full);
            ::fixy::futex::Deadline const soon =
                ::fixy::futex::Deadline::after(clock, nondet, kTimedStepNanos).expect("the clock reads");
            CRUCIBLE_FATAL_INVARIANT(is_refused_as(producer.template push_waiting_until<ParkAtOnce>(ctx, item, soon),
                                                   cc::PushRefusal::Full));
            CRUCIBLE_FATAL_INVARIANT(std::move(consumer.try_pop(ctx)).expect("the ring is full") == 0);
            ::fixy::futex::Deadline const later =
                ::fixy::futex::Deadline::after(clock, nondet, kTimedStepNanos).expect("the clock reads");
            CRUCIBLE_FATAL_INVARIANT(producer.template push_waiting_until<ParkAtOnce>(ctx, item, later).is_ok());
            std::move(producer).close(ctx);
            std::move(consumer).close(ctx);
        });
}

}  // namespace

int main() {
    ::fixy::BgLoadCtx const ctx{eff::testing::bg()};
    constexpr ParkWait kAtStride = ParkWait::producer_at_stride;
    expect_each_item_through_a_full_wire<4>(ctx,
                                            ConsumerPlan{PopDoor::receive_waiting, Pace::with_delays, kAtStride, 11});
    expect_each_item_through_a_full_wire<4>(
        ctx, ConsumerPlan{PopDoor::receive_waiting_until, Pace::with_delays, kAtStride, 12});
    expect_each_item_through_a_full_wire<4>(ctx, ConsumerPlan{PopDoor::try_receive, Pace::with_delays, kAtStride, 13});
    expect_each_item_through_a_full_wire<4>(ctx, ConsumerPlan{PopDoor::try_pop, Pace::with_delays, kAtStride, 14});
    expect_each_item_through_a_full_wire<1>(ctx,
                                            ConsumerPlan{PopDoor::receive_waiting, Pace::at_once, ParkWait::none, 15});
    expect_a_slow_producer_wakes_a_parked_consumer(ctx);
    expect_consumer_gone_ends_a_parked_producer(ctx);
    expect_a_parked_producer_waits_through_a_consumer_release(ctx);
    expect_a_push_after_a_consumer_release_reads_full(ctx);
    expect_a_producer_parks_before_the_first_consumer(ctx);
    expect_release_keeps_the_stream_open(ctx);
    expect_the_timed_receive_ends_at_its_deadline(ctx);
    expect_the_timed_push_ends_at_its_deadline(ctx);
    return 0;
}
