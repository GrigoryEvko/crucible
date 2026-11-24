// The lanes form of fixy/concurrent/PermissionedChannel.h, checked on one
// thread: the producer door of each lane, the order of the merged stream
// through the handles, the end of each lane and of the stream, the drain that
// opens each lane again, the doors of the producer root, and the refusals of
// the doors.  test_channel_wait runs the form on threads.  Each case mints
// its root and names its channel in its own scope, so no brand leaves the
// scope that made it.  Each handle lives in the body of a frame door, so a
// case opens the frame of each producer in the frame of the one before it.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <foundation/ByteSeal.h>
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

// The tag of Nondet, which the lend of a producer and the withdraw of the
// producer root take.
constexpr ::foundation::effects::cap::Nondet kNondet = ::foundation::effects::testing::test().nondet;

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

// Ends the test when the producer door lent no producer.  No root is out of
// the pool in the cases that call it.
void expect_lent(::fixy::Option<::fixy::Unit> lent) noexcept {
    static_cast<void>(std::move(lent).expect("the pool lends a producer while its root is in the pool"));
}

// Ends a producer handle, which is not linear, as the end of its scope does.
// The parameter takes the handle and ends before the next statement.
template <class Producer>
void end_producer(Producer /*ended*/) noexcept {}

// Opens the frame of a producer of each lane, each in the frame of the one
// before it.  The innermost body gives each carried value and then the three
// handles to step through the once door.  A lend door ends a body that it did
// not call, and a closure marks no end of its own, so step holds no linear
// value.  A linear value that step needs goes in carried, by move, and each
// body holds it directly.
template <class Channel, class Step, class... Carried>
void open_each_lane(Channel& channel, Step step, Carried... carried) noexcept {
    expect_lent(c::with_producer(
        channel, kNondet, kFirst,
        [&channel, step = std::move(step), ... carried = std::move(carried)](auto first) mutable noexcept {
            expect_lent(c::with_producer(
                channel, kNondet, kSecond,
                [&channel, step = std::move(step), ... carried = std::move(carried),
                 first = std::move(first)](auto second) mutable noexcept {
                    expect_lent(c::with_producer(
                        channel, kNondet, kThird,
                        [step = std::move(step), ... carried = std::move(carried), first = std::move(first),
                         second = std::move(second)](auto third) mutable noexcept {
                            ::foundation::linear::call_once(std::move(step), std::move(carried)..., std::move(first),
                                                            std::move(second), std::move(third));
                        }));
                }));
        }));
}

// ── The refusals ────────────────────────────────────────────────────────

// The second lend of one lane opens in the frame of the first one.
void second_producer_of_a_lane() {
    auto root = perm::mint_permission_root<RootTag>();
    using Lanes = c::channel_t<int, 4, decltype(root), kLanes>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Lanes::producer_tag, Lanes::consumer_tag>(std::move(root));
    ::foundation::linear::drop(std::move(consumer_perm));
    Lanes channel{std::move(producer_root)};
    expect_lent(c::with_producer(channel, kNondet, kSecond, [&channel](auto /*held*/) noexcept {
        expect_lent(c::with_producer(channel, kNondet, kSecond, [](auto /*second*/) noexcept {}));
    }));
}

void producer_of_a_lane_that_ended() {
    auto root = perm::mint_permission_root<RootTag>();
    using Lanes = c::channel_t<int, 4, decltype(root), kLanes>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Lanes::producer_tag, Lanes::consumer_tag>(std::move(root));
    ::foundation::linear::drop(std::move(consumer_perm));
    Lanes channel{std::move(producer_root)};
    expect_lent(
        c::with_producer(channel, kNondet, kSecond, [](auto producer) noexcept { std::move(producer).close(); }));
    expect_lent(c::with_producer(channel, kNondet, kSecond, [](auto /*again*/) noexcept {}));
}

// A drain takes the lease of the producer root, which a live producer keeps in
// the pool, so only the claim at rest of an owner with no tag can meet a live
// producer, and it ends the process.
void root_at_rest_while_a_producer_lives() {
    auto root = perm::mint_permission_root<RootTag>();
    using Lanes = c::channel_t<int, 4, decltype(root), kLanes>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Lanes::producer_tag, Lanes::consumer_tag>(std::move(root));
    ::foundation::linear::drop(std::move(consumer_perm));
    Lanes channel{std::move(producer_root)};
    expect_lent(c::with_producer(channel, kNondet, kFirst, [&channel](auto /*held*/) noexcept {
        auto rested = channel.claim_producer_root_at_rest();
        channel.deposit_producer_root(std::move(rested));
    }));
}

// A refused door ends the process, so each attack runs in a child through
// expect_dies of the Check family.  The attacks run first, while the test
// process has one thread.
void refusals_end_the_process(::fixy::TestRunnerCtx const& test) noexcept {
    ::fixy::check::expect_dies(test, second_producer_of_a_lane,
                               ::fixy::TextView{"two live producer handles of one lane of a lanes channel"});
    ::fixy::check::expect_dies(test, producer_of_a_lane_that_ended,
                               ::fixy::TextView{"a producer handle of a lane that ended"});
    ::fixy::check::expect_dies(test, root_at_rest_while_a_producer_lives,
                               ::fixy::TextView{"a claim of the exclusive of a SharedPermissionPool at rest found a "
                                                "share or the lease out"});
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
    // The consumer goes to the step with the producers, so the step ends each
    // producer before it reads the end of the stream.
    c::with_consumer(channel, std::move(consumer_perm), [&channel, &test](auto opened_consumer) noexcept {
        open_each_lane(
            channel,
            [&channel, &test](auto consumer, auto first, auto second, auto third) noexcept {
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

                // The end of the third producer ends its lane, which leaves the
                // turn after its last item.
                std::move(third).close();
                expect_receives(consumer, ::fixy::FixedArray<int, 2>{22, kEmpty});
                CRUCIBLE_TEST_ASSERT(test, !channel.is_closed() && !consumer.is_closed());

                // The ends of the first and the second producer end their lanes.
                end_producer(std::move(first));
                end_producer(std::move(second));
                expect_receives(consumer, ::fixy::FixedArray<int, 4>{23, 24, 25, kClosed});
                CRUCIBLE_TEST_ASSERT(test, channel.is_closed() && consumer.is_closed());
                CRUCIBLE_TEST_ASSERT(test, channel.outstanding_producers() == 0);
                ::foundation::linear::drop(std::move(consumer));
            },
            std::move(opened_consumer));
    });
}

// The drain gives the items that the lanes hold in the order of the turn, to a
// drop that holds state, and opens each lane again for a new stream.
void drain_opens_each_lane(::fixy::TestRunnerCtx const& test) noexcept {
    auto root = perm::mint_permission_root<RootTag>();
    using Lanes = c::channel_t<int, 4, decltype(root), kLanes>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Lanes::producer_tag, Lanes::consumer_tag>(std::move(root));
    Lanes channel{std::move(producer_root)};
    open_each_lane(channel, [&test](auto first, auto second, auto /*third*/) noexcept {
        CRUCIBLE_TEST_ASSERT(test, first.try_push(11) && first.try_push(12) && second.try_push(21));
    });
    CRUCIBLE_TEST_ASSERT(test, channel.is_closed());
    ::fixy::FixedList<int, 4> drained{};
    // The lease of the producer root opens the claim of the drain, and it goes
    // back to the pool after the claim ends.
    auto lease = channel.try_withdraw_producer_root(kNondet).expect("the root comes out with no producer out");
    {
        Lanes::DrainClaim claim = channel.claim_drain(lease);
        channel.drain(claim, [&drained](int item) noexcept {
            static_cast<void>(drained.append(item).expect("the drain gives at most four items"));
        });
    }
    channel.deposit_producer_root(std::move(lease));
    CRUCIBLE_TEST_ASSERT(test, drained.size() == 3);
    CRUCIBLE_TEST_ASSERT(test, *drained.get(0).expect("a first item") == 11);
    CRUCIBLE_TEST_ASSERT(test, *drained.get(1).expect("a second item") == 21);
    CRUCIBLE_TEST_ASSERT(test, *drained.get(2).expect("a third item") == 12);
    CRUCIBLE_TEST_ASSERT(test, !channel.is_closed());

    // Each lane opens again: a new stream on the same channel.
    c::with_consumer(channel, std::move(consumer_perm), [&channel, &test](auto consumer) noexcept {
        open_each_lane(channel, [&test](auto first, auto second, auto /*third*/) noexcept {
            CRUCIBLE_TEST_ASSERT(test, second.try_push(22) && first.try_push(13));
        });
        expect_receives(consumer, ::fixy::FixedArray<int, 3>{13, 22, kClosed});
        ::foundation::linear::drop(std::move(consumer));
    });
}

// While the producer root is out of the pool, the door lends no producer, and
// the lease of the root with the consumer token gives the ring to a recombined
// access.  The deposit of the lease opens the door again.
void root_doors(::fixy::TestRunnerCtx const& test) noexcept {
    auto root = perm::mint_permission_root<RootTag>();
    using Lanes = c::channel_t<int, 4, decltype(root), kLanes>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Lanes::producer_tag, Lanes::consumer_tag>(std::move(root));
    Lanes channel{std::move(producer_root)};
    auto withdrawn = channel.try_withdraw_producer_root(kNondet).expect("the root comes out with no producer out");
    bool const is_lend_refused = c::with_producer(channel, kNondet, kFirst, [](auto /*lent*/) noexcept {}).is_none();
    CRUCIBLE_TEST_ASSERT(test, channel.is_exclusive_active() && is_lend_refused);
    auto back = channel.with_recombined_access(
        Lanes::RecombinedParts{.root = std::move(withdrawn), .consumer = std::move(consumer_perm)},
        [](Lanes::RecombinedRing& ring) noexcept { static_cast<void>(ring.try_push(kSecond, 7)); });
    channel.deposit_producer_root(std::move(back.root));
    CRUCIBLE_TEST_ASSERT(test, !channel.is_exclusive_active());

    // The item of the recombined access waits in the second lane for the turn.
    c::with_consumer(channel, std::move(back.consumer), [&channel, &test](auto consumer) noexcept {
        open_each_lane(channel, [&test](auto first, auto /*second*/, auto /*third*/) noexcept {
            CRUCIBLE_TEST_ASSERT(test, first.try_push(1));
        });
        expect_receives(consumer, ::fixy::FixedArray<int, 3>{1, 7, kClosed});
        ::foundation::linear::drop(std::move(consumer));
    });
}

// A linear value that a body holds by value.
class[[= ::foundation::linear::linear{}]] LinearToken {
public:
    constexpr explicit LinearToken(std::uint32_t value) noexcept : value_{value} {}
    LinearToken(LinearToken const&) = delete("a linear token has one owner");
    LinearToken& operator=(LinearToken const&) = delete("a linear token has one owner");
    constexpr LinearToken(LinearToken&&) noexcept = default;
    constexpr LinearToken& operator=(LinearToken&&) noexcept = default;
    constexpr ~LinearToken() = default;

    [[nodiscard]] constexpr std::uint32_t consume() && noexcept { return value_; }

private:
    std::uint32_t value_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};

// While the producer root is out of the pool, the door lends no producer,
// and a body that holds a linear value comes back in the error.  After the
// deposit of the root, the next lend calls the body that came back.
void refused_lend_gives_back_a_linear_body(::fixy::TestRunnerCtx const& test) noexcept {
    auto root = perm::mint_permission_root<RootTag>();
    using Lanes = c::channel_t<int, 4, decltype(root), kLanes>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Lanes::producer_tag, Lanes::consumer_tag>(std::move(root));
    Lanes channel{std::move(producer_root)};
    auto withdrawn = channel.try_withdraw_producer_root(kNondet).expect("the root comes out with no producer out");
    auto body = [held = LinearToken{5}](auto /*lent*/) mutable noexcept { return std::move(held).consume(); };
    using Body = decltype(body);
    using Refused = c::RefusedLend<Body>;
    Body unrun = c::with_producer(channel, kNondet, kFirst, std::move(body))
                     .match([](std::uint32_t) noexcept -> Body { ::fixy::fatal("the door lends no producer now"); },
                            [](Refused refusal) noexcept -> Body { return std::move(refusal.body); });
    channel.deposit_producer_root(std::move(withdrawn));
    std::uint32_t const value = c::with_producer(channel, kNondet, kFirst, std::move(unrun))
                                    .match([](std::uint32_t given) noexcept { return given; },
                                           [](Refused) noexcept -> std::uint32_t {
                                               ::fixy::fatal("the door lends a producer after the deposit");
                                           });
    CRUCIBLE_TEST_ASSERT(test, value == 5);
    c::with_consumer(channel, std::move(consumer_perm),
                     [](auto consumer) noexcept { ::foundation::linear::drop(std::move(consumer)); });
}

// The count of the ends of live counted tokens.
constinit ::fixy::Atomic<int> counted_token_ends{0};

// A linear value whose end the test observes: the end of a live token counts
// one, and a moved-from token is not live.
class[[= ::foundation::linear::linear{}]] CountedToken {
public:
    CountedToken() noexcept = default;
    CountedToken(CountedToken const&) = delete("a linear token has one owner");
    CountedToken& operator=(CountedToken const&) = delete("a linear token has one owner");
    CountedToken(CountedToken&& other) noexcept : is_live_{other.is_live_} { other.is_live_ = false; }
    CountedToken& operator=(CountedToken&&) = delete("the test counts each end");
    ~CountedToken() {
        if (is_live_) static_cast<void>(counted_token_ends.fetch_add_acq_rel(1));
    }

private:
    bool is_live_ = true;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};

// A refused lend of a borrowing body ends the body in the door, and each
// linear value of the body ends through its own drop door: the end of the
// token runs one time, and the body does not run.
void refused_lend_ends_a_borrowing_body(::fixy::TestRunnerCtx const& test) noexcept {
    auto root = perm::mint_permission_root<RootTag>();
    using Lanes = c::channel_t<int, 4, decltype(root), kLanes>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Lanes::producer_tag, Lanes::consumer_tag>(std::move(root));
    Lanes channel{std::move(producer_root)};
    auto withdrawn = channel.try_withdraw_producer_root(kNondet).expect("the root comes out with no producer out");
    counted_token_ends.store_release(0);
    int calls = 0;
    bool const is_lent =
        c::with_producer(channel, kNondet, kFirst, [&calls, token = CountedToken{}](auto /*lent*/) mutable noexcept {
            ++calls;
            ::foundation::linear::drop(std::move(token));
        }).is_some();
    CRUCIBLE_TEST_ASSERT(test, !is_lent && calls == 0 && counted_token_ends.load_acquire() == 1);
    channel.deposit_producer_root(std::move(withdrawn));
    c::with_consumer(channel, std::move(consumer_perm),
                     [](auto consumer) noexcept { ::foundation::linear::drop(std::move(consumer)); });
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    refusals_end_the_process(test);
    order_and_end_through_the_handles(test);
    drain_opens_each_lane(test);
    root_doors(test);
    refused_lend_gives_back_a_linear_body(test);
    refused_lend_ends_a_borrowing_body(test);
    ::fixy::check::pass(test, "test_lanes_channel: PASSED\n");
    return 0;
}
