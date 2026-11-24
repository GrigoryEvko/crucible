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
#include <fixy/concurrent/PermissionedBoxChannel.h>
#include <fixy/concurrent/PermissionedChannel.h>
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

// The tag of a lend and of a pop of a merge.
constexpr eff::cap::Nondet kMerge = eff::testing::test().nondet;

// The result of the body of a producer frame.  A test lends while no
// exclusive is out, so an empty lend ends the process.
template <class Value>
[[nodiscard]] Value expect_lent(fixy::Option<Value>&& lent) noexcept {
    return std::move(lent).expect("the channel lends a producer while no exclusive is out");
}

// Two producers push into one ring, and the pop gives a Box of each object.
// A full ring gives the Box of the push back, with its object.  The consumer
// frame is the outer frame, and the two producer frames take the consumer
// handle by move.
void push_pop_and_full_ring(fixy::TestRunnerCtx const& test) noexcept {
    {
        auto root = perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::merge, ParcelTag>>();
        using Channel = c::box_channel_t<Parcel, 2, decltype(root)>;
        // The two ends have the shapes that a stage reads, with a Box as the
        // payload, and they name one channel.  The handles of the frames read
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
        // A pop of an MPSC channel is a merge, so it takes the tag of Nondet.
        static_assert(c::is_merge_consumer_handle_v<Channel::ConsumerEnd>);
        auto [producer_root, consumer_perm] =
            perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(std::move(root));
        Channel channel{std::move(producer_root)};
        c::with_consumer(channel, std::move(consumer_perm), [&test, &channel](auto consumer) noexcept {
            static_cast<void>(expect_lent(c::with_producer(
                channel, kMerge, [&test, &channel, consumer = std::move(consumer)](auto first) mutable noexcept {
                    static_cast<void>(expect_lent(c::with_producer(
                        channel, kMerge,
                        [&test, &channel, &first, consumer = std::move(consumer)](auto second) mutable noexcept {
                            using Producer = decltype(second);
                            using Consumer = decltype(consumer);
                            static_assert(c::is_producer_handle_v<Producer>);
                            static_assert(c::is_consumer_handle_v<Consumer>);
                            static_assert(std::is_same_v<c::producer_handle_value_t<Producer>, fixy::Box<Parcel>>);
                            static_assert(std::is_same_v<c::consumer_handle_value_t<Consumer>, fixy::Box<Parcel>>);
                            static_assert(c::HandlesShareChannel<Producer, Consumer>);
                            static_assert(Producer::channel_capacity == 2 && Consumer::channel_capacity == 2);
                            static_assert(!std::is_copy_constructible_v<Producer>);
                            static_assert(!std::is_move_assignable_v<Consumer>);
                            static_assert(c::is_merge_consumer_handle_v<Consumer>);
                            CRUCIBLE_TEST_ASSERT(test, channel.outstanding_producers() == 2);
                            CRUCIBLE_TEST_ASSERT(test, consumer.try_pop(kMerge).is_none());
                            CRUCIBLE_TEST_ASSERT(test, first.try_push(make_parcel(1)).is_none());
                            CRUCIBLE_TEST_ASSERT(test, second.try_push(make_parcel(2)).is_none());
                            fixy::Option<fixy::Box<Parcel>> const refused = first.try_push(make_parcel(3));
                            CRUCIBLE_TEST_ASSERT(test, label_of(refused) == 3);
                            CRUCIBLE_TEST_ASSERT(test, live_parcels.load_acquire() == 3);

                            fixy::Option<fixy::Box<Parcel>> const one = consumer.try_pop(kMerge);
                            fixy::Option<fixy::Box<Parcel>> const two = consumer.try_pop(kMerge);
                            CRUCIBLE_TEST_ASSERT(test, label_of(one) == 1);
                            CRUCIBLE_TEST_ASSERT(test, label_of(two) == 2);
                            CRUCIBLE_TEST_ASSERT(test, consumer.try_pop(kMerge).is_none());
                            ::foundation::linear::drop(std::move(consumer));
                        })));
                })));
        });
    }
    CRUCIBLE_TEST_ASSERT(test, live_parcels.load_acquire() == 0);
}

// The end of the last producer handle closes the channel, and the end of a
// different producer does not.  try_receive gives each Box that the ring still
// holds, and then Closed.
void receive_to_the_end_of_the_stream(fixy::TestRunnerCtx const& test) noexcept {
    {
        auto root = perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::merge, ParcelTag>>();
        using Channel = c::box_channel_t<Parcel, 4, decltype(root)>;
        auto [producer_root, consumer_perm] =
            perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(std::move(root));
        Channel channel{std::move(producer_root)};
        c::with_consumer(channel, std::move(consumer_perm), [&test, &channel](auto consumer) noexcept {
            CRUCIBLE_TEST_ASSERT(test, consumer.try_receive(kMerge).err() == fixy::some(c::NoItem::Empty));
            static_cast<void>(expect_lent(c::with_producer(channel, kMerge, [&test, &channel](auto second) noexcept {
                static_cast<void>(expect_lent(c::with_producer(channel, kMerge, [&test](auto first) noexcept {
                    CRUCIBLE_TEST_ASSERT(test, first.try_push(make_parcel(5)).is_none());
                })));
                CRUCIBLE_TEST_ASSERT(test, !channel.is_closed());
                CRUCIBLE_TEST_ASSERT(test, second.try_push(make_parcel(6)).is_none());
            })));
            CRUCIBLE_TEST_ASSERT(test, channel.is_closed());
            fixy::Option<fixy::Box<Parcel>> const fifth = consumer.try_receive(kMerge).ok();
            fixy::Option<fixy::Box<Parcel>> const sixth = consumer.try_receive(kMerge).ok();
            CRUCIBLE_TEST_ASSERT(test, label_of(fifth) == 5);
            CRUCIBLE_TEST_ASSERT(test, label_of(sixth) == 6);
            CRUCIBLE_TEST_ASSERT(test, consumer.try_receive(kMerge).err() == fixy::some(c::NoItem::Closed));
            ::foundation::linear::drop(std::move(consumer));
        });
    }
    CRUCIBLE_TEST_ASSERT(test, live_parcels.load_acquire() == 0);
}

// The end of the channel ends each object that the ring still holds.  The
// handles end first, so no handle lives at the drain.
void end_of_channel_ends_each_object(fixy::TestRunnerCtx const& test) noexcept {
    {
        auto root = perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::merge, ParcelTag>>();
        using Channel = c::box_channel_t<Parcel, 4, decltype(root)>;
        auto [producer_root, consumer_perm] =
            perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(std::move(root));
        Channel channel{std::move(producer_root)};
        c::with_consumer(channel, std::move(consumer_perm), [&test, &channel](auto consumer) noexcept {
            static_cast<void>(expect_lent(c::with_producer(channel, kMerge, [&test, &channel](auto first) noexcept {
                static_cast<void>(expect_lent(c::with_producer(channel, kMerge, [&test, &first](auto second) noexcept {
                    CRUCIBLE_TEST_ASSERT(test, first.try_push(make_parcel(7)).is_none());
                    CRUCIBLE_TEST_ASSERT(test, second.try_push(make_parcel(8)).is_none());
                })));
            })));
            ::foundation::linear::drop(std::move(consumer));
        });
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
        for (auto const slot : fixy::Index<kLogCapacity>::check(end_count)) {
            end_log.set(slot, label_);
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
        auto root = perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::merge, WordTag>>();
        using Channel = c::box_channel_t<Entry, 4, decltype(root)>;
        auto [producer_root, consumer_perm] =
            perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(std::move(root));
        Channel channel{std::move(producer_root)};
        c::with_consumer(channel, std::move(consumer_perm), [&test, &channel](auto consumer) noexcept {
            static_cast<void>(expect_lent(c::with_producer(channel, kMerge, [&test, &channel](auto first) noexcept {
                static_cast<void>(expect_lent(c::with_producer(channel, kMerge, [&test, &first](auto second) noexcept {
                    CRUCIBLE_TEST_ASSERT(test, first.try_push(make_entry(11)).is_none());
                    CRUCIBLE_TEST_ASSERT(test, second.try_push(make_entry(12)).is_none());
                    CRUCIBLE_TEST_ASSERT(test, first.try_push(make_entry(13)).is_none());
                })));
            })));
            ::foundation::linear::drop(std::move(consumer));
        });
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
// while a producer handle lives, and the claim of the producer root at rest of
// the end refuses.  The second child drains a plain MPSC channel while its
// consumer handle lives.
void drain_refuses_a_live_handle(fixy::TestRunnerCtx const& test) noexcept {
    ::fixy::check::expect_dies(
        test,
        []() noexcept {
            auto root = perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::merge, WordTag>>();
            using Channel = c::box_channel_t<Parcel, 4, decltype(root)>;
            auto [producer_root, consumer_perm] =
                perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(std::move(root));
            fixy::Box<Channel> channel = fixy::mint_box<Channel>(eff::testing::test().alloc, std::move(producer_root));
            Channel& held = *channel;
            // The body ends the Box of the channel while its producer handle
            // lives: the brand of the frame does not cover an owner of the
            // channel that the body ends.
            static_cast<void>(expect_lent(c::with_producer(held, kMerge, [&channel](auto producer) noexcept {
                { fixy::Box<Channel> const ending = std::move(channel); }
                static_cast<void>(producer.try_push(make_parcel(1)));
            })));
            ::foundation::linear::drop(std::move(consumer_perm));
        },
        ::fixy::TextView{"a claim of the exclusive of a SharedPermissionPool at rest found a share or the lease out"});
    ::fixy::check::expect_dies(
        test,
        []() noexcept {
            auto root = perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::merge, WordTag>>();
            using Channel = c::channel_t<std::uint64_t, 4, decltype(root)>;
            auto [producer_root, consumer_perm] =
                perm::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(std::move(root));
            Channel channel{std::move(producer_root)};
            c::with_consumer(channel, std::move(consumer_perm), [&channel](auto consumer) noexcept {
                auto lease = channel.try_withdraw_producer_root(eff::testing::test().nondet)
                                 .expect("the root comes out with no producer out");
                {
                    Channel::DrainClaim claim = channel.claim_drain(lease);
                    channel.drain(claim, [](std::uint64_t) noexcept {});
                }
                channel.deposit_producer_root(std::move(lease));
                ::foundation::linear::drop(std::move(consumer));
            });
        },
        ::fixy::TextView{"a drain of a merge channel while its consumer handle lives."});
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

// The count of the bulk drain: the labels that it received, and the labels
// that arrived out of the order of their producer.
struct ParcelCount {
    std::uint64_t received = 0;
    std::uint64_t out_of_order = 0;
};

// The consumer thread of the bulk test.  It owns its handle, receives each
// Box until the end of the stream, and counts the labels that arrive out of
// the order of their producer.  It gives back its count, which the caller
// reads after the join.  The thread calls the drain one time as an rvalue, and
// the drain ends its handle at the drop door.
template <class Consumer>
struct ParcelDrain {
    Consumer handle;
    fixy::FixedArray<std::uint64_t, kProducers> next_place{};

    explicit ParcelDrain(Consumer&& consumer) noexcept : handle{std::move(consumer)} {}

    [[nodiscard]] ParcelCount operator()() && noexcept {
        ParcelCount count{};
        for (;;) {
            fixy::Result<fixy::Box<Parcel>, c::NoItem> parcel = handle.try_receive(eff::testing::test().nondet);
            if (parcel.is_err()) {
                if (std::move(parcel).err() == fixy::some(c::NoItem::Closed)) {
                    ::foundation::linear::drop(std::move(handle));
                    return count;
                }
                CRUCIBLE_SPIN_PAUSE;
                continue;
            }
            std::uint64_t const label = label_of(std::move(parcel).ok());
            std::uint64_t const place = label & 0xFFFFFFFFu;
            bool is_known_producer = false;
            for (auto const producer : fixy::Index<kProducers>::check(label >> 32)) {
                if (next_place.at(producer) != place) ++count.out_of_order;
                next_place.set(producer, place + 1);
                is_known_producer = true;
            }
            if (!is_known_producer) ++count.out_of_order;
            ++count.received;
        }
    }
};

// The four threads of the bulk test, over the three producer handles and the
// consumer handle of the frames.  It gives the count of the consumer.
template <class First, class Second, class Third, class Consumer>
[[nodiscard]] ParcelCount move_many_boxes(First&& first, Second&& second, Third&& third, Consumer&& consumer) noexcept {
    ParcelFeeder first_feed{std::move(first), 0};
    ParcelFeeder second_feed{std::move(second), 1};
    ParcelFeeder third_feed{std::move(third), 2};
    ParcelDrain drain{std::move(consumer)};
    auto const joined = ::fixy::thread::run_joined(::fixy::BgDrainCtx{eff::testing::bg()},
                                                   ::fixy::thread::ThreadOptions{}, std::move(first_feed),
                                                   std::move(second_feed), std::move(third_feed), std::move(drain))
                            .expect("the test starts the three producer threads and the consumer thread");
    return joined.at(::fixy::index<3>());
}

// Three producer threads move many Boxes to one consumer thread through a
// small ring.  The consumer reads each object that its producer wrote before
// the push, so the labels of each producer arrive in its order, and the end
// of the last producer ends the stream.  The thread door owns the thread
// object of the consumer, and it ends it, and with it the consumer handle,
// after the join.
void three_threads_move_many_boxes(fixy::TestRunnerCtx const& test) noexcept {
    ParcelCount count{};
    {
        auto root = perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::merge, BulkTag>>();
        using BulkChannel = c::box_channel_t<Parcel, 64, decltype(root)>;
        auto [producer_root, consumer_perm] =
            perm::mint_permission_split<BulkChannel::producer_tag, BulkChannel::consumer_tag>(std::move(root));
        BulkChannel channel{std::move(producer_root)};
        // The consumer frame is the outer frame, and the three producer frames
        // nest in it.  Each inner body takes the handles of the outer frames
        // by move.
        count = c::with_consumer(channel, std::move(consumer_perm), [&channel](auto consumer) noexcept {
            return expect_lent(c::with_producer(
                channel, kMerge, [&channel, consumer = std::move(consumer)](auto first) mutable noexcept {
                    return expect_lent(
                        c::with_producer(channel, kMerge,
                                         [&channel, consumer = std::move(consumer),
                                          first = std::move(first)](auto second) mutable noexcept {
                                             return expect_lent(c::with_producer(
                                                 channel, kMerge,
                                                 [consumer = std::move(consumer), first = std::move(first),
                                                  second = std::move(second)](auto third) mutable noexcept {
                                                     return move_many_boxes(std::move(first), std::move(second),
                                                                            std::move(third), std::move(consumer));
                                                 }));
                                         }));
                }));
        });
    }
    CRUCIBLE_TEST_ASSERT(test, count.received == kProducers * kParcelsOfProducer);
    CRUCIBLE_TEST_ASSERT(test, count.out_of_order == 0);
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
