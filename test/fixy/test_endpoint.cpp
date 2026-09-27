// What fixy/concurrent/SubstrateSessionBridge.h, Endpoint.h,
// StageEndpointBridge.h and EndpointMint.h claim, checked at run time.
//
// Each check moves real values through a real channel.  A channel protocol
// loops with no exit, so each session ends with a typed detach, which
// releases the channel handle that the session owns.

#include <fixy/concurrent/EndpointMint.h>
#include <fixy/concurrent/Endpoint.h>
#include <fixy/concurrent/StageEndpointBridge.h>
#include <fixy/concurrent/SubstrateSessionBridge.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <cstdio>
#include <optional>
#include <type_traits>
#include <utility>

namespace c = fixy::concurrent;
namespace s = fixy::session;
namespace perm = foundation::permissions;

namespace {

using FgCtx = ::foundation::effects::ExecCtx<::foundation::effects::ctx_cap::Fg, ::foundation::effects::Row<>>;

[[nodiscard]] int fail(const char* what) noexcept {
    std::fprintf(stderr, "test_endpoint: %s\n", what);
    return 1;
}

// The transports of the channel protocols try once.  A full channel or an
// empty one makes the session wait and try again.
constexpr auto push_int = [](auto& handle, int& value) noexcept -> bool { return handle.try_push(value); };
constexpr auto pop_int = [](auto& handle) noexcept -> std::optional<int> { return handle.try_pop(); };

// ── The bridge ───────────────────────────────────────────────────────

struct BridgeTag {};
using BridgeSpsc = c::PermissionedSpscChannel<int, 8, BridgeTag>;

[[nodiscard]] int bridge_round_trip() {
    const FgCtx ctx = ::foundation::effects::testing::foreground();
    BridgeSpsc channel{};
    auto [producer_perm, consumer_perm] = perm::mint_permission_split<BridgeSpsc::producer_tag, BridgeSpsc::consumer_tag>(
        perm::mint_permission_root<BridgeSpsc::whole_tag>());

    std::optional producer{
        c::mint_substrate_session<BridgeSpsc, c::Direction::Producer>(ctx, channel.producer(std::move(producer_perm)))};
    for (int value = 1; value <= 3; ++value) {
        producer.emplace(std::move(*producer).send(value * 10, push_int));
    }
    std::move(*producer).detach(s::detach_reason::InfiniteLoopProtocol{});
    if (channel.size_approx() != 3) return fail("the producer session left the wrong number of values");

    std::optional consumer{
        c::mint_substrate_session<BridgeSpsc, c::Direction::Consumer>(ctx, channel.consumer(std::move(consumer_perm)))};
    for (int value = 1; value <= 3; ++value) {
        auto [got, next] = std::move(*consumer).recv(pop_int);
        if (got != value * 10) return fail("the consumer session read the wrong value");
        consumer.emplace(std::move(next));
    }
    std::move(*consumer).detach(s::detach_reason::InfiniteLoopProtocol{});
    if (!channel.empty_approx()) return fail("the consumer session left values in the channel");
    return 0;
}

// ── The endpoint ─────────────────────────────────────────────────────

struct EndpointTag {};
using EndpointSpsc = c::PermissionedSpscChannel<int, 8, EndpointTag>;

[[nodiscard]] int endpoint_send_recv_and_hand_back() {
    const FgCtx ctx = ::foundation::effects::testing::foreground();
    EndpointSpsc channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<EndpointSpsc::producer_tag, EndpointSpsc::consumer_tag>(
            perm::mint_permission_root<EndpointSpsc::whole_tag>());

    auto producer = c::mint_endpoint<EndpointSpsc, c::Direction::Producer>(ctx, channel.producer(std::move(producer_perm)));
    auto consumer = c::mint_endpoint<EndpointSpsc, c::Direction::Consumer>(ctx, channel.consumer(std::move(consumer_perm)));

    if (!producer.try_send(7)) return fail("the producer endpoint was refused below capacity");
    if (producer.size_approx() != 1) return fail("the producer endpoint saw the wrong size");
    const std::optional<int> got = consumer.try_recv();
    if (!got || *got != 7) return fail("the consumer endpoint read the wrong value");

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
    if (!channel.empty_approx()) return fail("the endpoint session left values in the channel");
    return 0;
}

// ── The stage bridge ─────────────────────────────────────────────────

struct StageInTag {};
struct StageOutTag {};
using StageIn = c::PermissionedSpscChannel<int, 8, StageInTag>;
using StageOut = c::PermissionedSpscChannel<int, 8, StageOutTag>;

// The body doubles each value it drains.
void doubling_stage(StageIn::ConsumerHandle&& in, StageOut::ProducerHandle&& out) noexcept {
    while (const std::optional<int> value = in.try_pop()) {
        (void)out.try_push(*value * 2);
    }
}

[[nodiscard]] int stage_from_endpoints_runs_its_body() {
    const FgCtx ctx = ::foundation::effects::testing::foreground();
    StageIn in_channel{};
    StageOut out_channel{};
    auto [in_producer_perm, in_consumer_perm] = perm::mint_permission_split<StageIn::producer_tag, StageIn::consumer_tag>(
        perm::mint_permission_root<StageIn::whole_tag>());
    auto [out_producer_perm, out_consumer_perm] =
        perm::mint_permission_split<StageOut::producer_tag, StageOut::consumer_tag>(
            perm::mint_permission_root<StageOut::whole_tag>());

    auto feeder = in_channel.producer(std::move(in_producer_perm));
    auto drain = out_channel.consumer(std::move(out_consumer_perm));
    (void)feeder.try_push(1);
    (void)feeder.try_push(2);

    auto in_ep = c::mint_endpoint<StageIn, c::Direction::Consumer>(ctx, in_channel.consumer(std::move(in_consumer_perm)));
    auto out_ep =
        c::mint_endpoint<StageOut, c::Direction::Producer>(ctx, out_channel.producer(std::move(out_producer_perm)));
    auto stage = c::mint_stage_from_endpoints<&doubling_stage>(ctx, std::move(in_ep), std::move(out_ep));
    std::move(stage).run();

    const std::optional<int> first = drain.try_pop();
    const std::optional<int> second = drain.try_pop();
    if (!first || !second || *first != 2 || *second != 4) return fail("the stage did not run its body");
    return 0;
}

// A stage that feeds an MPSC channel.  The MPSC producer handle has the
// producer pole, so the body is a stage and the endpoint mint takes it.
struct FeedInTag {};
struct FeedOutTag {};
using FeedIn = c::PermissionedSpscChannel<int, 8, FeedInTag>;
using FeedOut = c::PermissionedMpscChannel<int, 8, FeedOutTag>;

void forwarding_stage(FeedIn::ConsumerHandle&& in, FeedOut::ProducerHandle&& out) noexcept {
    while (const std::optional<int> value = in.try_pop()) {
        (void)out.try_push(*value + 100);
    }
}

[[nodiscard]] int stage_feeds_an_mpsc_channel() {
    const FgCtx ctx = ::foundation::effects::testing::foreground();
    FeedIn in_channel{};
    FeedOut out_channel{};
    auto [in_producer_perm, in_consumer_perm] = perm::mint_permission_split<FeedIn::producer_tag, FeedIn::consumer_tag>(
        perm::mint_permission_root<FeedIn::whole_tag>());
    auto out_consumer_perm = perm::mint_permission_root<FeedOut::consumer_tag>();

    auto feeder = in_channel.producer(std::move(in_producer_perm));
    auto drain = out_channel.consumer(std::move(out_consumer_perm));
    (void)feeder.try_push(1);
    (void)feeder.try_push(2);

    std::optional out_producer = out_channel.producer();
    if (!out_producer) return fail("the MPSC channel lent no producer share");
    auto stage = c::mint_stage_from_endpoints<&forwarding_stage>(
        ctx, c::mint_endpoint<FeedIn, c::Direction::Consumer>(ctx, in_channel.consumer(std::move(in_consumer_perm))),
        c::mint_endpoint<FeedOut, c::Direction::Producer>(ctx, std::move(*out_producer)));
    std::move(stage).run();

    const std::optional<int> first = drain.try_pop();
    const std::optional<int> second = drain.try_pop();
    if (!first || !second || *first != 101 || *second != 102) return fail("the stage did not feed the MPSC channel");
    return 0;
}

struct FanLeftTag {};
struct FanRightTag {};
struct FanOutTag {};
using FanLeft = c::PermissionedSpscChannel<int, 8, FanLeftTag>;
using FanRight = c::PermissionedSpscChannel<int, 8, FanRightTag>;
using FanOut = c::PermissionedSpscChannel<int, 8, FanOutTag>;

// The body sums one value from each input.
void summing_stage(FanLeft::ConsumerHandle&& left, FanRight::ConsumerHandle&& right,
                   FanOut::ProducerHandle&& out) noexcept {
    const std::optional<int> a = left.try_pop();
    const std::optional<int> b = right.try_pop();
    if (a && b) (void)out.try_push(*a + *b);
}

[[nodiscard]] int mpmc_stage_from_endpoints_runs_its_body() {
    const FgCtx ctx = ::foundation::effects::testing::foreground();
    FanLeft left{};
    FanRight right{};
    FanOut out{};
    auto [left_producer_perm, left_consumer_perm] = perm::mint_permission_split<FanLeft::producer_tag, FanLeft::consumer_tag>(
        perm::mint_permission_root<FanLeft::whole_tag>());
    auto [right_producer_perm, right_consumer_perm] =
        perm::mint_permission_split<FanRight::producer_tag, FanRight::consumer_tag>(
            perm::mint_permission_root<FanRight::whole_tag>());
    auto [out_producer_perm, out_consumer_perm] = perm::mint_permission_split<FanOut::producer_tag, FanOut::consumer_tag>(
        perm::mint_permission_root<FanOut::whole_tag>());

    auto left_feeder = left.producer(std::move(left_producer_perm));
    auto right_feeder = right.producer(std::move(right_producer_perm));
    auto drain = out.consumer(std::move(out_consumer_perm));
    (void)left_feeder.try_push(3);
    (void)right_feeder.try_push(4);

    auto stage = c::mint_mpmc_stage_from_endpoints<&summing_stage>(
        ctx, c::mint_endpoint<FanLeft, c::Direction::Consumer>(ctx, left.consumer(std::move(left_consumer_perm))),
        c::mint_endpoint<FanRight, c::Direction::Consumer>(ctx, right.consumer(std::move(right_consumer_perm))),
        c::mint_endpoint<FanOut, c::Direction::Producer>(ctx, out.producer(std::move(out_producer_perm))));
    std::move(stage).run();

    const std::optional<int> sum = drain.try_pop();
    if (!sum || *sum != 7) return fail("the fan-in stage did not run its body");
    return 0;
}

// A writer with the publish shape of a single-writer cell.  It keeps the
// last value it was given.
struct LastValueWriter {
    int* last = nullptr;
    void publish(int const& value) noexcept { *last = value; }
};

struct SwmrInTag {};
using SwmrIn = c::PermissionedSpscChannel<int, 8, SwmrInTag>;

void publishing_stage(SwmrIn::ConsumerHandle&& in, LastValueWriter&& writer) noexcept {
    while (const std::optional<int> value = in.try_pop()) {
        writer.publish(*value);
    }
}

[[nodiscard]] int swmr_stage_runs_its_body() {
    const FgCtx ctx = ::foundation::effects::testing::foreground();
    SwmrIn channel{};
    auto [producer_perm, consumer_perm] = perm::mint_permission_split<SwmrIn::producer_tag, SwmrIn::consumer_tag>(
        perm::mint_permission_root<SwmrIn::whole_tag>());
    auto feeder = channel.producer(std::move(producer_perm));
    (void)feeder.try_push(5);
    (void)feeder.try_push(6);

    int last = 0;
    auto stage = c::mint_swmr_stage<&publishing_stage>(
        ctx, c::mint_endpoint<SwmrIn, c::Direction::Consumer>(ctx, channel.consumer(std::move(consumer_perm))),
        LastValueWriter{.last = &last});
    std::move(stage).run();
    if (last != 6) return fail("the single-writer stage did not publish the last value");
    return 0;
}

// ── The recording wrapper ────────────────────────────────────────────

struct RecordTag {};
using RecordSpsc = c::PermissionedSpscChannel<int, 8, RecordTag>;

[[nodiscard]] int recording_endpoint_records_each_step() {
    const FgCtx ctx = ::foundation::effects::testing::foreground();
    RecordSpsc channel{};
    auto [producer_perm, consumer_perm] = perm::mint_permission_split<RecordSpsc::producer_tag, RecordSpsc::consumer_tag>(
        perm::mint_permission_root<RecordSpsc::whole_tag>());
    auto consumer = channel.consumer(std::move(consumer_perm));

    s::SessionEventLog log;
    auto recorded = c::mint_recording_endpoint(
        c::mint_endpoint<RecordSpsc, c::Direction::Producer>(ctx, channel.producer(std::move(producer_perm))), log,
        s::RoleTagId{1}, s::RoleTagId{2});
    auto after_send = std::move(recorded).send(11, push_int);
    std::move(after_send).detach(s::detach_reason::InfiniteLoopProtocol{});

    const std::optional<int> got = consumer.try_pop();
    if (!got || *got != 11) return fail("the recorded endpoint did not send its value");
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
    std::puts("test_endpoint: all checks passed");
    return 0;
}
