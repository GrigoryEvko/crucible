// The SPSC channel of Box values: the push of a Box and the pop of the same
// object, the give-back of a full ring, the end of the stream, the end of
// each object that the ring holds at the end of the channel, and two threads
// that move many Boxes.  The tsan preset runs the threaded case.
//
// Each test mints the root of its channel where it names the type of the
// channel, so the brand of the root stays in that scope, and each test ends
// each handle at the drop door of foundation/Linear.h.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/concurrent/ChannelEnd.h>
#include <fixy/concurrent/HandleTraits.h>
#include <fixy/concurrent/PermissionedBoxChannel.h>
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

// A push gives the object to the ring, and the pop gives a Box of the same
// object.  A full ring gives the Box of the push back, with its object.
void push_pop_and_full_ring(fixy::TestRunnerCtx const& test) noexcept {
    {
        auto root = perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, ParcelTag>>();
        using Channel = c::box_channel_t<Parcel, 2, decltype(root)>;
        // The two ends have the shapes that a stage reads, with a Box as the
        // payload, and they name one channel.  The handles of the frame read
        // the same in the body below.
        static_assert(c::is_producer_handle_v<Channel::ProducerEnd>);
        static_assert(c::is_consumer_handle_v<Channel::ConsumerEnd>);
        static_assert(std::is_same_v<c::producer_handle_value_t<Channel::ProducerEnd>, fixy::Box<Parcel>>);
        static_assert(std::is_same_v<c::consumer_handle_value_t<Channel::ConsumerEnd>, fixy::Box<Parcel>>);
        static_assert(c::HandlesShareChannel<Channel::ProducerEnd, Channel::ConsumerEnd>);
        // Each end states the capacity of the channel.
        static_assert(Channel::ProducerEnd::channel_capacity == 2 && Channel::ConsumerEnd::channel_capacity == 2);
        static_assert(!std::is_copy_constructible_v<Channel::ProducerEnd>);
        static_assert(!std::is_move_assignable_v<Channel::ConsumerEnd>);
        static_assert(!std::is_move_constructible_v<Channel>);
        Channel channel{};
        auto [producer_perm, consumer_perm] =
            perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(std::move(root));
        c::with_ends(channel, std::move(producer_perm), std::move(consumer_perm),
                     [&test](auto producer, auto consumer) noexcept {
                         using Producer = decltype(producer);
                         using Consumer = decltype(consumer);
                         static_assert(c::is_producer_handle_v<Producer>);
                         static_assert(c::is_consumer_handle_v<Consumer>);
                         static_assert(std::is_same_v<c::producer_handle_value_t<Producer>, fixy::Box<Parcel>>);
                         static_assert(std::is_same_v<c::consumer_handle_value_t<Consumer>, fixy::Box<Parcel>>);
                         static_assert(c::HandlesShareChannel<Producer, Consumer>);
                         static_assert(Producer::channel_capacity == 2 && Consumer::channel_capacity == 2);
                         static_assert(!std::is_copy_constructible_v<Producer>);
                         static_assert(!std::is_move_assignable_v<Consumer>);

                         CRUCIBLE_TEST_ASSERT(test, consumer.try_pop().is_none());
                         CRUCIBLE_TEST_ASSERT(test, producer.try_push(make_parcel(1)).is_none());
                         CRUCIBLE_TEST_ASSERT(test, producer.try_push(make_parcel(2)).is_none());
                         fixy::Option<fixy::Box<Parcel>> const refused = producer.try_push(make_parcel(3));
                         CRUCIBLE_TEST_ASSERT(test, label_of(refused) == 3);
                         CRUCIBLE_TEST_ASSERT(test, live_parcels.load_acquire() == 3);

                         fixy::Option<fixy::Box<Parcel>> const first = consumer.try_pop();
                         fixy::Option<fixy::Box<Parcel>> const second = consumer.try_pop();
                         CRUCIBLE_TEST_ASSERT(test, label_of(first) == 1);
                         CRUCIBLE_TEST_ASSERT(test, label_of(second) == 2);
                         CRUCIBLE_TEST_ASSERT(test, consumer.try_pop().is_none());
                         ::foundation::linear::drop(std::move(producer));
                         ::foundation::linear::drop(std::move(consumer));
                     });
    }
    CRUCIBLE_TEST_ASSERT(test, live_parcels.load_acquire() == 0);
}

// The close door of the producer handle closes the channel before the end of
// the scope of the handle.  try_receive gives each Box that the ring still
// holds, and then Closed.
void receive_to_the_end_of_the_stream(fixy::TestRunnerCtx const& test) noexcept {
    {
        auto root = perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, ParcelTag>>();
        using Channel = c::box_channel_t<Parcel, 2, decltype(root)>;
        Channel channel{};
        auto [producer_perm, consumer_perm] =
            perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(std::move(root));
        c::with_ends(channel, std::move(producer_perm), std::move(consumer_perm),
                     [&test](auto producer, auto consumer) noexcept {
                         CRUCIBLE_TEST_ASSERT(test, consumer.try_receive().err() == fixy::some(c::NoItem::Empty));
                         CRUCIBLE_TEST_ASSERT(test, producer.try_push(make_parcel(5)).is_none());
                         std::move(producer).close();

                         CRUCIBLE_TEST_ASSERT(test, consumer.is_closed());
                         fixy::Option<fixy::Box<Parcel>> const last = consumer.try_receive().ok();
                         CRUCIBLE_TEST_ASSERT(test, label_of(last) == 5);
                         CRUCIBLE_TEST_ASSERT(test, consumer.try_receive().err() == fixy::some(c::NoItem::Closed));
                         ::foundation::linear::drop(std::move(consumer));
                     });
    }
    CRUCIBLE_TEST_ASSERT(test, live_parcels.load_acquire() == 0);
}

// The end of the channel ends each object that the ring still holds.  The
// handles end first, so no handle lives at the drain.
void end_of_channel_ends_each_object(fixy::TestRunnerCtx const& test) noexcept {
    {
        auto root = perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, ParcelTag>>();
        using Channel = c::box_channel_t<Parcel, 2, decltype(root)>;
        Channel channel{};
        auto [producer_perm, consumer_perm] =
            perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(std::move(root));
        c::with_ends(channel, std::move(producer_perm), std::move(consumer_perm),
                     [&test](auto producer, auto consumer) noexcept {
                         CRUCIBLE_TEST_ASSERT(test, producer.try_push(make_parcel(7)).is_none());
                         CRUCIBLE_TEST_ASSERT(test, producer.try_push(make_parcel(8)).is_none());
                         ::foundation::linear::drop(std::move(producer));
                         ::foundation::linear::drop(std::move(consumer));
                     });
        CRUCIBLE_TEST_ASSERT(test, live_parcels.load_acquire() == 2);
    }
    CRUCIBLE_TEST_ASSERT(test, live_parcels.load_acquire() == 0);
}

constexpr std::uint64_t kBulkParcels = 100000;

// The producer thread of the bulk test.  It owns its handle and pushes each
// label in order.  A full ring gives the Box back, and the next push takes it.
template <class Producer>
struct ParcelFeeder {
    Producer handle;

    explicit ParcelFeeder(Producer&& producer) noexcept : handle{std::move(producer)} {}

    void operator()() noexcept {
        for (std::uint64_t label = 0; label < kBulkParcels; ++label) {
            fixy::Box<Parcel> parcel = make_parcel(label);
            for (;;) {
                fixy::Option<fixy::Box<Parcel>> kept = handle.try_push(std::move(parcel));
                if (kept.is_none()) break;
                parcel = std::move(kept).expect("a full channel gives the Box back");
                CRUCIBLE_SPIN_PAUSE;
            }
        }
    }
};

// The count of the bulk drain: the labels that it received, and the labels
// that arrived out of order.
struct ParcelCount {
    std::uint64_t received = 0;
    std::uint64_t out_of_order = 0;
};

// The consumer thread of the bulk test.  It owns its handle, pops each Box,
// and counts the labels that arrive out of order.  It gives back its count,
// which the caller reads after the join.
template <class Consumer>
struct ParcelDrain {
    Consumer handle;

    explicit ParcelDrain(Consumer&& consumer) noexcept : handle{std::move(consumer)} {}

    [[nodiscard]] ParcelCount operator()() noexcept {
        ParcelCount count{};
        while (count.received < kBulkParcels) {
            fixy::Option<fixy::Box<Parcel>> const parcel = handle.try_pop();
            if (parcel.is_none()) {
                CRUCIBLE_SPIN_PAUSE;
                continue;
            }
            if (label_of(parcel) != count.received) ++count.out_of_order;
            ++count.received;
        }
        return count;
    }
};

// A producer thread moves many Boxes to a consumer thread through a small
// ring.  The consumer reads each object that the producer wrote before its
// push, so each label arrives in order.  The thread door owns the two thread
// objects, and it ends them, and with them the two handles, at the drop door
// after the join.
void two_threads_move_many_boxes(fixy::TestRunnerCtx const& test) noexcept {
    ParcelCount count{};
    {
        auto root = perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, BulkTag>>();
        using BulkChannel = c::box_channel_t<Parcel, 64, decltype(root)>;
        BulkChannel channel{};
        auto [producer_perm, consumer_perm] =
            perm::mint_permission_split<BulkChannel::producer_tag, BulkChannel::consumer_tag>(std::move(root));
        count = c::with_ends(channel, std::move(producer_perm), std::move(consumer_perm),
                             [](auto producer, auto consumer) noexcept {
                                 ParcelFeeder feed{std::move(producer)};
                                 ParcelDrain drain{std::move(consumer)};
                                 auto const joined = ::fixy::thread::run_joined(::fixy::BgDrainCtx{eff::testing::bg()},
                                                                                ::fixy::thread::ThreadOptions{},
                                                                                std::move(feed), std::move(drain))
                                                         .expect("the test starts the producer thread and the "
                                                                 "consumer thread");
                                 return joined.at(::fixy::index<1>());
                             });
    }
    CRUCIBLE_TEST_ASSERT(test, count.received == kBulkParcels);
    CRUCIBLE_TEST_ASSERT(test, count.out_of_order == 0);
    CRUCIBLE_TEST_ASSERT(test, live_parcels.load_acquire() == 0);
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{eff::testing::test()};
    push_pop_and_full_ring(test);
    receive_to_the_end_of_the_stream(test);
    end_of_channel_ends_each_object(test);
    two_threads_move_many_boxes(test);
    ::fixy::check::pass(test, "test_spsc_box_channel: each Box moves one time and each object ends one time\n");
    return 0;
}
