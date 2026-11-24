// The merge form of the parked channel (fixy/concurrent/ParkedChannel.h over a
// merge of fixy/concurrent/PermissionedChannel.h).  Three producer threads
// each open a frame of their own, which lends a share of the pool, and one
// consumer pops.  Each pop that takes an item notifies room once and wakes one
// parked producer, the close of the last producer ends the stream, and the
// close of the consumer wakes each parked producer.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/ParkedChannel.h>
#include <fixy/concurrent/PermissionedBoxChannel.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Linear.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include "../foundation/philox_stream.h"

#include <cstdint>
#include <utility>

namespace {

namespace eff = ::foundation::effects;
namespace cc = ::fixy::concurrent;
namespace perm = ::foundation::permissions;

using ParkAtOnce = cc::SpinThenPark<0>;
using Item = std::uint64_t;

struct MergeTag {};

constexpr std::uint64_t kProducers = 3;
constexpr std::uint64_t kItemsEach = 1000;
// The producers and the Boxes of each producer of the merge of Box objects.
constexpr std::uint64_t kBoxProducers = 2;
constexpr std::uint64_t kBoxesEach = 500;

using ItemBox = ::fixy::Box<Item>;
using BoxRefused = cc::PushRefused<ItemBox>;
// The turns of the pause after the producers report their wait.
constexpr std::uint64_t kSettleTurns = 60000;

[[nodiscard]] eff::cap::Nondet nondet_of(::fixy::BgLoadCtx const& ctx) noexcept {
    return eff::extract_bare_from_ctx<eff::Effect::Nondet>(ctx);
}

// The item of a producer: its index in the high word and its place in the low
// word.
[[nodiscard]] Item item_of(std::uint64_t producer, std::uint64_t place) noexcept { return (producer << 32) | place; }

// A delay of the consumer: short for most items and long for some, so the
// producers wait for room.
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
    return std::move(lent).expect("the merge lends a producer while its root is in the pool");
}

// True when the ring took the Box.  A refusal ends its Box here.
[[nodiscard]] bool is_pushed(::fixy::Result<::fixy::Unit, BoxRefused>&& pushed) noexcept {
    return std::move(pushed).match([](::fixy::Unit) noexcept { return true; },
                                   [](BoxRefused refused) noexcept {
                                       ::foundation::linear::drop(std::move(refused));
                                       return false;
                                   });
}

// The value of a Box.  The Box can be a temporary, which lives until the end
// of the full expression of the call.
[[nodiscard]] Item value_of(ItemBox const& box) noexcept { return *box; }

// Waits until count reaches the goal.
void wait_until(::fixy::Atomic<std::uint64_t> const& count, std::uint64_t goal) noexcept {
    while (count.load_acquire() < goal) {
        CRUCIBLE_SPIN_PAUSE;
    }
}

// The count of the consumer: the items, the items that came out of the order
// of their producer, and whether the end came after the last item.
struct MergeCount {
    std::uint64_t received = 0;
    std::uint64_t out_of_order = 0;
};

// The next place of each producer, read by the index of the producer.
struct NextPlaces {
    std::uint64_t first = 0;
    std::uint64_t second = 0;
    std::uint64_t third = 0;

    // True when item comes at the next place of its producer, and the place
    // after it is then the next one.
    [[nodiscard]] bool take(Item item) noexcept {
        std::uint64_t const producer = item >> 32;
        std::uint64_t const place = item & 0xFFFFFFFFu;
        std::uint64_t& next = producer == 0 ? first : (producer == 1 ? second : third);
        bool const is_next = producer < kProducers && place == next;
        next = place + 1;
        return is_next;
    }
};

// Three producers push into a merge of 4 cells with push_waiting, and a slow
// consumer takes each item with receive_waiting.  The producers park on room,
// and each pop wakes one of them.  Each producer opens its frame and waits for
// the others before its first push, so no producer closes the stream before
// each one opened.  The stream ends at the close of the last producer, and
// the items of each producer come in its order.
void expect_three_producers_through_a_full_merge(::fixy::BgLoadCtx const& ctx) {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::merge, MergeTag>>();
    using Parked = cc::ParkedChannel<cc::channel_t<Item, 4, decltype(root)>>;
    static_assert(Parked::is_merge);
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Parked::producer_tag, Parked::consumer_tag>(std::move(root));
    Parked channel{std::move(producer_root)};
    // The count of the producers that opened their frame.
    ::fixy::Atomic<std::uint64_t> opened{0};
    MergeCount const count =
        cc::with_consumer(channel, std::move(consumer_perm), [&ctx, &channel, &opened](auto consumer) noexcept {
            auto feed = [&channel, &opened](std::uint64_t producer) noexcept {
                return [&channel, &opened, producer](::fixy::BgLoadCtx const& bg) noexcept {
                    static_cast<void>(expect_lent(
                        cc::with_producer(channel, nondet_of(bg), [&bg, &opened, producer](auto handle) noexcept {
                            static_cast<void>(opened.fetch_add_acq_rel(1));
                            wait_until(opened, kProducers);
                            for (std::uint64_t place = 0; place < kItemsEach; ++place) {
                                CRUCIBLE_FATAL_INVARIANT(
                                    handle.template push_waiting<ParkAtOnce>(bg, item_of(producer, place)).is_ok());
                            }
                            std::move(handle).close(bg);
                        })));
                };
            };
            auto drain = [owned = std::move(consumer)](::fixy::BgLoadCtx const& bg) mutable noexcept {
                ::crucible::test::PhiloxStream delays{71};
                MergeCount got{};
                NextPlaces next{};
                ::fixy::Result<Item, cc::NoItem> received = owned.template receive_waiting<ParkAtOnce>(bg);
                while (received.is_ok()) {
                    if (!next.take(std::move(received).expect("the test above found an item"))) ++got.out_of_order;
                    ++got.received;
                    delay(delays);
                    received = owned.template receive_waiting<ParkAtOnce>(bg);
                }
                std::move(owned).close(bg);
                return got;
            };
            auto joined = ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, feed(0), feed(1), feed(2),
                                                     std::move(drain))
                              .expect("the test starts the three producers and the consumer");
            return joined.at(::fixy::index<3>());
        });
    CRUCIBLE_FATAL_INVARIANT(count.received == kProducers * kItemsEach);
    CRUCIBLE_FATAL_INVARIANT(count.out_of_order == 0);
}

// Three producers park on a full merge of 2 cells.  The consumer closes after
// each producer reports its wait and some more turns, and its close must wake
// each of them: each ends with ConsumerGone.  A close that woke one producer
// would leave two parked.
void expect_a_consumer_close_wakes_each_parked_producer(::fixy::BgLoadCtx const& ctx) {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::merge, MergeTag>>();
    using Parked = cc::ParkedChannel<cc::channel_t<Item, 2, decltype(root)>>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Parked::producer_tag, Parked::consumer_tag>(std::move(root));
    Parked channel{std::move(producer_root)};
    // The count of the producers that start their wait.
    ::fixy::Atomic<std::uint64_t> waiting{0};
    std::uint64_t const gone =
        cc::with_consumer(channel, std::move(consumer_perm), [&ctx, &channel, &waiting](auto consumer) noexcept {
            auto wait_to_push = [&channel, &waiting](std::uint64_t producer) noexcept {
                return [&channel, &waiting, producer](::fixy::BgLoadCtx const& bg) noexcept {
                    return expect_lent(
                        cc::with_producer(channel, nondet_of(bg), [&bg, &waiting, producer](auto handle) noexcept {
                            std::uint64_t place = 0;
                            while (handle.try_push(bg, item_of(producer, place)).is_ok()) {
                                ++place;
                            }
                            static_cast<void>(waiting.fetch_add_acq_rel(1));
                            bool const is_gone = handle.template push_waiting<ParkAtOnce>(bg, item_of(producer, place))
                                                     .is_err_and([](cc::PushRefusal why) noexcept {
                                                         return why == cc::PushRefusal::ConsumerGone;
                                                     });
                            std::move(handle).close(bg);
                            return is_gone ? std::uint64_t{1} : std::uint64_t{0};
                        }));
                };
            };
            auto close_after_the_waits = [&waiting,
                                          owned = std::move(consumer)](::fixy::BgLoadCtx const& bg) mutable noexcept {
                wait_until(waiting, kProducers);
                for (std::uint64_t turn = 0; turn < kSettleTurns; ++turn) {
                    CRUCIBLE_SPIN_PAUSE;
                }
                std::move(owned).close(bg);
                return std::uint64_t{0};
            };
            auto joined = ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, wait_to_push(0),
                                                     wait_to_push(1), wait_to_push(2), std::move(close_after_the_waits))
                              .expect("the test starts the three producers and the consumer");
            return joined.at(::fixy::index<0>()) + joined.at(::fixy::index<1>()) + joined.at(::fixy::index<2>());
        });
    CRUCIBLE_FATAL_INVARIANT(gone == kProducers);
}

// Two producer threads push Boxes into a merge of 4 Box cells with
// push_waiting, and a slow consumer takes each Box with receive_waiting.  Each
// Box comes one time, in the order of its producer, and then the end.
void expect_two_box_producers_through_a_full_merge(::fixy::BgLoadCtx const& ctx) {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::merge, MergeTag>>();
    using Parked = cc::ParkedChannel<cc::box_channel_t<Item, 4, decltype(root)>>;
    static_assert(Parked::is_merge && Parked::moves_its_items);
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Parked::producer_tag, Parked::consumer_tag>(std::move(root));
    Parked channel{std::move(producer_root)};
    // The count of the producers that opened their frame.
    ::fixy::Atomic<std::uint64_t> opened{0};
    MergeCount const count =
        cc::with_consumer(channel, std::move(consumer_perm), [&ctx, &channel, &opened](auto consumer) noexcept {
            auto feed = [&channel, &opened](std::uint64_t producer) noexcept {
                return [&channel, &opened, producer](::fixy::BgLoadCtx const& bg) noexcept {
                    static_cast<void>(expect_lent(
                        cc::with_producer(channel, nondet_of(bg), [&bg, &opened, producer](auto handle) noexcept {
                            static_cast<void>(opened.fetch_add_acq_rel(1));
                            wait_until(opened, kBoxProducers);
                            for (std::uint64_t place = 0; place < kBoxesEach; ++place) {
                                ItemBox box =
                                    ::fixy::mint_box<Item>(eff::testing::test().alloc, item_of(producer, place));
                                CRUCIBLE_FATAL_INVARIANT(
                                    is_pushed(handle.template push_waiting<ParkAtOnce>(bg, std::move(box))));
                            }
                            std::move(handle).close(bg);
                        })));
                };
            };
            auto drain = [owned = std::move(consumer)](::fixy::BgLoadCtx const& bg) mutable noexcept {
                ::crucible::test::PhiloxStream delays{81};
                MergeCount got{};
                NextPlaces next{};
                ::fixy::Result<ItemBox, cc::NoItem> received = owned.template receive_waiting<ParkAtOnce>(bg);
                while (received.is_ok()) {
                    if (!next.take(value_of(std::move(received).expect("the test above found a Box")))) {
                        ++got.out_of_order;
                    }
                    ++got.received;
                    delay(delays);
                    received = owned.template receive_waiting<ParkAtOnce>(bg);
                }
                std::move(owned).close(bg);
                return got;
            };
            auto joined =
                ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, feed(0), feed(1), std::move(drain))
                    .expect("the test starts the two producers and the consumer");
            return joined.at(::fixy::index<2>());
        });
    CRUCIBLE_FATAL_INVARIANT(count.received == kBoxProducers * kBoxesEach);
    CRUCIBLE_FATAL_INVARIANT(count.out_of_order == 0);
}

}  // namespace

int main() {
    ::fixy::BgLoadCtx const ctx{eff::testing::bg()};
    expect_three_producers_through_a_full_merge(ctx);
    expect_a_consumer_close_wakes_each_parked_producer(ctx);
    expect_two_box_producers_through_a_full_merge(ctx);
    return 0;
}
