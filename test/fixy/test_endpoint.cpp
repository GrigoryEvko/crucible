// What fixy/channels/SubstrateSessionBridge.h, Endpoint.h,
// StageEndpointBridge.h and EndpointMint.h claim, checked at run time.
//
// Each check moves real values through a real channel.  A channel protocol
// loops with no exit, so each session ends with a typed detach, which
// releases the channel handle that the session owns.
//
// A frame door of the channel gives each handle to a body, and a session, an
// endpoint or a stage takes the handle by move.  So each of them opens and
// ends inside the body of the frame of its handle.

#include <fixy/channels/EndpointMint.h>
#include <fixy/channels/Endpoint.h>
#include <fixy/channels/StageEndpointBridge.h>
#include <fixy/channels/SubstrateSessionBridge.h>
#include <fixy/concurrent/PermissionedChannel.h>

#include <foundation/Linear.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Seat.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <cstdio>
#include <type_traits>
#include <utility>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace c = fixy::concurrent;
namespace channels = fixy::channels;
namespace s = fixy::session;
namespace perm = foundation::permissions;

namespace {

using FgCtx = ::foundation::effects::ExecCtx<::foundation::effects::ctx_cap::Fg, ::foundation::effects::Row<>>;
// A context that holds Nondet and no other atom, for a snapshot.
using SnapshotCtx = ::foundation::effects::ExecCtx<::foundation::effects::Test,
                                                   ::foundation::effects::Row<::foundation::effects::Effect::Nondet>>;

// The tag of a snapshot of a channel.  The test context holds Nondet.
constexpr ::foundation::effects::cap::Nondet kSnapshot = ::foundation::effects::testing::test().nondet;

[[nodiscard]] int fail(const char* what) noexcept {
    std::fprintf(stderr, "test_endpoint: %s\n", what);
    return 1;
}

// A session over a channel handle steps through the write and the read of
// the pole table of the bridge, so each step takes a context and no
// transport.  A full channel or an empty one makes the session wait and try
// again.

// ── The bridge ───────────────────────────────────────────────────────

// Each channel of this file has one root site, and its type names the
// brand of that site.
struct BridgeTag {};
auto bridge_root() noexcept {
    return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, BridgeTag>>();
}
using BridgeSpsc = c::channel_t<int, 8, decltype(bridge_root())>;

// The table bridges the two directions of a channel.  A value that names
// no direction has no handle of its pole, so the channel does not bridge it.
static_assert(!channels::IsBridgeableDirection<BridgeSpsc, static_cast<channels::Direction>(2)>);

[[nodiscard]] int bridge_round_trip() {
    const FgCtx ctx = ::foundation::effects::testing::foreground();
    BridgeSpsc channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<BridgeSpsc::producer_tag, BridgeSpsc::consumer_tag>(bridge_root());

    // A session over a handle refuses move-assignment, so each loop keeps its
    // current session in a Seat of foundation/core/Seat.h.
    c::with_producer(channel, std::move(producer_perm), [&ctx](auto producer) noexcept {
        auto head = ::foundation::core::mint_seat(
            channels::mint_substrate_session<BridgeSpsc, channels::Direction::Producer>(ctx, std::move(producer)));
        for (int value = 1; value <= 3; ++value) {
            head.step([&ctx, value](auto&& held) noexcept { return std::move(held).send(ctx, value * 10); });
        }
        std::move(head).take().detach(s::detach_reason::InfiniteLoopProtocol{});
    });
    // A receive on an empty pole waits, so the consumer session reads only
    // when the producer session left the three values.
    if (channel.size_approx(kSnapshot) != 3) {
        ::foundation::linear::drop(std::move(consumer_perm));
        return fail("the producer session left the wrong number of values");
    }

    const bool is_in_order = c::with_consumer(channel, std::move(consumer_perm), [&ctx](auto consumer) noexcept {
        auto head = ::foundation::core::mint_seat(
            channels::mint_substrate_session<BridgeSpsc, channels::Direction::Consumer>(ctx, std::move(consumer)));
        bool is_each_read = true;
        for (int value = 1; value <= 3; ++value) {
            head.step([&ctx, &is_each_read, value](auto&& held) noexcept {
                auto [got, next] = std::move(held).recv(ctx);
                is_each_read = is_each_read && got == value * 10;
                return std::move(next);
            });
        }
        std::move(head).take().detach(s::detach_reason::InfiniteLoopProtocol{});
        return is_each_read;
    });
    if (!is_in_order) return fail("the consumer session read the wrong value");
    if (!channel.empty_approx(kSnapshot)) return fail("the consumer session left values in the channel");
    return 0;
}

// The read of the pole table of a merge pole takes the bare tag of Nondet
// from the context of the step, so the consumer session receives in a
// context that holds Nondet.  The root of the channel stands in the test, so
// its brand stays in the test.
struct MergeTag {};

[[nodiscard]] int merge_pole_receives_in_a_nondet_context() {
    const SnapshotCtx ctx{::foundation::effects::testing::test()};
    auto root = perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::merge, MergeTag>>();
    using BridgeMerge = c::channel_t<int, 8, decltype(root)>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<BridgeMerge::producer_tag, BridgeMerge::consumer_tag>(std::move(root));
    BridgeMerge channel{std::move(producer_root)};

    const bool is_in_order =
        c::with_consumer(channel, std::move(consumer_perm), [&ctx, &channel](auto consumer) noexcept {
            auto head =
                channels::mint_substrate_session<BridgeMerge, channels::Direction::Consumer>(ctx, std::move(consumer));

            // A receive on an empty pole waits, so the test fills the pole
            // first, and it ends the process when the pole refuses a value.
            auto const filled = c::with_producer(
                channel, kSnapshot, [](auto feeder) noexcept { return feeder.try_push(5) && feeder.try_push(6); });
            if (filled.is_none()) ::fixy::fatal("test_endpoint: the merge channel lent no producer share");
            if (filled != ::fixy::some(true))
                ::fixy::fatal("test_endpoint: the merge channel refused a value below capacity");

            // Each receive goes through the handle of the pole, and the detach
            // door of the handle ends the stream and the pole with it.
            auto [first, after_first] = std::move(head).recv(ctx);
            auto [second, next] = std::move(after_first).recv(ctx);
            std::move(next).detach(s::detach_reason::InfiniteLoopProtocol{});
            return first == 5 && second == 6;
        });
    const bool is_drained = channel.empty_approx(kSnapshot);
    return is_in_order && is_drained ? 0
                                     : fail(is_in_order ? "the merge pole session left values in the channel"
                                                        : "the merge pole session read the wrong values");
}

// ── The endpoint ─────────────────────────────────────────────────────

struct EndpointTag {};
auto endpoint_root() noexcept {
    return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, EndpointTag>>();
}
using EndpointSpsc = c::channel_t<int, 8, decltype(endpoint_root())>;

// A scope brand that the checks name.  No frame door gives a handle on it.
struct CheckScope {};

// A snapshot of an endpoint takes the tag of Nondet from its context, so an
// endpoint of the foreground context reads no snapshot.  Each endpoint holds
// a frame handle on the scope brand of the checks.
template <class EndpointType>
concept ReadsSnapshot = requires(EndpointType const& endpoint) {
    endpoint.size_approx();
    endpoint.empty_approx();
};
static_assert(!ReadsSnapshot<channels::Endpoint<EndpointSpsc, channels::Direction::Producer, FgCtx,
                                                EndpointSpsc::producer_handle<CheckScope>>>);
static_assert(!ReadsSnapshot<channels::Endpoint<EndpointSpsc, channels::Direction::Consumer, FgCtx,
                                                EndpointSpsc::consumer_handle<CheckScope>>>);
static_assert(ReadsSnapshot<channels::Endpoint<EndpointSpsc, channels::Direction::Producer, SnapshotCtx,
                                               EndpointSpsc::producer_handle<CheckScope>>>);
static_assert(ReadsSnapshot<channels::Endpoint<EndpointSpsc, channels::Direction::Consumer, SnapshotCtx,
                                               EndpointSpsc::consumer_handle<CheckScope>>>);

[[nodiscard]] int endpoint_send_recv_and_hand_back() {
    const FgCtx ctx = ::foundation::effects::testing::foreground();
    EndpointSpsc channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<EndpointSpsc::producer_tag, EndpointSpsc::consumer_tag>(endpoint_root());

    const int failures = c::with_ends(
        channel, std::move(producer_perm), std::move(consumer_perm), [&ctx](auto sender, auto receiver) noexcept {
            // The producer endpoint reads a snapshot, so its context holds Nondet.
            auto producer = channels::mint_endpoint<EndpointSpsc, channels::Direction::Producer>(
                SnapshotCtx{::foundation::effects::testing::test()}, std::move(sender));
            auto consumer =
                channels::mint_endpoint<EndpointSpsc, channels::Direction::Consumer>(ctx, std::move(receiver));

            const bool is_first_sent = producer.try_send(7);
            const bool is_one_held = producer.size_approx() == 1;
            // An empty read gives the value -1, which no send gave.
            const bool is_first_read = consumer.try_recv().value_or(-1) == 7;

            // A moved endpoint takes the handle along, and the new endpoint
            // still reaches the channel.
            auto moved = std::move(producer);
            const bool is_moved_sent = moved.try_send(8);

            // into_handle gives the only handle that holds the permission.
            auto handle = std::move(moved).into_handle();
            const bool is_pushed = handle.try_push(9);
            ::foundation::linear::drop(std::move(handle).release());

            // into_session starts the default protocol over the same handle.
            // A receive on an empty pole waits, so the session reads only when
            // the two values above went in.
            auto session = std::move(consumer).into_session();
            bool is_session_read = false;
            if (is_moved_sent && is_pushed) {
                auto [first, after_first] = std::move(session).recv(ctx);
                auto [second, after_second] = std::move(after_first).recv(ctx);
                std::move(after_second).detach(s::detach_reason::InfiniteLoopProtocol{});
                is_session_read = first == 8 && second == 9;
            } else {
                std::move(session).detach(s::detach_reason::InfiniteLoopProtocol{});
            }

            if (!is_first_sent) return fail("the producer endpoint was refused below capacity");
            if (!is_one_held) return fail("the producer endpoint saw the wrong size");
            if (!is_first_read) return fail("the consumer endpoint read the wrong value");
            if (!is_moved_sent) return fail("the moved endpoint lost its channel");
            if (!is_pushed) return fail("the handle from into_handle lost its channel");
            if (!is_session_read) return fail("the endpoint session read the wrong values");
            return 0;
        });
    if (failures != 0) return failures;
    if (!channel.empty_approx(kSnapshot)) return fail("the endpoint session left values in the channel");
    return 0;
}

// ── The stage bridge ─────────────────────────────────────────────────

// A stage takes the handles of two frames, and each handle carries the scope
// brand of its frame.  So each body of a stage is a template over the types
// of its handles, and the body of the inner frame names it.  The inner body
// takes the handle of the outer frame by move.

struct StageInTag {};
struct StageOutTag {};
auto stage_in_root() noexcept {
    return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, StageInTag>>();
}
auto stage_out_root() noexcept {
    return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, StageOutTag>>();
}
using StageIn = c::channel_t<int, 8, decltype(stage_in_root())>;
using StageOut = c::channel_t<int, 8, decltype(stage_out_root())>;

// The body doubles each value it drains.
template <class In, class Out>
void doubling_stage(In&& in, Out&& out) noexcept {
    while (true) {
        auto value = in.try_pop();
        if (value.is_none()) break;
        (void)out.try_push(std::move(value).expect("the test above found an item") * 2);
    }
}

[[nodiscard]] int stage_from_endpoints_runs_its_body() {
    const FgCtx ctx = ::foundation::effects::testing::foreground();
    StageIn in_channel{};
    StageOut out_channel{};
    auto [in_producer_perm, in_consumer_perm] =
        perm::mint_permission_split<StageIn::producer_tag, StageIn::consumer_tag>(stage_in_root());
    auto [out_producer_perm, out_consumer_perm] =
        perm::mint_permission_split<StageOut::producer_tag, StageOut::consumer_tag>(stage_out_root());

    auto feeder_token = c::with_producer(in_channel, std::move(in_producer_perm), [](auto feeder) noexcept {
        (void)feeder.try_push(1);
        (void)feeder.try_push(2);
        return std::move(feeder).release();
    });
    ::foundation::linear::drop(std::move(feeder_token));

    c::with_consumer(
        in_channel, std::move(in_consumer_perm),
        [&ctx, &out_channel, out_producer_perm = std::move(out_producer_perm)](auto in) mutable noexcept {
            c::with_producer(
                out_channel, std::move(out_producer_perm), [&ctx, in = std::move(in)](auto out) mutable noexcept {
                    auto in_ep = channels::mint_endpoint<StageIn, channels::Direction::Consumer>(ctx, std::move(in));
                    auto out_ep = channels::mint_endpoint<StageOut, channels::Direction::Producer>(ctx, std::move(out));
                    auto stage = channels::mint_stage_from_endpoints<&doubling_stage<decltype(in), decltype(out)>>(
                        ctx, std::move(in_ep), std::move(out_ep));
                    std::move(stage).run();
                });
        });

    return c::with_consumer(out_channel, std::move(out_consumer_perm), [](auto drain) noexcept {
        const bool is_doubled = drain.try_pop().value_or(-1) == 2 && drain.try_pop().value_or(-1) == 4;
        ::foundation::linear::drop(std::move(drain).release());
        return is_doubled ? 0 : fail("the stage did not run its body");
    });
}

// A stage that feeds an MPSC channel.  The MPSC producer handle has the
// producer pole, so the body is a stage and the endpoint mint takes it.
struct FeedInTag {};
struct FeedOutTag {};
auto feed_in_root() noexcept {
    return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, FeedInTag>>();
}
auto feed_out_root() noexcept {
    return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::merge, FeedOutTag>>();
}
using FeedIn = c::channel_t<int, 8, decltype(feed_in_root())>;
using FeedOut = c::channel_t<int, 8, decltype(feed_out_root())>;

template <class In, class Out>
void forwarding_stage(In&& in, Out&& out) noexcept {
    while (true) {
        auto value = in.try_pop();
        if (value.is_none()) break;
        (void)out.try_push(std::move(value).expect("the test above found an item") + 100);
    }
}

[[nodiscard]] int stage_feeds_an_mpsc_channel() {
    const FgCtx ctx = ::foundation::effects::testing::foreground();
    auto [in_producer_perm, in_consumer_perm] =
        perm::mint_permission_split<FeedIn::producer_tag, FeedIn::consumer_tag>(feed_in_root());
    auto [out_producer_root, out_consumer_perm] =
        perm::mint_permission_split<FeedOut::producer_tag, FeedOut::consumer_tag>(feed_out_root());
    FeedIn in_channel{};
    FeedOut out_channel{std::move(out_producer_root)};

    auto feeder_token = c::with_producer(in_channel, std::move(in_producer_perm), [](auto feeder) noexcept {
        (void)feeder.try_push(1);
        (void)feeder.try_push(2);
        return std::move(feeder).release();
    });
    ::foundation::linear::drop(std::move(feeder_token));

    // The merge door gives no value when the MPSC channel lends no producer
    // share.
    const bool is_lent =
        c::with_consumer(in_channel, std::move(in_consumer_perm), [&ctx, &out_channel](auto in) noexcept {
            return c::with_producer(
                       out_channel, kSnapshot,
                       [&ctx, in = std::move(in)](auto out) mutable noexcept {
                           auto stage =
                               channels::mint_stage_from_endpoints<&forwarding_stage<decltype(in), decltype(out)>>(
                                   ctx,
                                   channels::mint_endpoint<FeedIn, channels::Direction::Consumer>(ctx, std::move(in)),
                                   channels::mint_endpoint<FeedOut, channels::Direction::Producer>(ctx,
                                                                                                   std::move(out)));
                           std::move(stage).run();
                       })
                .is_some();
        });
    if (!is_lent) {
        ::foundation::linear::drop(std::move(out_consumer_perm));
        return fail("the MPSC channel lent no producer share");
    }

    return c::with_consumer(out_channel, std::move(out_consumer_perm), [](auto drain) noexcept {
        // A pop of an MPSC channel is a merge, so it takes the tag of Nondet.
        const ::foundation::effects::cap::Nondet merge = ::foundation::effects::testing::test().nondet;
        const bool is_forwarded = drain.try_pop(merge).value_or(-1) == 101 && drain.try_pop(merge).value_or(-1) == 102;
        ::foundation::linear::drop(std::move(drain).release());
        return is_forwarded ? 0 : fail("the stage did not feed the MPSC channel");
    });
}

struct FanLeftTag {};
struct FanRightTag {};
struct FanOutTag {};
auto fan_left_root() noexcept {
    return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, FanLeftTag>>();
}
auto fan_right_root() noexcept {
    return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, FanRightTag>>();
}
auto fan_out_root() noexcept {
    return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, FanOutTag>>();
}
using FanLeft = c::channel_t<int, 8, decltype(fan_left_root())>;
using FanRight = c::channel_t<int, 8, decltype(fan_right_root())>;
using FanOut = c::channel_t<int, 8, decltype(fan_out_root())>;

// The body sums one value from each input.
template <class Left, class Right, class Out>
void summing_stage(Left&& left, Right&& right, Out&& out) noexcept {
    auto from_left = left.try_pop();
    auto from_right = right.try_pop();
    if (from_left.is_some() && from_right.is_some()) {
        (void)out.try_push(std::move(from_left).expect("the test above found an item")
                           + std::move(from_right).expect("the test above found an item"));
    }
}

[[nodiscard]] int mpmc_stage_from_endpoints_runs_its_body() {
    const FgCtx ctx = ::foundation::effects::testing::foreground();
    FanLeft left{};
    FanRight right{};
    FanOut out{};
    auto [left_producer_perm, left_consumer_perm] =
        perm::mint_permission_split<FanLeft::producer_tag, FanLeft::consumer_tag>(fan_left_root());
    auto [right_producer_perm, right_consumer_perm] =
        perm::mint_permission_split<FanRight::producer_tag, FanRight::consumer_tag>(fan_right_root());
    auto [out_producer_perm, out_consumer_perm] =
        perm::mint_permission_split<FanOut::producer_tag, FanOut::consumer_tag>(fan_out_root());

    auto left_token = c::with_producer(left, std::move(left_producer_perm), [](auto left_feeder) noexcept {
        (void)left_feeder.try_push(3);
        return std::move(left_feeder).release();
    });
    auto right_token = c::with_producer(right, std::move(right_producer_perm), [](auto right_feeder) noexcept {
        (void)right_feeder.try_push(4);
        return std::move(right_feeder).release();
    });
    ::foundation::linear::drop(std::move(left_token));
    ::foundation::linear::drop(std::move(right_token));

    c::with_consumer(left, std::move(left_consumer_perm),
                     [&ctx, &right, &out, right_consumer_perm = std::move(right_consumer_perm),
                      out_producer_perm = std::move(out_producer_perm)](auto left_in) mutable noexcept {
                         c::with_consumer(
                             right, std::move(right_consumer_perm),
                             [&ctx, &out, out_producer_perm = std::move(out_producer_perm),
                              left_in = std::move(left_in)](auto right_in) mutable noexcept {
                                 c::with_producer(
                                     out, std::move(out_producer_perm),
                                     [&ctx, left_in = std::move(left_in),
                                      right_in = std::move(right_in)](auto sum_out) mutable noexcept {
                                         auto stage = channels::mint_mpmc_stage_from_endpoints<
                                             &summing_stage<decltype(left_in), decltype(right_in), decltype(sum_out)>>(
                                             ctx,
                                             channels::mint_endpoint<FanLeft, channels::Direction::Consumer>(
                                                 ctx, std::move(left_in)),
                                             channels::mint_endpoint<FanRight, channels::Direction::Consumer>(
                                                 ctx, std::move(right_in)),
                                             channels::mint_endpoint<FanOut, channels::Direction::Producer>(
                                                 ctx, std::move(sum_out)));
                                         std::move(stage).run();
                                     });
                             });
                     });

    return c::with_consumer(out, std::move(out_consumer_perm), [](auto drain) noexcept {
        const bool is_summed = drain.try_pop().value_or(-1) == 7;
        ::foundation::linear::drop(std::move(drain).release());
        return is_summed ? 0 : fail("the fan-in stage did not run its body");
    });
}

// A writer with the publish shape of a single-writer cell.  It keeps the
// last value it was given in an atomic cell of the test, so the stage can run
// on a thread of its own (foundation/Transfer.h).
struct LastValueWriter {
    ::foundation::core::Atomic<int>& last;
    void publish(int const& value) noexcept { last.store_release(value); }
};

struct SwmrInTag {};
auto swmr_in_root() noexcept {
    return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, SwmrInTag>>();
}
using SwmrIn = c::channel_t<int, 8, decltype(swmr_in_root())>;

template <class In>
void publishing_stage(In&& in, LastValueWriter&& writer) noexcept {
    while (true) {
        auto value = in.try_pop();
        if (value.is_none()) break;
        writer.publish(std::move(value).expect("the test above found an item"));
    }
}

[[nodiscard]] int swmr_stage_runs_its_body() {
    const FgCtx ctx = ::foundation::effects::testing::foreground();
    SwmrIn channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<SwmrIn::producer_tag, SwmrIn::consumer_tag>(swmr_in_root());
    auto feeder_token = c::with_producer(channel, std::move(producer_perm), [](auto feeder) noexcept {
        (void)feeder.try_push(5);
        (void)feeder.try_push(6);
        return std::move(feeder).release();
    });
    ::foundation::linear::drop(std::move(feeder_token));

    ::foundation::core::Atomic<int> last{0};
    c::with_consumer(channel, std::move(consumer_perm), [&ctx, &last](auto in) noexcept {
        auto stage = channels::mint_swmr_stage_from_endpoint<&publishing_stage<decltype(in)>>(
            ctx, channels::mint_endpoint<SwmrIn, channels::Direction::Consumer>(ctx, std::move(in)),
            LastValueWriter{.last = last});
        std::move(stage).run();
    });
    if (last.load_acquire() != 6) return fail("the single-writer stage did not publish the last value");
    return 0;
}

// ── The recording wrapper ────────────────────────────────────────────

struct RecordTag {};
auto record_root() noexcept {
    return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, RecordTag>>();
}
using RecordSpsc = c::channel_t<int, 8, decltype(record_root())>;

[[nodiscard]] int recording_endpoint_records_each_step() {
    const FgCtx ctx = ::foundation::effects::testing::foreground();
    RecordSpsc channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<RecordSpsc::producer_tag, RecordSpsc::consumer_tag>(record_root());

    s::SessionEventLog log;
    const int received = c::with_ends(
        channel, std::move(producer_perm), std::move(consumer_perm),
        [&ctx, &log](auto producer, auto consumer) noexcept {
            auto recorded = channels::mint_recording_endpoint(
                channels::mint_endpoint<RecordSpsc, channels::Direction::Producer>(ctx, std::move(producer)),
                s::mint_session_event_writer(::foundation::effects::testing::test().alloc, log), s::RoleTagId{1},
                s::RoleTagId{2});
            auto after_send = std::move(recorded).send(ctx, 11);
            std::move(after_send).detach(s::detach_reason::InfiniteLoopProtocol{});

            const int value = consumer.try_pop().value_or(-1);
            ::foundation::linear::drop(std::move(consumer).release());
            return value;
        });

    if (received != 11) return fail("the recorded endpoint did not send its value");
    if (log.size() == 0 || log[0].op() != s::SessionOp::Send) return fail("the recorded endpoint wrote no send event");
    return 0;
}

// A recorded consumer endpoint receives through the read of the pole table,
// in the context of the step, and records the receive of an int.  The root
// of the channel stands in the test, so its brand stays in the test.
struct RecordInTag {};

[[nodiscard]] int recording_endpoint_records_each_receive() {
    const FgCtx ctx = ::foundation::effects::testing::foreground();
    auto root = perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, RecordInTag>>();
    using RecordInSpsc = c::channel_t<int, 8, decltype(root)>;
    RecordInSpsc channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<RecordInSpsc::producer_tag, RecordInSpsc::consumer_tag>(std::move(root));
    s::SessionEventLog log;
    const int value = c::with_ends(
        channel, std::move(producer_perm), std::move(consumer_perm),
        [&ctx, &log](auto producer, auto consumer) noexcept {
            auto recorded = channels::mint_recording_endpoint(
                channels::mint_endpoint<RecordInSpsc, channels::Direction::Consumer>(ctx, std::move(consumer)),
                s::mint_session_event_writer(::foundation::effects::testing::test().alloc, log), s::RoleTagId{2},
                s::RoleTagId{1});

            // A receive on an empty pole waits, so the test fills the pole
            // first, and it ends the process when the pole refuses the value.
            // The close ends the producer handle with its token.
            const bool is_filled = producer.try_push(12);
            std::move(producer).close();
            if (!is_filled) ::fixy::fatal("test_endpoint: the channel refused a value below capacity");

            auto [received, next] = std::move(recorded).recv(ctx);
            std::move(next).detach(s::detach_reason::InfiniteLoopProtocol{});
            return received;
        });

    if (value != 12) return fail("the recorded endpoint read the wrong value");
    if (log.size() == 0 || log[0].op() != s::SessionOp::Recv || log[0].payload_schema() != s::default_schema_hash<int>)
        return fail("the recorded endpoint wrote no receive event of an int");
    return 0;
}

}  // namespace

int main() {
    int failures = 0;
    failures += bridge_round_trip();
    failures += merge_pole_receives_in_a_nondet_context();
    failures += endpoint_send_recv_and_hand_back();
    failures += stage_from_endpoints_runs_its_body();
    failures += stage_feeds_an_mpsc_channel();
    failures += mpmc_stage_from_endpoints_runs_its_body();
    failures += swmr_stage_runs_its_body();
    failures += recording_endpoint_records_each_step();
    failures += recording_endpoint_records_each_receive();
    if (failures != 0) {
        std::fprintf(stderr, "test_endpoint: %d check(s) failed\n", failures);
        return 1;
    }
    ::fixy::check::pass(kTest, "test_endpoint: all checks passed\n");
    return 0;
}
