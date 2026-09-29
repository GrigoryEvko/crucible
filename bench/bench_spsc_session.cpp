// The typed session over an SPSC channel handle against the bare handle.
//
// mint_substrate_session (fixy/concurrent/SubstrateSessionBridge.h) takes a
// channel handle by move, so the session owns the handle for its whole
// life, and a send or a receive goes through a transport: a trying write
// (bool(handle&, Item&)) or a polling read (std::optional<Item>(handle&)).
// A full or an empty channel makes the session wait through its watch and
// try again.
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
// session in a std::optional and re-seats it with emplace, as
// test/fixy/test_endpoint.cpp does.
//
// Single-threaded: the ring's atomics are uncontended, which isolates the
// cost of one operation.  test/fixy/test_concurrent_channels.cpp covers the
// cross-thread behaviour.  The numbers are for inspection, and the program
// exits 0.

#include <fixy/concurrent/PermissionedSpscChannel.h>
#include <fixy/concurrent/SubstrateSessionBridge.h>

#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include "bench_harness.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <utility>

namespace {

namespace cc = ::fixy::concurrent;
namespace s = ::fixy::session;
namespace perm = ::foundation::permissions;

using Item = std::uint64_t;

// 2^20 slots of 8 bytes.  Each body pops the item it pushed, so the ring
// never fills.
inline constexpr std::size_t kRingSlots = std::size_t{1} << 20;

template <int Arm>
struct ArmTag {};

template <int Arm>
using Channel = cc::PermissionedSpscChannel<Item, kRingSlots, ArmTag<Arm>>;

constexpr auto kForeground = ::foundation::effects::testing::foreground();

// The transports of the typed arms: each tries once.
constexpr auto try_push_item = [](auto& producer, Item& item) noexcept { return producer.try_push(item); };
constexpr auto try_pop_item = [](auto& consumer) noexcept { return consumer.try_pop(); };

// A rejected push means the next pop finds an empty ring, so the arm does
// not measure a round trip.  The arm stops at the first one.
[[noreturn]] CRUCIBLE_COLD void stop_on_rejected_push(const char* bench_name) noexcept {
    std::fprintf(stderr, "bench_spsc_session: %s: the ring rejected a push, so the round trip is not measured\n",
                 bench_name);
    std::abort();
}

// The bare pop takes the value out of the optional, so the barrier sees an
// Item, as it does after the typed receive.
[[nodiscard]] inline Item pop_value(auto& consumer) noexcept { return consumer.try_pop().value_or(Item{0}); }

// One channel per arm on the heap (8 MB of ring), and its two handles.
template <int Arm>
struct Rig {
    std::unique_ptr<Channel<Arm>> channel = std::make_unique<Channel<Arm>>();
    std::pair<typename Channel<Arm>::ProducerHandle, typename Channel<Arm>::ConsumerHandle> handles = [this] {
        auto [producer_perm, consumer_perm] =
            perm::mint_permission_split<typename Channel<Arm>::producer_tag, typename Channel<Arm>::consumer_tag>(
                perm::mint_permission_root<typename Channel<Arm>::whole_tag>());
        return std::pair{channel->producer(std::move(producer_perm)), channel->consumer(std::move(consumer_perm))};
    }();
};

[[nodiscard]] bench::Report bare_push_bare_pop() {
    constexpr const char* name = "round-trip: bare push + bare pop";
    Rig<0> rig;
    auto& [producer, consumer] = rig.handles;
    Item item = 0;
    return bench::run(name, [&] {
        if (!producer.try_push(++item)) [[unlikely]]
            stop_on_rejected_push(name);
        bench::do_not_optimize(pop_value(consumer));
    });
}

[[nodiscard]] bench::Report typed_send_bare_pop() {
    constexpr const char* name = "round-trip: typed send + bare pop";
    Rig<1> rig;
    auto& [producer, consumer] = rig.handles;
    std::optional session{
        cc::mint_substrate_session<Channel<1>, cc::Direction::Producer>(kForeground, std::move(producer))};
    Item item = 0;
    auto report = bench::run(name, [&] {
        session.emplace(std::move(*session).send(++item, try_push_item));
        bench::do_not_optimize(pop_value(consumer));
    });
    std::move(*session).detach(s::detach_reason::InfiniteLoopProtocol{});
    return report;
}

[[nodiscard]] bench::Report bare_push_typed_recv() {
    constexpr const char* name = "round-trip: bare push + typed recv";
    Rig<2> rig;
    auto& [producer, consumer] = rig.handles;
    std::optional session{
        cc::mint_substrate_session<Channel<2>, cc::Direction::Consumer>(kForeground, std::move(consumer))};
    Item item = 0;
    auto report = bench::run(name, [&] {
        if (!producer.try_push(++item)) [[unlikely]]
            stop_on_rejected_push(name);
        auto [value, next] = std::move(*session).recv(try_pop_item);
        bench::do_not_optimize(value);
        session.emplace(std::move(next));
    });
    std::move(*session).detach(s::detach_reason::InfiniteLoopProtocol{});
    return report;
}

[[nodiscard]] bench::Report typed_send_typed_recv() {
    constexpr const char* name = "round-trip: typed send + typed recv";
    Rig<3> rig;
    auto& [producer, consumer] = rig.handles;
    std::optional sender{
        cc::mint_substrate_session<Channel<3>, cc::Direction::Producer>(kForeground, std::move(producer))};
    std::optional receiver{
        cc::mint_substrate_session<Channel<3>, cc::Direction::Consumer>(kForeground, std::move(consumer))};
    Item item = 0;
    auto report = bench::run(name, [&] {
        sender.emplace(std::move(*sender).send(++item, try_push_item));
        auto [value, next] = std::move(*receiver).recv(try_pop_item);
        bench::do_not_optimize(value);
        receiver.emplace(std::move(next));
    });
    std::move(*sender).detach(s::detach_reason::InfiniteLoopProtocol{});
    std::move(*receiver).detach(s::detach_reason::InfiniteLoopProtocol{});
    return report;
}

}  // namespace

int main() {
    std::array reports{bare_push_bare_pop(), typed_send_bare_pop(), bare_push_typed_recv(), typed_send_typed_recv()};
    bench::emit_reports_text(reports);

    std::printf("\n=== SPSC session: cost of each side against the bare round trip ===\n");
    const std::array compares{
        bench::compare(reports[0], reports[1]),  // the typed send
        bench::compare(reports[0], reports[2]),  // the typed receive
        bench::compare(reports[0], reports[3]),  // both
    };
    bench::emit_compares(compares);

    bench::emit_reports_json(reports, bench::env_json());
    return 0;
}
