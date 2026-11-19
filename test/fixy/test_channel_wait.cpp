// The wait of the consumer handle of fixy/concurrent/PermissionedChannel.h:
// receive_waiting waits by its policy while the channel is empty and open,
// and it gives each item in order and then the end.  The producers push with
// a random delay of a Philox stream and notify the event count after each
// push and after their close.  A lost wake leaves the consumer parked, and
// the test then fails at the timeout of the test preset.

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
    typename Wire::ProducerHandle producer = channel.producer(std::move(producer_perm));
    typename Wire::ConsumerHandle consumer = channel.consumer(std::move(consumer_perm));
    std::uint64_t received = 0;
    bool is_ordered = true;
    auto produce = [&]() noexcept {
        ::crucible::test::PhiloxStream delays{seed};
        for (Item item = 0; item < kItems; ++item) {
            delay(delays);
            while (!producer.try_push(item)) {
                CRUCIBLE_SPIN_PAUSE;
            }
            events.notify_one(ctx);
        }
        std::move(producer).close();
        events.notify_one(ctx);
    };
    auto consume = [&]() noexcept {
        for (;;) {
            ::fixy::Result<Item, cc::NoItem> got = consumer.template receive_waiting<Policy>(ctx, events);
            if (got.is_err()) {
                is_ordered = is_ordered && got.err().value_or(cc::NoItem::Empty) == cc::NoItem::Closed;
                return;
            }
            is_ordered = is_ordered && std::move(got).expect("the test above found an item") == received;
            ++received;
        }
    };
    CRUCIBLE_FATAL_INVARIANT(::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, produce, consume).is_ok());
    CRUCIBLE_FATAL_INVARIANT(is_ordered);
    CRUCIBLE_FATAL_INVARIANT(received == kItems);
    ::foundation::linear::drop(std::move(consumer));
}

// A merge of 8 cells: two producers, one consumer, each item of each producer
// in the order of that producer, then the end when both producers closed.
void expect_each_item_then_the_end_of_a_merge(::fixy::BgLoadCtx const& ctx, std::uint64_t seed) {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::merge, MergeTag>>();
    using Merge = cc::channel_t<Item, 8, decltype(root)>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Merge::producer_tag, Merge::consumer_tag>(std::move(root));
    Merge channel{std::move(producer_root)};
    cc::EventCount events{};
    typename Merge::ConsumerHandle consumer = channel.consumer(std::move(consumer_perm));
    eff::cap::Nondet const merge = eff::extract_bare_from_ctx<eff::Effect::Nondet>(ctx);
    ::fixy::FixedArray<Item, 2> next{};
    std::uint64_t received = 0;
    bool is_ordered = true;
    // Both producers open the channel before the threads start, so the
    // channel closes only after the second close.
    typename Merge::ProducerHandle first_producer = channel.producer().expect("the pool lends a producer");
    typename Merge::ProducerHandle second_producer = channel.producer().expect("the pool lends a producer");
    auto produce = [&](typename Merge::ProducerHandle& producer, Item producer_bit, std::uint64_t stream) noexcept {
        ::crucible::test::PhiloxStream delays{stream};
        for (Item item = 0; item < kItems; ++item) {
            delay(delays);
            while (!producer.try_push((item << 1) | producer_bit)) {
                CRUCIBLE_SPIN_PAUSE;
            }
            events.notify_one(ctx);
        }
        std::move(producer).close();
        events.notify_one(ctx);
    };
    auto first = [&]() noexcept { produce(first_producer, 0, seed); };
    auto second = [&]() noexcept { produce(second_producer, 1, seed + 1); };
    auto consume = [&]() noexcept {
        for (;;) {
            ::fixy::Result<Item, cc::NoItem> got = consumer.template receive_waiting<ParkAtOnce>(ctx, events, merge);
            if (got.is_err()) {
                is_ordered = is_ordered && got.err().value_or(cc::NoItem::Empty) == cc::NoItem::Closed;
                return;
            }
            Item const value = std::move(got).expect("the test above found an item");
            Item& expected = *next.get(value & 1).expect("a producer bit is below two");
            is_ordered = is_ordered && (value >> 1) == expected;
            ++expected;
            ++received;
        }
    };
    CRUCIBLE_FATAL_INVARIANT(
        ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, first, second, consume).is_ok());
    CRUCIBLE_FATAL_INVARIANT(is_ordered);
    CRUCIBLE_FATAL_INVARIANT(received == 2 * kItems);
    ::foundation::linear::drop(std::move(consumer));
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
