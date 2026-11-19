// The wait of the consumer handle of fixy/concurrent/PermissionedChannel.h:
// receive_waiting waits by its policy while the channel is empty and open,
// and it gives each item in order and then the end.  The producers push on
// threads of their own with a random delay of a Philox stream, and they
// notify the event count after each push and after their close.  Each
// producer runs beside the consumer through run_beside of fixy/os/ThreadTasks.h,
// and the consumer receives on the calling thread.  A lost wake leaves the consumer
// parked, and the test then fails at the timeout of the test preset.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/EventCount.h>
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
using SpinFirst = cc::SpinThenPark<2000>;
using Item = std::uint64_t;

struct WireTag {};
struct MergeTag {};

constexpr std::uint64_t kItems = 4000;

// A delay of the producer: short for most items and long for some, so the
// consumer parks after its budget.
void delay(::crucible::test::PhiloxStream& delays) noexcept {
    std::uint64_t const draw = delays.below(100);
    std::uint64_t const turns = draw < 90 ? delays.below(200) : 2000 + delays.below(10000);
    for (std::uint64_t turn = 0; turn < turns; ++turn) {
        CRUCIBLE_SPIN_PAUSE;
    }
}

// The gate: a hot context takes Spin through the handle and no policy that
// parks.
template <class Handle, class Policy, class Ctx>
concept CanReceiveWaiting = requires(Handle& handle, Ctx const& ctx, cc::EventCount& events) {
    handle.template receive_waiting<Policy>(ctx, events);
};

// The lane of a producer: the low bit of each item, and the stream of its
// delays.
enum class Lane : std::uint8_t {
    first = 0,
    second = 1,
};

// The body of a producer of kItems items.  The body owns the handle, pushes
// each item, notifies after each push, and closes the stream at its end.  The
// item is its count shifted left by one, with the bit of the lane.  It takes
// the context of the call of run_beside.
template <class Producer>
[[nodiscard]] auto make_producer(cc::EventCount& events, Producer&& producer, Lane lane, std::uint64_t seed) noexcept {
    return [&events, lane, seed, handle = std::move(producer)](::fixy::BgLoadCtx const& ctx) mutable noexcept {
        auto const lane_bit = static_cast<Item>(lane);
        ::crucible::test::PhiloxStream delays{seed + lane_bit};
        for (Item item = 0; item < kItems; ++item) {
            delay(delays);
            while (!handle.try_push((item << 1) | lane_bit)) {
                CRUCIBLE_SPIN_PAUSE;
            }
            events.notify_one(ctx);
        }
        std::move(handle).close();
        events.notify_one(ctx);
    };
}

// The result of the consumer of a test.
struct Received {
    std::uint64_t count = 0;
    bool is_ordered = true;
};

// Receives until the end of the stream, with the tag of each pop, and checks
// the order of each lane.
template <class Policy, class Consumer, typename... PopTag>
[[nodiscard]] Received receive_each(::fixy::BgLoadCtx const& ctx, cc::EventCount& events, Consumer& consumer,
                                    PopTag... tag) noexcept {
    Received result{};
    ::fixy::FixedArray<Item, 2> next{};
    bool is_open = true;
    while (is_open) {
        ::fixy::Result<Item, cc::NoItem> got = consumer.template receive_waiting<Policy>(ctx, events, tag...);
        if (got.is_err()) {
            result.is_ordered = result.is_ordered && got.err().value_or(cc::NoItem::Empty) == cc::NoItem::Closed;
            is_open = false;
            continue;
        }
        Item const value = std::move(got).expect("the test above found an item");
        Item& expected = *next.get(value & 1).expect("a lane bit is below two");
        result.is_ordered = result.is_ordered && (value >> 1) == expected;
        ++expected;
        ++result.count;
    }
    return result;
}

// A wire of 8 cells: one producer, one consumer, each item in order, then the
// end of the stream.
template <class Policy>
void expect_each_item_then_the_end_of_a_wire(::fixy::BgLoadCtx const& ctx, std::uint64_t seed) {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::wire, WireTag>>();
    using Wire = cc::channel_t<Item, 8, decltype(root)>;
    static_assert(CanReceiveWaiting<typename Wire::ConsumerHandle, cc::Spin, ::fixy::HotFgCtx>);
    static_assert(!CanReceiveWaiting<typename Wire::ConsumerHandle, ParkAtOnce, ::fixy::HotFgCtx>);
    static_assert(CanReceiveWaiting<typename Wire::ConsumerHandle, ParkAtOnce, ::fixy::BgLoadCtx>);
    Wire channel{};
    cc::EventCount events{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Wire::producer_tag, Wire::consumer_tag>(std::move(root));
    typename Wire::ConsumerHandle consumer = channel.consumer(std::move(consumer_perm));
    auto values =
        ::fixy::thread::run_beside(ctx, ::fixy::thread::ThreadOptions{},
                                   make_producer(events, channel.producer(std::move(producer_perm)), Lane::first, seed),
                                   [&ctx, &events, owned = std::move(consumer)]() mutable noexcept {
                                       Received const got = receive_each<Policy>(ctx, events, owned);
                                       ::foundation::linear::drop(std::move(owned));
                                       return got;
                                   })
            .expect("the thread door starts a producer");
    Received const received = values.at(::fixy::index<1>());
    CRUCIBLE_FATAL_INVARIANT(received.is_ordered);
    CRUCIBLE_FATAL_INVARIANT(received.count == kItems);
}

// Receives each item of a merge beside its second producer, which runs
// through run_beside, and ends the consumer end.
template <class Consumer, class Producer>
[[nodiscard]] Received receive_beside(::fixy::BgLoadCtx const& ctx, cc::EventCount& events, Consumer&& consumer,
                                      eff::cap::Nondet merge, Producer&& producer) noexcept {
    auto values =
        ::fixy::thread::run_beside(ctx, ::fixy::thread::ThreadOptions{}, std::move(producer),
                                   [&ctx, &events, receiving = std::move(consumer), merge]() mutable noexcept {
                                       Received const got = receive_each<ParkAtOnce>(ctx, events, receiving, merge);
                                       ::foundation::linear::drop(std::move(receiving));
                                       return got;
                                   })
            .expect("the thread door starts the second producer");
    return values.at(::fixy::index<1>());
}

// The continuation of the first producer of a merge: it owns the consumer end
// and the second producer, and it receives beside the second producer.
template <class Consumer, class Producer>
[[nodiscard]] auto make_receive_beside(::fixy::BgLoadCtx const& ctx, cc::EventCount& events, Consumer&& consumer,
                                       eff::cap::Nondet merge, Producer&& producer) noexcept {
    return [&ctx, &events, owned = std::move(consumer), merge, second = std::move(producer)]() mutable noexcept {
        return receive_beside(ctx, events, std::move(owned), merge, std::move(second));
    };
}

// A merge of 8 cells: two producers, one consumer, each item of each producer
// in the order of that producer, then the end when both producers closed.
// Both producers open the channel before their threads start, so the channel
// closes only after the second close.
void expect_each_item_then_the_end_of_a_merge(::fixy::BgLoadCtx const& ctx, std::uint64_t seed) {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::merge, MergeTag>>();
    using Merge = cc::channel_t<Item, 8, decltype(root)>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Merge::producer_tag, Merge::consumer_tag>(std::move(root));
    Merge channel{std::move(producer_root)};
    cc::EventCount events{};
    typename Merge::ConsumerHandle consumer = channel.consumer(std::move(consumer_perm));
    eff::cap::Nondet const merge = eff::extract_bare_from_ctx<eff::Effect::Nondet>(ctx);
    typename Merge::ProducerHandle first_producer = channel.producer().expect("the pool lends a producer");
    typename Merge::ProducerHandle second_producer = channel.producer().expect("the pool lends a producer");
    auto receive_beside_second = make_receive_beside(
        ctx, events, std::move(consumer), merge, make_producer(events, std::move(second_producer), Lane::second, seed));
    auto values = ::fixy::thread::run_beside(ctx, ::fixy::thread::ThreadOptions{},
                                             make_producer(events, std::move(first_producer), Lane::first, seed),
                                             std::move(receive_beside_second))
                      .expect("the thread door starts the first producer");
    Received const received = values.at(::fixy::index<1>());
    CRUCIBLE_FATAL_INVARIANT(received.is_ordered);
    CRUCIBLE_FATAL_INVARIANT(received.count == 2 * kItems);
}

}  // namespace

int main() {
    ::fixy::BgLoadCtx const ctx{eff::testing::bg()};
    expect_each_item_then_the_end_of_a_wire<ParkAtOnce>(ctx, 11);
    expect_each_item_then_the_end_of_a_wire<SpinFirst>(ctx, 12);
    expect_each_item_then_the_end_of_a_wire<cc::Spin>(ctx, 13);
    expect_each_item_then_the_end_of_a_merge(ctx, 21);
    return 0;
}
