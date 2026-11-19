// What fixy/channels/SubstrateSessionBridge.h, Endpoint.h,
// StageEndpointBridge.h and EndpointMint.h claim, checked at run time.
//
// Each check moves real values through a real channel.  A channel protocol
// loops with no exit, so each session ends with a typed detach, which
// releases the channel handle that the session owns.

#include <fixy/channels/EndpointMint.h>
#include <fixy/channels/Endpoint.h>
#include <fixy/channels/StageEndpointBridge.h>
#include <fixy/channels/SubstrateSessionBridge.h>

#include <foundation/core/Atomic.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <cstdio>
#include <optional>
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

// The transports of the channel protocols try once.  A full channel or an
// empty one makes the session wait and try again.
constexpr auto push_int = [](auto& handle, int& value) noexcept -> bool { return handle.try_push(value); };
constexpr auto pop_int = [](auto& handle) noexcept -> ::fixy::Option<int> { return handle.try_pop(); };

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

    std::optional producer{channels::mint_substrate_session<BridgeSpsc, channels::Direction::Producer>(
        ctx, channel.producer(std::move(producer_perm)))};
    for (int value = 1; value <= 3; ++value) {
        producer.emplace(std::move(*producer).send(value * 10, push_int));
    }
    std::move(*producer).detach(s::detach_reason::InfiniteLoopProtocol{});
    if (channel.size_approx(kSnapshot) != 3) return fail("the producer session left the wrong number of values");

    std::optional consumer{channels::mint_substrate_session<BridgeSpsc, channels::Direction::Consumer>(
        ctx, channel.consumer(std::move(consumer_perm)))};
    for (int value = 1; value <= 3; ++value) {
        auto [got, next] = std::move(*consumer).recv(pop_int);
        if (got != value * 10) return fail("the consumer session read the wrong value");
        consumer.emplace(std::move(next));
    }
    std::move(*consumer).detach(s::detach_reason::InfiniteLoopProtocol{});
    if (!channel.empty_approx(kSnapshot)) return fail("the consumer session left values in the channel");
    return 0;
}

// ── The endpoint ─────────────────────────────────────────────────────

struct EndpointTag {};
auto endpoint_root() noexcept {
    return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, EndpointTag>>();
}
using EndpointSpsc = c::channel_t<int, 8, decltype(endpoint_root())>;

// A snapshot of an endpoint takes the tag of Nondet from its context, so an
// endpoint of the foreground context reads no snapshot.
template <class EndpointType>
concept ReadsSnapshot = requires(EndpointType const& endpoint) {
    endpoint.size_approx();
    endpoint.empty_approx();
};
static_assert(!ReadsSnapshot<channels::Endpoint<EndpointSpsc, channels::Direction::Producer, FgCtx>>);
static_assert(!ReadsSnapshot<channels::Endpoint<EndpointSpsc, channels::Direction::Consumer, FgCtx>>);
static_assert(ReadsSnapshot<channels::Endpoint<EndpointSpsc, channels::Direction::Producer, SnapshotCtx>>);
static_assert(ReadsSnapshot<channels::Endpoint<EndpointSpsc, channels::Direction::Consumer, SnapshotCtx>>);

[[nodiscard]] int endpoint_send_recv_and_hand_back() {
    const FgCtx ctx = ::foundation::effects::testing::foreground();
    EndpointSpsc channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<EndpointSpsc::producer_tag, EndpointSpsc::consumer_tag>(endpoint_root());

    // The producer endpoint reads a snapshot, so its context holds Nondet.
    auto producer = channels::mint_endpoint<EndpointSpsc, channels::Direction::Producer>(
        SnapshotCtx{::foundation::effects::testing::test()}, channel.producer(std::move(producer_perm)));
    auto consumer = channels::mint_endpoint<EndpointSpsc, channels::Direction::Consumer>(
        ctx, channel.consumer(std::move(consumer_perm)));

    if (!producer.try_send(7)) return fail("the producer endpoint was refused below capacity");
    if (producer.size_approx() != 1) return fail("the producer endpoint saw the wrong size");
    // An empty read gives the value -1, which no send gave.
    if (consumer.try_recv().value_or(-1) != 7) return fail("the consumer endpoint read the wrong value");

    // A moved endpoint takes the handle along, and the new endpoint still
    // reaches the channel.
    auto moved = std::move(producer);
    if (!moved.try_send(8)) return fail("the moved endpoint lost its channel");

    // into_handle gives the only handle that holds the permission.
    auto handle = std::move(moved).into_handle();
    if (!handle.try_push(9)) return fail("the handle from into_handle lost its channel");

    // into_session starts the default protocol over the same handle.
    std::optional session{std::move(consumer).into_session()};
    auto [first, after_first] = std::move(*session).recv(pop_int);
    session.emplace(std::move(after_first));
    auto [second, after_second] = std::move(*session).recv(pop_int);
    session.emplace(std::move(after_second));
    if (first != 8 || second != 9) return fail("the endpoint session read the wrong values");
    std::move(*session).detach(s::detach_reason::InfiniteLoopProtocol{});
    if (!channel.empty_approx(kSnapshot)) return fail("the endpoint session left values in the channel");
    return 0;
}

// ── The stage bridge ─────────────────────────────────────────────────

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
void doubling_stage(StageIn::ConsumerHandle&& in, StageOut::ProducerHandle&& out) noexcept {
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

    auto feeder = in_channel.producer(std::move(in_producer_perm));
    auto drain = out_channel.consumer(std::move(out_consumer_perm));
    (void)feeder.try_push(1);
    (void)feeder.try_push(2);

    auto in_ep = channels::mint_endpoint<StageIn, channels::Direction::Consumer>(
        ctx, in_channel.consumer(std::move(in_consumer_perm)));
    auto out_ep = channels::mint_endpoint<StageOut, channels::Direction::Producer>(
        ctx, out_channel.producer(std::move(out_producer_perm)));
    auto stage = channels::mint_stage_from_endpoints<&doubling_stage>(ctx, std::move(in_ep), std::move(out_ep));
    std::move(stage).run();

    if (drain.try_pop().value_or(-1) != 2 || drain.try_pop().value_or(-1) != 4)
        return fail("the stage did not run its body");
    return 0;
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

void forwarding_stage(FeedIn::ConsumerHandle&& in, FeedOut::ProducerHandle&& out) noexcept {
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

    auto feeder = in_channel.producer(std::move(in_producer_perm));
    auto drain = out_channel.consumer(std::move(out_consumer_perm));
    (void)feeder.try_push(1);
    (void)feeder.try_push(2);

    auto out_producer = out_channel.producer();
    if (out_producer.is_none()) return fail("the MPSC channel lent no producer share");
    auto stage = channels::mint_stage_from_endpoints<&forwarding_stage>(
        ctx,
        channels::mint_endpoint<FeedIn, channels::Direction::Consumer>(
            ctx, in_channel.consumer(std::move(in_consumer_perm))),
        channels::mint_endpoint<FeedOut, channels::Direction::Producer>(
            ctx, std::move(out_producer).expect("the test above found a producer share")));
    std::move(stage).run();

    // A pop of an MPSC channel is a merge, so it takes the tag of Nondet.
    const ::foundation::effects::cap::Nondet merge = ::foundation::effects::testing::test().nondet;
    if (drain.try_pop(merge).value_or(-1) != 101 || drain.try_pop(merge).value_or(-1) != 102)
        return fail("the stage did not feed the MPSC channel");
    return 0;
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
void summing_stage(FanLeft::ConsumerHandle&& left, FanRight::ConsumerHandle&& right,
                   FanOut::ProducerHandle&& out) noexcept {
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

    auto left_feeder = left.producer(std::move(left_producer_perm));
    auto right_feeder = right.producer(std::move(right_producer_perm));
    auto drain = out.consumer(std::move(out_consumer_perm));
    (void)left_feeder.try_push(3);
    (void)right_feeder.try_push(4);

    auto stage = channels::mint_mpmc_stage_from_endpoints<&summing_stage>(
        ctx,
        channels::mint_endpoint<FanLeft, channels::Direction::Consumer>(ctx,
                                                                        left.consumer(std::move(left_consumer_perm))),
        channels::mint_endpoint<FanRight, channels::Direction::Consumer>(
            ctx, right.consumer(std::move(right_consumer_perm))),
        channels::mint_endpoint<FanOut, channels::Direction::Producer>(ctx,
                                                                       out.producer(std::move(out_producer_perm))));
    std::move(stage).run();

    if (drain.try_pop().value_or(-1) != 7) return fail("the fan-in stage did not run its body");
    return 0;
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

void publishing_stage(SwmrIn::ConsumerHandle&& in, LastValueWriter&& writer) noexcept {
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
    auto feeder = channel.producer(std::move(producer_perm));
    (void)feeder.try_push(5);
    (void)feeder.try_push(6);

    ::foundation::core::Atomic<int> last{0};
    auto stage = channels::mint_swmr_stage_from_endpoint<&publishing_stage>(
        ctx,
        channels::mint_endpoint<SwmrIn, channels::Direction::Consumer>(ctx, channel.consumer(std::move(consumer_perm))),
        LastValueWriter{.last = last});
    std::move(stage).run();
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
    auto consumer = channel.consumer(std::move(consumer_perm));

    s::SessionEventLog log;
    auto recorded =
        channels::mint_recording_endpoint(channels::mint_endpoint<RecordSpsc, channels::Direction::Producer>(
                                              ctx, channel.producer(std::move(producer_perm))),
                                          s::mint_session_event_writer(log), s::RoleTagId{1}, s::RoleTagId{2});
    auto after_send = std::move(recorded).send(11, push_int);
    std::move(after_send).detach(s::detach_reason::InfiniteLoopProtocol{});

    if (consumer.try_pop().value_or(-1) != 11) return fail("the recorded endpoint did not send its value");
    if (log.size() == 0 || log[0].op() != s::SessionOp::Send) return fail("the recorded endpoint wrote no send event");
    return 0;
}

}  // namespace

int main() {
    int failures = 0;
    failures += bridge_round_trip();
    failures += endpoint_send_recv_and_hand_back();
    failures += stage_from_endpoints_runs_its_body();
    failures += stage_feeds_an_mpsc_channel();
    failures += mpmc_stage_from_endpoints_runs_its_body();
    failures += swmr_stage_runs_its_body();
    failures += recording_endpoint_records_each_step();
    if (failures != 0) {
        std::fprintf(stderr, "test_endpoint: %d check(s) failed\n", failures);
        return 1;
    }
    ::fixy::check::pass(kTest, "test_endpoint: all checks passed\n");
    return 0;
}
