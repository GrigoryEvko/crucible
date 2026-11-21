// The typed session over an SPSC channel handle against the bare handle.
//
// mint_substrate_session (fixy/channels/SubstrateSessionBridge.h) takes a
// channel handle by move, so the session owns the handle for its whole
// life.  A send or a receive goes through the write or the read of the pole
// table of the bridge: a trying push or a polling pop of the handle, in the
// context of the step.  A full or an empty channel makes the session wait
// through its watch and try again.
//
// Four arms form a 2x2 matrix, each a round trip of one push and one pop,
// so the ring holds zero or one item for the whole run.  A push-only body
// would fill the ring within milliseconds of the auto-batched run, and a
// pop-only body would drain it.
//
//   bare push + bare pop      (baseline)
//   typed send + bare pop     (the producer runs a session)
//   bare push + typed recv    (the consumer runs a session)
//   typed send + typed recv   (both run a session)
//
// The deltas against the baseline give the cost of each side.  A session
// owns its handle, so each arm has a channel of its own, with a tag of its
// own.  A channel handle cannot be reassigned, so a typed arm keeps its
// session in a Seat of foundation/core/Seat.h.  Each step runs on the
// session in the storage of the Seat and puts the next session there.
//
// Single-threaded: the ring's atomics are uncontended, which isolates the
// cost of one operation.  test/fixy/test_concurrent_channels.cpp covers the
// cross-thread behaviour.  The numbers are for inspection, and the program
// exits 0.

#include <fixy/Core.h>
#include <fixy/channels/SubstrateSessionBridge.h>
#include <fixy/concurrent/PermissionedChannel.h>

#include <foundation/Platform.h>
#include <foundation/core/Seat.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include "bench_harness.h"

#include <cstdint>
#include <utility>

namespace {

namespace cc = ::fixy::concurrent;
namespace channels = ::fixy::channels;
namespace s = ::fixy::session;
namespace perm = ::foundation::permissions;

using Item = std::uint64_t;

constexpr ::foundation::effects::Alloc kAlloc = ::foundation::effects::testing::test().alloc;

// 2^20 slots of 8 bytes.  Each body pops the item it pushed, so the ring
// never fills.
inline constexpr std::size_t kRingSlots = std::size_t{1} << 20;

template <int Arm>
struct ArmTag {};

// The root site of the channel of each arm.  The channel type names the
// brand of that site.
template <int Arm>
auto arm_root() noexcept {
    return perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::wire, ArmTag<Arm>>>();
}

template <int Arm>
using Channel = cc::channel_t<Item, kRingSlots, decltype(arm_root<Arm>())>;

// The context of each typed step.  The pole table of the bridge writes and
// reads the ring, each one time.
constexpr auto kForeground = ::foundation::effects::testing::foreground();

// A rejected push means the next pop finds an empty ring, so the arm does
// not measure a round trip.  The arm stops at the first one.
[[noreturn]] CRUCIBLE_COLD void stop_on_rejected_push(::fixy::TextView bench_name) noexcept {
    ::fixy::fatal("bench_spsc_session: {}: the ring rejected a push, so the round trip is not measured", bench_name);
}

// The bare pop takes the value out of the Option, so the barrier sees an
// Item, as it does after the typed receive.
[[nodiscard]] inline Item pop_value(auto& consumer) noexcept { return consumer.try_pop().value_or(Item{0}); }

// Each arm holds its channel on the heap (8 MB of ring) in a Box, and the two
// handles of the channel as its locals.  The brand of the root of an arm
// stays in the function of the arm.

[[nodiscard]] bench::Report bare_push_bare_pop() {
    constexpr ::fixy::TextView name = "round-trip: bare push + bare pop";
    ::fixy::Box<Channel<0>> channel = ::fixy::mint_box<Channel<0>>(kAlloc);
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Channel<0>::producer_tag, Channel<0>::consumer_tag>(arm_root<0>());
    auto producer = (*channel).producer(std::move(producer_perm));
    auto consumer = (*channel).consumer(std::move(consumer_perm));
    Item item = 0;
    return bench::run(name, [&] {
        if (!producer.try_push(++item)) [[unlikely]]
            stop_on_rejected_push(name);
        bench::do_not_optimize(pop_value(consumer));
    });
}

// One send of the session in a Seat: the step runs the send on the session in
// the storage of the Seat.
template <typename Session>
[[gnu::always_inline]] inline void send_in_seat(::foundation::core::Seat<Session>& seat, Item item) noexcept {
    seat.step([item](Session&& session) noexcept { return std::move(session).send(kForeground, item); });
}

// One receive of the session in a Seat.  It gives the received item.
template <typename Session>
[[nodiscard, gnu::always_inline]] inline Item recv_in_seat(::foundation::core::Seat<Session>& seat) noexcept {
    Item received = 0;
    seat.step([&received](Session&& session) noexcept {
        auto [value, next] = std::move(session).recv(kForeground);
        received = value;
        return std::move(next);
    });
    return received;
}

// A typed arm gives the Seat of its session to run_with, which holds it for
// the row and gives it back.  The arm then ends the session at its detach.

[[nodiscard]] bench::Report typed_send_bare_pop() {
    constexpr ::fixy::TextView name = "round-trip: typed send + bare pop";
    ::fixy::Box<Channel<1>> channel = ::fixy::mint_box<Channel<1>>(kAlloc);
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Channel<1>::producer_tag, Channel<1>::consumer_tag>(arm_root<1>());
    auto producer = (*channel).producer(std::move(producer_perm));
    auto consumer = (*channel).consumer(std::move(consumer_perm));
    Item item = 0;
    auto [report, session] = bench::run_with(
        name,
        ::foundation::core::mint_seat(channels::mint_substrate_session<Channel<1>, channels::Direction::Producer>(
            kForeground, std::move(producer))),
        [&item, &consumer](auto& seat) noexcept {
            send_in_seat(seat, ++item);
            bench::do_not_optimize(pop_value(consumer));
        });
    std::move(session).take().detach(s::detach_reason::InfiniteLoopProtocol{});
    return std::move(report);
}

[[nodiscard]] bench::Report bare_push_typed_recv() {
    constexpr ::fixy::TextView name = "round-trip: bare push + typed recv";
    ::fixy::Box<Channel<2>> channel = ::fixy::mint_box<Channel<2>>(kAlloc);
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Channel<2>::producer_tag, Channel<2>::consumer_tag>(arm_root<2>());
    auto producer = (*channel).producer(std::move(producer_perm));
    auto consumer = (*channel).consumer(std::move(consumer_perm));
    Item item = 0;
    auto [report, session] = bench::run_with(
        name,
        ::foundation::core::mint_seat(channels::mint_substrate_session<Channel<2>, channels::Direction::Consumer>(
            kForeground, std::move(consumer))),
        [&item, &producer](auto& seat) noexcept {
            if (!producer.try_push(++item)) [[unlikely]]
                stop_on_rejected_push(name);
            bench::do_not_optimize(recv_in_seat(seat));
        });
    std::move(session).take().detach(s::detach_reason::InfiniteLoopProtocol{});
    return std::move(report);
}

[[nodiscard]] bench::Report typed_send_typed_recv() {
    constexpr ::fixy::TextView name = "round-trip: typed send + typed recv";
    ::fixy::Box<Channel<3>> channel = ::fixy::mint_box<Channel<3>>(kAlloc);
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Channel<3>::producer_tag, Channel<3>::consumer_tag>(arm_root<3>());
    auto producer = (*channel).producer(std::move(producer_perm));
    auto consumer = (*channel).consumer(std::move(consumer_perm));
    Item item = 0;
    auto [report, seats] = bench::run_with(
        name,
        ::fixy::Record{
            ::foundation::core::mint_seat(channels::mint_substrate_session<Channel<3>, channels::Direction::Producer>(
                kForeground, std::move(producer))),
            ::foundation::core::mint_seat(channels::mint_substrate_session<Channel<3>, channels::Direction::Consumer>(
                kForeground, std::move(consumer)))},
        [&item](auto& held) noexcept {
            auto& [sender, receiver] = held;
            send_in_seat(sender, ++item);
            bench::do_not_optimize(recv_in_seat(receiver));
        });
    auto [sender, receiver] = std::move(seats);
    std::move(sender).take().detach(s::detach_reason::InfiniteLoopProtocol{});
    std::move(receiver).take().detach(s::detach_reason::InfiniteLoopProtocol{});
    return std::move(report);
}

}  // namespace

int main() {
    ::fixy::FixedArray<bench::Report, 4> reports{bare_push_bare_pop(), typed_send_bare_pop(), bare_push_typed_recv(),
                                                 typed_send_typed_recv()};
    bench::emit_reports_text(reports.view());

    bench::print("\n=== SPSC session: cost of each side against the bare round trip ===\n");
    ::fixy::FixedArray<bench::Compare, 3> const compares{
        bench::compare(reports.at(::fixy::index<0>()), reports.at(::fixy::index<1>())),  // the typed send
        bench::compare(reports.at(::fixy::index<0>()), reports.at(::fixy::index<2>())),  // the typed receive
        bench::compare(reports.at(::fixy::index<0>()), reports.at(::fixy::index<3>())),  // both
    };
    bench::emit_compares(compares.view());

    bench::emit_reports_json(reports.view(), bench::env_json());
    return 0;
}
