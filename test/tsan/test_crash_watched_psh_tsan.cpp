// GAPS-046 — CrashWatchedPSH permission-recovery race harness.
//
// This TU is intentionally registered as a normal crucible_test target:
// the default preset gives fast regression coverage, and the tsan
// preset compiles the same target with -fsanitize=thread plus the
// repository TSAN suppressions.

#include <crucible/bridges/CrashTransport.h>
#include "../test_assert.h"

#include <atomic>
#include <cstdio>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

namespace {

using namespace crucible::safety;
using namespace crucible::safety::proto;

struct WorkerTag {};
struct CoordTag {};
struct MasterTag {};
struct WorkerFallbackTag {};

struct SharedWire {
    std::atomic<int> delivered{0};
    std::atomic<int> last_value{0};
};

struct Channel {
    SharedWire* wire = nullptr;
    int         id = 0;
};

void send_int(Channel& channel, int value) noexcept {
    channel.wire->last_value.store(value, std::memory_order_relaxed);
    channel.wire->delivered.fetch_add(1, std::memory_order_relaxed);
}

int recv_int(Channel& channel) noexcept {
    channel.wire->delivered.fetch_add(1, std::memory_order_relaxed);
    return channel.wire->last_value.load(std::memory_order_relaxed);
}

void send_coord_transfer(Channel& channel,
                         Transferable<int, CoordTag>&& value) noexcept {
    channel.wire->last_value.store(value.value, std::memory_order_relaxed);
    channel.wire->delivered.fetch_add(1, std::memory_order_relaxed);
}

template <typename Flag>
void signal_after_start(Flag& flag, std::atomic<bool>& start) noexcept {
    while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    flag.signal();
}

}  // namespace

namespace crucible::permissions {

template <>
struct survivor_registry<WorkerTag> {
    using type = inheritance_list<CoordTag>;
};

template <>
struct survivor_registry<CoordTag> {
    using type = inheritance_list<MasterTag>;
};

template <>
struct survivor_registry<WorkerFallbackTag> {
    using type = inheritance_list<MasterTag>;
};

}  // namespace crucible::permissions

namespace {

static_assert(std::is_same_v<
    proto::detail::crash_event_for_t<WorkerTag, Channel>,
    CrashEvent<WorkerTag, Channel, CoordTag>>);
static_assert(std::is_same_v<
    proto::detail::crash_event_for_t<CoordTag, Channel>,
    CrashEvent<CoordTag, Channel, MasterTag>>);
static_assert(std::is_same_v<
    proto::detail::crash_event_for_t<WorkerFallbackTag, Channel>,
    CrashEvent<WorkerFallbackTag, Channel, MasterTag>>);

void verify_three_permissioned_peers(int iteration) {
    SharedWire worker_wire;
    SharedWire coord_wire;
    SharedWire master_wire;

    auto worker_perm = mint_permission_root<WorkerTag>();
    auto coord_perm = mint_permission_root<CoordTag>();
    auto master_perm = mint_permission_root<MasterTag>();

    auto worker = mint_permissioned_session<End>(
        Channel{&worker_wire, 1000 + iteration}, std::move(worker_perm));
    auto coord = mint_permissioned_session<End>(
        Channel{&coord_wire, 2000 + iteration}, std::move(coord_perm));
    auto master = mint_permissioned_session<End>(
        Channel{&master_wire, 3000 + iteration}, std::move(master_perm));

    static_assert(std::is_same_v<typename decltype(worker)::perm_set,
                                 PermSet<WorkerTag>>);
    static_assert(std::is_same_v<typename decltype(coord)::perm_set,
                                 PermSet<CoordTag>>);
    static_assert(std::is_same_v<typename decltype(master)::perm_set,
                                 PermSet<MasterTag>>);

    std::move(worker).detach(detach_reason::TestInstrumentation{});
    std::move(coord).detach(detach_reason::TestInstrumentation{});
    std::move(master).detach(detach_reason::TestInstrumentation{});
}

void scenario_clean_peer_death(int iteration) {
    using P = Recv<int, End>;
    using Followup = Send<Transferable<int, CoordTag>, End>;

    SharedWire wire;
    OneShotFlag worker_dead;
    auto coord = mint_permissioned_session<P>(Channel{&wire, iteration});
    auto watched = mint_crash_watched_session<WorkerTag>(
        std::move(coord), worker_dead);

    std::jthread worker([&] { worker_dead.signal(); });
    worker.join();

    auto result = std::move(watched).recv(recv_int);
    assert(!result);
    assert(wire.delivered.load(std::memory_order_relaxed) == 0);

    using Error = typename decltype(result)::error_type;
    static_assert(std::is_same_v<Error, CrashEvent<WorkerTag, Channel, CoordTag>>);

    auto permissions = std::move(result.error().permissions);
    static_assert(std::is_same_v<decltype(permissions),
                                 std::tuple<Permission<CoordTag>>>);
    assert(result.error().resource.id == iteration);

    auto followup = mint_permissioned_session<Followup>(
        Channel{&wire, 60'000 + iteration}, std::move(std::get<0>(permissions)));
    auto after = std::move(followup).send(
        Transferable<int, CoordTag>{99, mint_permission_root<CoordTag>()},
        send_coord_transfer);
    auto recovered = std::move(after).close();
    assert(recovered.id == 60'000 + iteration);
    assert(wire.delivered.load(std::memory_order_relaxed) == 1);
}

void scenario_death_races_send(int iteration) {
    using P = Send<Transferable<int, CoordTag>, End>;

    SharedWire wire;
    OneShotFlag worker_dead;
    std::atomic<bool> start{false};

    auto initial_perm = mint_permission_root<CoordTag>();
    auto coord = mint_permissioned_session<P>(
        Channel{&wire, 10'000 + iteration}, std::move(initial_perm));
    auto watched = mint_crash_watched_session<WorkerTag>(
        std::move(coord), worker_dead);

    std::jthread killer(signal_after_start<OneShotFlag>,
                        std::ref(worker_dead),
                        std::ref(start));

    Transferable<int, CoordTag> payload{
        77,
        mint_permission_root<CoordTag>()};

    start.store(true, std::memory_order_release);
    auto result = std::move(watched).send(
        std::move(payload), send_coord_transfer);
    killer.join();

    if (result) {
        assert(wire.delivered.load(std::memory_order_relaxed) == 1);
        auto recovered = std::move(*result).close();
        assert(recovered.id == 10'000 + iteration);
    } else {
        assert(wire.delivered.load(std::memory_order_relaxed) == 0);
        auto permissions = std::move(result.error().permissions);
        static_assert(std::is_same_v<decltype(permissions),
                                     std::tuple<Permission<CoordTag>>>);
        assert(result.error().resource.id == 10'000 + iteration);
        (void)permissions;
    }
}

void scenario_concurrent_peer_deaths(int iteration) {
    using P = Send<int, End>;

    SharedWire worker_wire;
    SharedWire coord_wire;
    OneShotFlag worker_dead;
    OneShotFlag coord_dead;
    std::atomic<bool> start{false};

    auto master_worker_watch = mint_crash_watched_session<WorkerTag>(
        mint_permissioned_session<P>(Channel{&worker_wire, 20'000 + iteration}),
        worker_dead);
    auto master_coord_watch = mint_crash_watched_session<CoordTag>(
        mint_permissioned_session<P>(Channel{&coord_wire, 30'000 + iteration}),
        coord_dead);

    std::jthread worker_killer(signal_after_start<OneShotFlag>,
                               std::ref(worker_dead),
                               std::ref(start));
    std::jthread coord_killer(signal_after_start<OneShotFlag>,
                              std::ref(coord_dead),
                              std::ref(start));

    start.store(true, std::memory_order_release);
    worker_killer.join();
    coord_killer.join();

    auto worker_result = std::move(master_worker_watch).send(1, send_int);
    auto coord_result = std::move(master_coord_watch).send(2, send_int);

    assert(!worker_result);
    assert(!coord_result);
    assert(worker_wire.delivered.load(std::memory_order_relaxed) == 0);
    assert(coord_wire.delivered.load(std::memory_order_relaxed) == 0);

    auto worker_permissions = std::move(worker_result.error().permissions);
    auto coord_permissions = std::move(coord_result.error().permissions);
    static_assert(std::is_same_v<decltype(worker_permissions),
                                 std::tuple<Permission<CoordTag>>>);
    static_assert(std::is_same_v<decltype(coord_permissions),
                                 std::tuple<Permission<MasterTag>>>);
    static_assert(std::tuple_size_v<decltype(worker_permissions)> == 1);
    static_assert(std::tuple_size_v<decltype(coord_permissions)> == 1);
    assert(worker_result.error().resource.id == 20'000 + iteration);
    assert(coord_result.error().resource.id == 30'000 + iteration);
    (void)worker_permissions;
    (void)coord_permissions;
}

void scenario_fallback_survivor_observes(int iteration) {
    using P = Recv<int, End>;
    using Followup = Send<Transferable<int, MasterTag>, End>;

    SharedWire coord_wire;
    SharedWire master_wire;
    OneShotFlag worker_dead;

    auto coord = mint_crash_watched_session<WorkerFallbackTag>(
        mint_permissioned_session<End>(Channel{&coord_wire, 40'000 + iteration}),
        worker_dead);
    auto coord_channel = std::move(coord).close();
    assert(coord_channel.id == 40'000 + iteration);

    worker_dead.signal();

    auto master = mint_crash_watched_session<WorkerFallbackTag>(
        mint_permissioned_session<P>(Channel{&master_wire, 50'000 + iteration}),
        worker_dead);
    auto result = std::move(master).recv(recv_int);

    assert(!result);
    assert(master_wire.delivered.load(std::memory_order_relaxed) == 0);

    auto permissions = std::move(result.error().permissions);
    static_assert(std::is_same_v<decltype(permissions),
                                 std::tuple<Permission<MasterTag>>>);
    assert(result.error().resource.id == 50'000 + iteration);

    auto followup = mint_permissioned_session<Followup>(
        Channel{&master_wire, 70'000 + iteration},
        std::move(std::get<0>(permissions)));
    auto after = std::move(followup).send(
        Transferable<int, MasterTag>{101, mint_permission_root<MasterTag>()},
        [](Channel& channel, Transferable<int, MasterTag>&& value) noexcept {
            channel.wire->last_value.store(value.value, std::memory_order_relaxed);
            channel.wire->delivered.fetch_add(1, std::memory_order_relaxed);
        });
    auto recovered = std::move(after).close();
    assert(recovered.id == 70'000 + iteration);
    assert(master_wire.delivered.load(std::memory_order_relaxed) == 1);
}

void run_stress_iterations() {
    for (int i = 0; i < 50; ++i) {
        verify_three_permissioned_peers(i);
        scenario_clean_peer_death(i);
        scenario_death_races_send(i);
        scenario_concurrent_peer_deaths(i);
        scenario_fallback_survivor_observes(i);
    }
}

}  // namespace

int main() {
    run_stress_iterations();
    std::puts("crash_watched_psh_tsan: 50x clean death + send race + "
              "concurrent deaths + fallback survivor OK");
    return 0;
}
