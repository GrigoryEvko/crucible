// The MPSC channel of Box values, and the drain of the MPSC channel under it:
// the push of a Box and the pop of the same object, the give-back of a full
// ring, the end of the stream after the last producer, the end of each object
// that the ring holds at the end of the channel, the drain in the order of
// the ring and its refusal while a handle lives, and three producer threads
// that move many Boxes to one consumer thread.  The tsan preset runs the
// threaded case.
//
// Each test mints the root of its channel where it names the type of the
// channel, so the brand of the root stays in that scope.  A test ends each
// consumer handle at the drop door of foundation/Linear.h, and each producer
// handle at the end of its scope.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/concurrent/ChannelEnd.h>
#include <fixy/concurrent/HandleTraits.h>
#include <fixy/concurrent/PermissionedMpscBoxChannel.h>
#include <fixy/concurrent/PermissionedMpscChannel.h>
#include <fixy/os/ThreadTasks.h>

#include <foundation/Linear.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace c = fixy::concurrent;
namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {

struct ParcelTag {};
struct BulkTag {};
struct WordTag {};

// The count of the live parcels, on each thread.  Each object must end one
// time, and no object may leak.
fixy::Atomic<std::int64_t> live_parcels{0};

class Parcel {
public:
    explicit Parcel(std::uint64_t label) noexcept : label_{label} { (void)live_parcels.fetch_add_acq_rel(1); }
    Parcel(Parcel const&) = delete("a parcel ends one time, so it has one owner");
    Parcel& operator=(Parcel const&) = delete("a parcel ends one time, so it has one owner");
    ~Parcel() { (void)live_parcels.fetch_sub_acq_rel(1); }

    [[nodiscard]] std::uint64_t label() const noexcept { return label_; }

private:
    std::uint64_t label_ = 0;
};

[[nodiscard]] fixy::Box<Parcel> make_parcel(std::uint64_t label) noexcept {
    return fixy::mint_box<Parcel>(eff::testing::test().alloc, label);
}

// The label of the object of a Box that a receive gave, or kNoLabel.
inline constexpr std::uint64_t kNoLabel = ~std::uint64_t{0};

[[nodiscard]] std::uint64_t label_of(fixy::Option<fixy::Box<Parcel>> const& box) noexcept {
    return box.match([](fixy::Box<Parcel> const& held) noexcept { return held->label(); },
                     []() noexcept { return kNoLabel; });
}

// A producer handle of the channel.  A test lends while no exclusive is out,
// so an empty lend ends the process.
template <class Channel>
[[nodiscard]] typename Channel::ProducerHandle lend(Channel& channel) noexcept {
    return channel.producer().expect("the channel lends a producer while no exclusive is out");
}

// Two producers push into one ring, and the pop gives a Box of each object.
// A full ring gives the Box of the push back, with its object.
void push_pop_and_full_ring(fixy::TestRunnerCtx const& test) noexcept {
    {
        auto root = perm::mint_permission_root<c::mpsc_tag::Whole<ParcelTag>>();
        using Channel = c::mpsc_box_channel_t<Parcel, 2, decltype(root)>;
        // The two handles have the shapes that a stage reads, with a Box as
        // the payload, and they name one channel.
        static_assert(c::is_producer_handle_v<Channel::ProducerHandle>);
        static_assert(c::is_consumer_handle_v<Channel::ConsumerHandle>);
        static_assert(std::is_same_v<c::producer_handle_value_t<Channel::ProducerHandle>, fixy::Box<Parcel>>);
        static_assert(std::is_same_v<c::consumer_handle_value_t<Channel::ConsumerHandle>, fixy::Box<Parcel>>);
        static_assert(c::HandlesShareChannel<Channel::ProducerHandle, Channel::ConsumerHandle>);
        // Each handle states the capacity of the channel.
        static_assert(Channel::ProducerHandle::channel_capacity == 2 && Channel::ConsumerHandle::channel_capacity == 2);
        static_assert(!std::is_copy_constructible_v<Channel::ProducerHandle>);
        static_assert(!std::is_move_assignable_v<Channel::ConsumerHandle>);
        static_assert(!std::is_move_constructible_v<Channel>);
        auto [producer_root, consumer_perm] =
            perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(std::move(root));
        Channel channel{std::move(producer_root)};
        auto consumer = channel.consumer(std::move(consumer_perm));
        // A pop of an MPSC channel is a merge, so it takes the tag of Nondet.
        const eff::cap::Nondet merge = eff::testing::test().nondet;
        static_assert(c::is_merge_consumer_handle_v<Channel::ConsumerHandle>);
        {
            auto first = lend(channel);
            auto second = lend(channel);
            CRUCIBLE_TEST_ASSERT(test, channel.outstanding_producers() == 2);
            CRUCIBLE_TEST_ASSERT(test, consumer.try_pop(merge).is_none());
            CRUCIBLE_TEST_ASSERT(test, first.try_push(make_parcel(1)).is_none());
            CRUCIBLE_TEST_ASSERT(test, second.try_push(make_parcel(2)).is_none());
            fixy::Option<fixy::Box<Parcel>> const refused = first.try_push(make_parcel(3));
            CRUCIBLE_TEST_ASSERT(test, label_of(refused) == 3);
            CRUCIBLE_TEST_ASSERT(test, live_parcels.load_acquire() == 3);

            fixy::Option<fixy::Box<Parcel>> const one = consumer.try_pop(merge);
            fixy::Option<fixy::Box<Parcel>> const two = consumer.try_pop(merge);
            CRUCIBLE_TEST_ASSERT(test, label_of(one) == 1);
            CRUCIBLE_TEST_ASSERT(test, label_of(two) == 2);
            CRUCIBLE_TEST_ASSERT(test, consumer.try_pop(merge).is_none());
        }
        ::foundation::linear::drop(std::move(consumer));
    }
    CRUCIBLE_TEST_ASSERT(test, live_parcels.load_acquire() == 0);
}

// The end of the last producer handle closes the channel, and the end of a
// different producer does not.  try_receive gives each Box that the ring still
// holds, and then Closed.
void receive_to_the_end_of_the_stream(fixy::TestRunnerCtx const& test) noexcept {
    {
        auto root = perm::mint_permission_root<c::mpsc_tag::Whole<ParcelTag>>();
        using Channel = c::mpsc_box_channel_t<Parcel, 4, decltype(root)>;
        auto [producer_root, consumer_perm] =
            perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(std::move(root));
        Channel channel{std::move(producer_root)};
        auto consumer = channel.consumer(std::move(consumer_perm));
        const eff::cap::Nondet merge = eff::testing::test().nondet;
        CRUCIBLE_TEST_ASSERT(test, consumer.try_receive(merge).err() == fixy::some(c::NoItem::Empty));
        {
            auto second = lend(channel);
            {
                auto first = lend(channel);
                CRUCIBLE_TEST_ASSERT(test, first.try_push(make_parcel(5)).is_none());
            }
            CRUCIBLE_TEST_ASSERT(test, !channel.is_closed());
            CRUCIBLE_TEST_ASSERT(test, second.try_push(make_parcel(6)).is_none());
        }
        CRUCIBLE_TEST_ASSERT(test, channel.is_closed());
        fixy::Option<fixy::Box<Parcel>> const fifth = consumer.try_receive(merge).ok();
        fixy::Option<fixy::Box<Parcel>> const sixth = consumer.try_receive(merge).ok();
        CRUCIBLE_TEST_ASSERT(test, label_of(fifth) == 5);
        CRUCIBLE_TEST_ASSERT(test, label_of(sixth) == 6);
        CRUCIBLE_TEST_ASSERT(test, consumer.try_receive(merge).err() == fixy::some(c::NoItem::Closed));
        ::foundation::linear::drop(std::move(consumer));
    }
    CRUCIBLE_TEST_ASSERT(test, live_parcels.load_acquire() == 0);
}

// The end of the channel ends each object that the ring still holds.  The
// handles end first, so no handle lives at the drain.
void end_of_channel_ends_each_object(fixy::TestRunnerCtx const& test) noexcept {
    {
        auto root = perm::mint_permission_root<c::mpsc_tag::Whole<ParcelTag>>();
        using Channel = c::mpsc_box_channel_t<Parcel, 4, decltype(root)>;
        auto [producer_root, consumer_perm] =
            perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(std::move(root));
        Channel channel{std::move(producer_root)};
        auto consumer = channel.consumer(std::move(consumer_perm));
        {
            auto first = lend(channel);
            auto second = lend(channel);
            CRUCIBLE_TEST_ASSERT(test, first.try_push(make_parcel(7)).is_none());
            CRUCIBLE_TEST_ASSERT(test, second.try_push(make_parcel(8)).is_none());
        }
        ::foundation::linear::drop(std::move(consumer));
        CRUCIBLE_TEST_ASSERT(test, live_parcels.load_acquire() == 2);
    }
    CRUCIBLE_TEST_ASSERT(test, live_parcels.load_acquire() == 0);
}

// The labels of the entries that ended, in the order of their ends.
inline constexpr std::size_t kLogCapacity = 8;
fixy::FixedArray<std::uint64_t, kLogCapacity> end_log{};
std::size_t end_count = 0;

// An object that writes its label to the log at its end.
class Entry {
public:
    explicit Entry(std::uint64_t label) noexcept : label_{label} {}
    Entry(Entry const&) = delete("an entry ends one time, so it has one owner");
    Entry& operator=(Entry const&) = delete("an entry ends one time, so it has one owner");
    ~Entry() {
        for (auto const& slot : end_log.view().get(end_count)) {
            *slot = label_;
        }
        ++end_count;
    }

private:
    std::uint64_t label_ = 0;
};

[[nodiscard]] fixy::Box<Entry> make_entry(std::uint64_t label) noexcept {
    return fixy::mint_box<Entry>(eff::testing::test().alloc, label);
}

// The end of the channel drains the ring of the words under it, and the drain
// gives each word the oldest first, so the entries end in the order of their
// pushes, across two producers.
void drain_gives_each_item_in_order(fixy::TestRunnerCtx const& test) noexcept {
    end_count = 0;
    {
        auto root = perm::mint_permission_root<c::mpsc_tag::Whole<WordTag>>();
        using Channel = c::mpsc_box_channel_t<Entry, 4, decltype(root)>;
        auto [producer_root, consumer_perm] =
            perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(std::move(root));
        Channel channel{std::move(producer_root)};
        auto consumer = channel.consumer(std::move(consumer_perm));
        {
            auto first = lend(channel);
            auto second = lend(channel);
            CRUCIBLE_TEST_ASSERT(test, first.try_push(make_entry(11)).is_none());
            CRUCIBLE_TEST_ASSERT(test, second.try_push(make_entry(12)).is_none());
            CRUCIBLE_TEST_ASSERT(test, first.try_push(make_entry(13)).is_none());
        }
        ::foundation::linear::drop(std::move(consumer));
        CRUCIBLE_TEST_ASSERT(test, end_count == 0);
    }
    CRUCIBLE_TEST_ASSERT(test, end_count == 3);
    std::uint64_t expected = 11;
    bool is_in_order = true;
    for (std::uint64_t const label : end_log) {
        if (expected < 14) is_in_order = is_in_order && label == expected;
        ++expected;
    }
    CRUCIBLE_TEST_ASSERT(test, is_in_order);
}

// A drain while a handle of one role lives ends the process, so no handle can
// push or pop while the drain claims the ring.  Each drain runs in a child
// through expect_dies of the Check family.  The first child ends a Box channel
// while a producer handle lives, and the drain of the end refuses.  The second
// child drains a plain MPSC channel while its consumer handle lives.
void drain_refuses_a_live_handle(fixy::TestRunnerCtx const& test) noexcept {
    ::fixy::check::expect_dies(
        test,
        []() noexcept {
            auto root = perm::mint_permission_root<c::mpsc_tag::Whole<WordTag>>();
            using Channel = c::mpsc_box_channel_t<Parcel, 4, decltype(root)>;
            auto [producer_root, consumer_perm] =
                perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(std::move(root));
            fixy::Box<Channel> channel = fixy::mint_box<Channel>(eff::testing::test().alloc, std::move(producer_root));
            auto producer = lend(*channel);
            { fixy::Box<Channel> const ending = std::move(channel); }
            static_cast<void>(producer.try_push(make_parcel(1)));
            ::foundation::linear::drop(std::move(consumer_perm));
        },
        ::fixy::TextView{"a drain of an MPSC channel while a producer handle lives."});
    ::fixy::check::expect_dies(
        test,
        []() noexcept {
            auto root = perm::mint_permission_root<c::mpsc_tag::Whole<WordTag>>();
            using Channel = c::mpsc_channel_t<std::uint64_t, 4, decltype(root)>;
            auto [producer_root, consumer_perm] =
                perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(std::move(root));
            Channel channel{std::move(producer_root)};
            auto consumer = channel.consumer(std::move(consumer_perm));
            channel.drain([](std::uint64_t) noexcept {});
            ::foundation::linear::drop(std::move(consumer));
        },
        ::fixy::TextView{"a drain of an MPSC channel while its consumer handle lives."});
}

constexpr std::uint64_t kProducers = 3;
constexpr std::uint64_t kParcelsOfProducer = 20000;

// The label of a parcel: the producer in the high bits, the place in the
// stream of the producer in the low bits.
[[nodiscard]] constexpr std::uint64_t label_at(std::uint64_t producer, std::uint64_t place) noexcept {
    return (producer << 32) | place;
}

// A producer thread of the bulk test.  It takes its handle and pushes each
// label of its stream in order.  A full ring gives the Box back, and the next
// push takes it.  The handle ends on the thread, after the last push, so the
// end of the last producer ends the stream.
template <class Producer>
struct ParcelFeeder {
    Producer handle;
    std::uint64_t producer = 0;

    ParcelFeeder(Producer&& lent, std::uint64_t index) noexcept : handle{std::move(lent)}, producer{index} {}

    void operator()() noexcept {
        Producer local = std::move(handle);
        for (std::uint64_t place = 0; place < kParcelsOfProducer; ++place) {
            fixy::Box<Parcel> parcel = make_parcel(label_at(producer, place));
            for (;;) {
                fixy::Option<fixy::Box<Parcel>> kept = local.try_push(std::move(parcel));
                if (kept.is_none()) break;
                parcel = std::move(kept).expect("a full channel gives the Box back");
                CRUCIBLE_SPIN_PAUSE;
            }
        }
    }
};

// The consumer thread of the bulk test.  It owns its handle, receives each
// Box until the end of the stream, and counts the labels that arrive out of
// the order of their producer.
template <class Consumer>
struct ParcelDrain {
    Consumer handle;
    fixy::FixedArray<std::uint64_t, kProducers> next_place{};
    std::uint64_t received = 0;
    std::uint64_t out_of_order = 0;

    explicit ParcelDrain(Consumer&& consumer) noexcept : handle{std::move(consumer)} {}

    void operator()() noexcept {
        for (;;) {
            fixy::Result<fixy::Box<Parcel>, c::NoItem> parcel = handle.try_receive(eff::testing::test().nondet);
            if (parcel.is_err()) {
                if (std::move(parcel).err() == fixy::some(c::NoItem::Closed)) return;
                CRUCIBLE_SPIN_PAUSE;
                continue;
            }
            std::uint64_t const label = label_of(std::move(parcel).ok());
            std::uint64_t const place = label & 0xFFFFFFFFu;
            bool is_known_producer = false;
            for (auto const& expected : next_place.view().get(label >> 32)) {
                if (*expected != place) ++out_of_order;
                *expected = place + 1;
                is_known_producer = true;
            }
            if (!is_known_producer) ++out_of_order;
            ++received;
        }
    }
};

// Three producer threads move many Boxes to one consumer thread through a
// small ring.  The consumer reads each object that its producer wrote before
// the push, so the labels of each producer arrive in its order, and the end
// of the last producer ends the stream.  The test ends the thread object of
// the consumer, and with it the consumer handle, after the join.
void three_threads_move_many_boxes(fixy::TestRunnerCtx const& test) noexcept {
    std::uint64_t received = 0;
    std::uint64_t out_of_order = 0;
    {
        auto root = perm::mint_permission_root<c::mpsc_tag::Whole<BulkTag>>();
        using BulkChannel = c::mpsc_box_channel_t<Parcel, 64, decltype(root)>;
        auto [producer_root, consumer_perm] =
            perm::mint_permission_split<BulkChannel::producer_tag, BulkChannel::consumer_tag>(std::move(root));
        BulkChannel channel{std::move(producer_root)};
        ParcelFeeder first{lend(channel), 0};
        ParcelFeeder second{lend(channel), 1};
        ParcelFeeder third{lend(channel), 2};
        ParcelDrain drain{channel.consumer(std::move(consumer_perm))};
        ::fixy::thread::run_joined(::fixy::BgDrainCtx{eff::testing::bg()}, ::fixy::thread::ThreadOptions{}, first,
                                   second, third, drain)
            .expect("the test starts the three producer threads and the consumer thread");
        received = drain.received;
        out_of_order = drain.out_of_order;
        ::foundation::linear::drop(std::move(drain));
    }
    CRUCIBLE_TEST_ASSERT(test, received == kProducers * kParcelsOfProducer);
    CRUCIBLE_TEST_ASSERT(test, out_of_order == 0);
    CRUCIBLE_TEST_ASSERT(test, live_parcels.load_acquire() == 0);
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{eff::testing::test()};
    push_pop_and_full_ring(test);
    receive_to_the_end_of_the_stream(test);
    end_of_channel_ends_each_object(test);
    drain_gives_each_item_in_order(test);
    drain_refuses_a_live_handle(test);
    three_threads_move_many_boxes(test);
    ::fixy::check::pass(test, "test_mpsc_box_channel: each Box moves one time and each object ends one time\n");
    return 0;
}
