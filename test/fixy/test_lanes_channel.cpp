// The lanes form of fixy/concurrent/PermissionedChannel.h, checked on one
// thread: the producer door of each lane, the order of the merged stream
// through the handles, the end of each lane and of the stream, the drain that
// opens each lane again, the doors of the producer root, and the refusals of
// the doors.  test_channel_wait runs the form on threads.  Each case mints
// its root and names its channel in its own scope, so no brand leaves the
// scope that made it.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <foundation/Linear.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace {

namespace c = ::fixy::concurrent;
namespace perm = ::foundation::permissions;

struct LanesTag {};
using RootTag = c::channel_tag::Whole<c::ChannelForm::lanes, LanesTag>;

// Each channel of the test has three lanes of four cells.
constexpr c::LaneCount kLanes{3};
constexpr c::LaneIndex<kLanes> kFirst{::fixy::index<0>()};
constexpr c::LaneIndex<kLanes> kSecond{::fixy::index<1>()};
constexpr c::LaneIndex<kLanes> kThird{::fixy::index<2>()};

// The codes of a receive that gives no item.  No push gives either code.
constexpr int kEmpty = -1;
constexpr int kClosed = -2;

// The item of a receive, or the code of its end.
template <class Consumer>
[[nodiscard]] int receive_code(Consumer& consumer) noexcept {
    return consumer.try_receive().match(
        [](int item) noexcept { return item; },
        [](c::NoItem why) noexcept { return why == c::NoItem::Closed ? kClosed : kEmpty; });
}

// Takes one receive for each code of expected, in order, and ends the test at
// the first receive that gives another code.
template <class Consumer, std::size_t N>
void expect_receives(Consumer& consumer, ::fixy::FixedArray<int, N> const& expected) noexcept {
    for (int const code : expected) {
        int const got = receive_code(consumer);
        if (got != code) ::fixy::fatal("a receive gave {} where the order of the lanes owes {}", got, code);
    }
}

// A producer handle of a lane.  No root is out of the pool in these cases, so
// an empty lend ends the test.
template <class Channel>
[[nodiscard]] typename Channel::ProducerHandle lend(Channel& channel, c::LaneIndex<kLanes> lane) noexcept {
    return channel.producer(lane).expect("the pool lends a producer while its root is in the pool");
}

// ── The refusals ────────────────────────────────────────────────────────

void second_producer_of_a_lane() {
    auto root = perm::mint_permission_root<RootTag>();
    using Lanes = c::channel_t<int, 4, decltype(root), kLanes>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Lanes::producer_tag, Lanes::consumer_tag>(std::move(root));
    ::foundation::linear::drop(std::move(consumer_perm));
    Lanes channel{std::move(producer_root)};
    [[maybe_unused]] auto held = lend(channel, kSecond);
    [[maybe_unused]] auto second = lend(channel, kSecond);
}

void producer_of_a_lane_that_ended() {
    auto root = perm::mint_permission_root<RootTag>();
    using Lanes = c::channel_t<int, 4, decltype(root), kLanes>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Lanes::producer_tag, Lanes::consumer_tag>(std::move(root));
    ::foundation::linear::drop(std::move(consumer_perm));
    Lanes channel{std::move(producer_root)};
    std::move(lend(channel, kSecond)).close();
    [[maybe_unused]] auto again = lend(channel, kSecond);
}

void drain_while_a_producer_lives() {
    auto root = perm::mint_permission_root<RootTag>();
    using Lanes = c::channel_t<int, 4, decltype(root), kLanes>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Lanes::producer_tag, Lanes::consumer_tag>(std::move(root));
    ::foundation::linear::drop(std::move(consumer_perm));
    Lanes channel{std::move(producer_root)};
    [[maybe_unused]] auto held = lend(channel, kFirst);
    channel.drain([](int) noexcept {});
}

// A refused door ends the process, so each attack runs in a child through
// expect_dies of the Check family.  The attacks run first, while the test
// process has one thread.
void refusals_end_the_process(::fixy::TestRunnerCtx const& test) noexcept {
    ::fixy::check::expect_dies(test, second_producer_of_a_lane,
                               ::fixy::TextView{"two live producer handles of one lane of a lanes channel"});
    ::fixy::check::expect_dies(test, producer_of_a_lane_that_ended,
                               ::fixy::TextView{"a producer handle of a lane that ended"});
    ::fixy::check::expect_dies(test, drain_while_a_producer_lives,
                               ::fixy::TextView{"a drain of a lanes channel while a producer handle lives."});
}

// ── The stream ──────────────────────────────────────────────────────────

// The handles give the order of the lanes: one item of each lane in turn, a
// wait on the open lane of the turn, the end of each lane with its producer
// handle, and the end of the stream after the last lane.
void order_and_end_through_the_handles(::fixy::TestRunnerCtx const& test) noexcept {
    auto root = perm::mint_permission_root<RootTag>();
    using Lanes = c::channel_t<int, 4, decltype(root), kLanes>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Lanes::producer_tag, Lanes::consumer_tag>(std::move(root));
    Lanes channel{std::move(producer_root)};
    Lanes::ConsumerHandle consumer = channel.consumer(std::move(consumer_perm));
    {
        Lanes::ProducerHandle first = lend(channel, kFirst);
        Lanes::ProducerHandle second = lend(channel, kSecond);
        Lanes::ProducerHandle third = lend(channel, kThird);
        CRUCIBLE_TEST_ASSERT(test, first.lane() == kFirst && third.lane() == kThird);
        CRUCIBLE_TEST_ASSERT(test, channel.outstanding_producers() == 3);
        CRUCIBLE_TEST_ASSERT(test, third.try_push(31) && second.try_push(21));
        expect_receives(consumer, ::fixy::FixedArray<int, 1>{kEmpty});
        CRUCIBLE_TEST_ASSERT(test, first.try_push(11) && first.try_push(12));
        expect_receives(consumer, ::fixy::FixedArray<int, 5>{11, 21, 31, 12, kEmpty});

        // A full lane refuses a push.
        for (int item = 22; item < 26; ++item) {
            CRUCIBLE_TEST_ASSERT(test, second.try_push(item));
        }
        CRUCIBLE_TEST_ASSERT(test, !second.try_push(26));

        // The end of the third producer ends its lane, which leaves the turn
        // after its last item.
        std::move(third).close();
        expect_receives(consumer, ::fixy::FixedArray<int, 2>{22, kEmpty});
        CRUCIBLE_TEST_ASSERT(test, !channel.is_closed() && !consumer.is_closed());
    }
    // The ends of the first and the second producer end their lanes.
    expect_receives(consumer, ::fixy::FixedArray<int, 4>{23, 24, 25, kClosed});
    CRUCIBLE_TEST_ASSERT(test, channel.is_closed() && consumer.is_closed());
    CRUCIBLE_TEST_ASSERT(test, channel.outstanding_producers() == 0);
    ::foundation::linear::drop(std::move(consumer));
}

// The drain gives the items that the lanes hold in the order of the turn, to a
// drop that holds state, and opens each lane again for a new stream.
void drain_opens_each_lane(::fixy::TestRunnerCtx const& test) noexcept {
    auto root = perm::mint_permission_root<RootTag>();
    using Lanes = c::channel_t<int, 4, decltype(root), kLanes>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Lanes::producer_tag, Lanes::consumer_tag>(std::move(root));
    Lanes channel{std::move(producer_root)};
    {
        Lanes::ProducerHandle first = lend(channel, kFirst);
        Lanes::ProducerHandle second = lend(channel, kSecond);
        Lanes::ProducerHandle third = lend(channel, kThird);
        CRUCIBLE_TEST_ASSERT(test, first.try_push(11) && first.try_push(12) && second.try_push(21));
    }
    CRUCIBLE_TEST_ASSERT(test, channel.is_closed());
    ::fixy::FixedList<int, 4> drained{};
    channel.drain([&drained](int item) noexcept {
        static_cast<void>(drained.append(item).expect("the drain gives at most four items"));
    });
    CRUCIBLE_TEST_ASSERT(test, drained.size() == 3);
    CRUCIBLE_TEST_ASSERT(test, *drained.get(0).expect("a first item") == 11);
    CRUCIBLE_TEST_ASSERT(test, *drained.get(1).expect("a second item") == 21);
    CRUCIBLE_TEST_ASSERT(test, *drained.get(2).expect("a third item") == 12);
    CRUCIBLE_TEST_ASSERT(test, !channel.is_closed());

    // Each lane opens again: a new stream on the same channel.
    Lanes::ConsumerHandle consumer = channel.consumer(std::move(consumer_perm));
    {
        Lanes::ProducerHandle first = lend(channel, kFirst);
        Lanes::ProducerHandle second = lend(channel, kSecond);
        Lanes::ProducerHandle third = lend(channel, kThird);
        CRUCIBLE_TEST_ASSERT(test, second.try_push(22) && first.try_push(13));
    }
    expect_receives(consumer, ::fixy::FixedArray<int, 3>{13, 22, kClosed});
    ::foundation::linear::drop(std::move(consumer));
}

// While the producer root is out of the pool, the door lends no producer, and
// the root with the consumer token gives the ring to a recombined access.  The
// deposit of the root opens the door again.
void root_doors(::fixy::TestRunnerCtx const& test) noexcept {
    auto root = perm::mint_permission_root<RootTag>();
    using Lanes = c::channel_t<int, 4, decltype(root), kLanes>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Lanes::producer_tag, Lanes::consumer_tag>(std::move(root));
    Lanes channel{std::move(producer_root)};
    auto withdrawn = channel.try_withdraw_producer_root().expect("the root comes out with no producer out");
    CRUCIBLE_TEST_ASSERT(test, channel.is_exclusive_active() && channel.producer(kFirst).is_none());
    auto whole = perm::mint_permission_combine<Lanes::whole_tag>(std::move(withdrawn), std::move(consumer_perm));
    auto back = channel.with_recombined_access(
        std::move(whole), [](Lanes::ring_type& ring) noexcept { static_cast<void>(ring.try_push(kSecond, 7)); });
    auto [returned_root, returned_consumer] =
        perm::mint_permission_split<Lanes::producer_tag, Lanes::consumer_tag>(std::move(back));
    channel.deposit_producer_root(std::move(returned_root));
    CRUCIBLE_TEST_ASSERT(test, !channel.is_exclusive_active());

    // The item of the recombined access waits in the second lane for the turn.
    Lanes::ConsumerHandle consumer = channel.consumer(std::move(returned_consumer));
    {
        Lanes::ProducerHandle first = lend(channel, kFirst);
        Lanes::ProducerHandle second = lend(channel, kSecond);
        Lanes::ProducerHandle third = lend(channel, kThird);
        CRUCIBLE_TEST_ASSERT(test, first.try_push(1));
    }
    expect_receives(consumer, ::fixy::FixedArray<int, 3>{1, 7, kClosed});
    ::foundation::linear::drop(std::move(consumer));
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    refusals_end_the_process(test);
    order_and_end_through_the_handles(test);
    drain_opens_each_lane(test);
    root_doors(test);
    ::fixy::check::pass(test, "test_lanes_channel: PASSED\n");
    return 0;
}
