#include <crucible/bridges/CrashTransport.h>
#include <crucible/sessions/SessionMint.h>

#include <atomic>
#include <cstdio>
#include <deque>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

namespace {

using namespace crucible::safety;
using namespace crucible::safety::proto;

struct ServerPeer {};
struct OtherPeer {};
struct ServerSurvivor {};
struct OtherSurvivor {};
struct WorkerPerm {};

struct Channel {
    std::deque<int>* wire = nullptr;
    int session_id = 0;
};

}  // namespace

namespace crucible::permissions {

template <>
struct survivor_registry<ServerPeer> {
    using type = inheritance_list<ServerSurvivor>;
};

template <>
struct survivor_registry<OtherPeer> {
    using type = inheritance_list<OtherSurvivor>;
};

}  // namespace crucible::permissions

namespace {

static_assert(!std::is_copy_constructible_v<CrashWatchedHandle<End, Channel, ServerPeer>>);
static_assert(std::is_move_constructible_v<CrashWatchedHandle<End, Channel, ServerPeer>>);
// The handle passes itself as the base's Derived argument.  That is what
// makes an abandonment diagnostic name this type instead of a bare
// session handle over the same protocol, which would be ambiguous.
static_assert(
    std::is_base_of_v<SessionHandleBase<End, CrashWatchedHandle<End, Channel, ServerPeer, CrashClass::Abort, void>>,
                      CrashWatchedHandle<End, Channel, ServerPeer>>);

static_assert(CrashWatchedHandle<End, Channel, ServerPeer>::crash_class == CrashClass::Abort);
static_assert(std::is_same_v<CrashWatchedHandle<Send<int, Stop_g<CrashClass::Throw>>, Channel, ServerPeer,
                                                CrashClass::Throw>::stop_type,
                             Stop_g<CrashClass::Throw>>);

static_assert(std::is_same_v<CrashEvent<ServerPeer, Channel>::peer, ServerPeer>);
static_assert(std::is_same_v<CrashEvent<ServerPeer, Channel>::resource_type, Channel>);
static_assert(std::is_same_v<proto::detail::crash_event_for_t<ServerPeer, Channel>,
                             CrashEvent<ServerPeer, Channel, ServerSurvivor>>);
static_assert(std::is_same_v<CrashEvent<ServerPeer, Channel, ServerSurvivor>::permissions_type,
                             std::tuple<Permission<ServerSurvivor>>>);

int run_happy_path() {
    using P = Send<int, Recv<int, End>>;

    std::deque<int> wire;
    OneShotFlag flag;  // never signalled
    Channel ch{&wire, 42};

    auto bare = mint_session_handle<P>(std::move(ch));
    auto watched = mint_crash_watched_session<ServerPeer>(std::move(bare), flag);

    auto r1 = std::move(watched).send(100, [](Channel& c, int v) noexcept { c.wire->push_back(v); });
    if (!r1) return 1;

    auto r2 = std::move(*r1).recv([](Channel& c) noexcept -> int {
        int x = c.wire->front();
        c.wire->pop_front();
        return x;
    });
    if (!r2) return 2;

    auto [received, h_end] = std::move(*r2);
    if (received != 100) return 3;

    auto recovered = std::move(h_end).close();
    if (recovered.session_id != 42) return 4;
    return 0;
}

int run_crash_before_send() {
    using P = Send<int, End>;

    std::deque<int> wire;
    OneShotFlag flag;
    Channel ch{&wire, 7};

    auto bare = mint_session_handle<P>(std::move(ch));
    auto watched = mint_crash_watched_session<ServerPeer>(std::move(bare), flag);

    flag.signal();

    auto r1 = std::move(watched).send(999, [](Channel& c, int v) noexcept { c.wire->push_back(v); });

    if (r1) return 1;
    auto& crash = r1.error();
    if (crash.resource.session_id != 7) return 2;
    // On the crash path the transport callback never runs at all, which
    // is why the wire has to be empty here.
    if (!wire.empty()) return 3;
    return 0;
}

// The crash fires before the payload's permission is consumed, so the
// event hands back survivor permissions rather than losing the
// permission with the dead session.
int run_permissioned_crash_before_transferable_send() {
    using P = Send<Transferable<int, WorkerPerm>, End>;

    std::deque<int> wire;
    OneShotFlag flag;
    Channel ch{&wire, 17};
    bool transport_invoked = false;

    auto initial_perm = mint_permission_root<WorkerPerm>();
    auto psh = mint_permissioned_session<P>(::crucible::effects::HotFgCtx{}, std::move(ch), std::move(initial_perm));
    static_assert(std::is_same_v<typename decltype(psh)::perm_set, PermSet<WorkerPerm>>);

    auto watched = mint_crash_watched_session<ServerPeer>(std::move(psh), flag);
    static_assert(std::is_same_v<typename decltype(watched)::perm_set, PermSet<WorkerPerm>>);

    flag.signal();

    Transferable<int, WorkerPerm> payload{123, mint_permission_root<WorkerPerm>()};

    auto r = std::move(watched).send(std::move(payload), [&](Channel& c, Transferable<int, WorkerPerm>&& t) noexcept {
        transport_invoked = true;
        c.wire->push_back(t.value);
    });

    using Result = decltype(r);
    static_assert(std::is_same_v<typename Result::error_type, CrashEvent<ServerPeer, Channel, ServerSurvivor>>);

    if (r) return 1;
    auto& crash = r.error();
    if (crash.resource.session_id != 17) return 2;
    if (transport_invoked) return 3;
    if (!wire.empty()) return 4;

    auto survivor_permissions = std::move(crash.permissions);
    static_assert(std::is_same_v<decltype(survivor_permissions), std::tuple<Permission<ServerSurvivor>>>);
    (void)survivor_permissions;
    return 0;
}

int run_throw_grade_stop_terminal() {
    using P = Send<int, Stop_g<CrashClass::Throw>>;

    std::deque<int> wire;
    OneShotFlag flag;
    Channel ch{&wire, 23};

    auto bare = mint_session_handle<P>(std::move(ch));
    auto watched = mint_crash_watched_session<ServerPeer, CrashClass::Throw>(std::move(bare), flag);

    static_assert(decltype(watched)::crash_class == CrashClass::Throw);

    auto r = std::move(watched).send(321, [](Channel& c, int v) noexcept { c.wire->push_back(v); });
    if (!r) return 1;
    if (wire.size() != 1 || wire.front() != 321) return 2;

    static_assert(std::remove_cvref_t<decltype(*r)>::crash_class == CrashClass::Throw);
    static_assert(std::is_same_v<typename std::remove_cvref_t<decltype(*r)>::protocol, Stop_g<CrashClass::Throw>>);

    auto recovered = std::move(*r).close();
    if (recovered.session_id != 23) return 3;
    return 0;
}

int run_error_return_grade_crash_before_recv() {
    using P = Recv<int, End>;

    std::deque<int> wire;
    OneShotFlag flag;
    Channel ch{&wire, 31};

    auto bare = mint_session_handle<P>(std::move(ch));
    auto watched = mint_crash_watched_session<ServerPeer, CrashClass::ErrorReturn>(std::move(bare), flag);

    static_assert(decltype(watched)::crash_class == CrashClass::ErrorReturn);

    flag.signal();

    auto r = std::move(watched).recv([](Channel& c) noexcept -> int {
        int x = c.wire->front();
        c.wire->pop_front();
        return x;
    });

    if (r) return 1;
    if (r.error().resource.session_id != 31) return 2;
    return 0;
}

int run_crash_mid_protocol() {
    using P = Send<int, Recv<int, End>>;

    std::deque<int> wire;
    OneShotFlag flag;
    Channel ch{&wire, 13};

    auto bare = mint_session_handle<P>(std::move(ch));
    auto watched = mint_crash_watched_session<ServerPeer>(std::move(bare), flag);

    auto r1 = std::move(watched).send(5, [](Channel& c, int v) noexcept { c.wire->push_back(v); });
    if (!r1) return 1;
    if (wire.size() != 1 || wire.front() != 5) return 2;

    flag.signal();

    auto r2 = std::move(*r1).recv([](Channel& c) noexcept -> int { return c.wire->back(); });
    if (r2) return 3;
    if (r2.error().resource.session_id != 13) return 4;
    return 0;
}

// The flag pairs a release on signal with an acquire on every per-op
// check.  Anything the producer wrote before signalling is therefore
// visible to the consumer once it observes the crash.
int run_cross_thread_crash() {
    using P = Loop<Select<Send<int, Continue>, End>>;

    std::deque<int> wire;
    OneShotFlag flag;
    Channel ch{&wire, 99};

    auto bare = mint_session_handle<P>(std::move(ch));
    auto watched = mint_crash_watched_session<ServerPeer>(std::move(bare), flag);

    std::atomic<bool> ready{false};
    std::jthread producer([&] {
        while (!ready.load(std::memory_order_acquire)) {
            CRUCIBLE_SPIN_PAUSE;
        }
        flag.signal();
    });

    ready.store(true, std::memory_order_release);

    // The spin is bounded so that a bug which never delivers the signal
    // fails the test instead of hanging it.
    for (int i = 0; i < 10000000; ++i) {
        auto r = std::move(watched).template select_local<0>();
        if (!r) {
            if (r.error().resource.session_id != 99) return 1;
            producer.join();
            return 0;
        }
        auto r2 = std::move(*r).send(i, [](Channel& c, int v) noexcept { c.wire->push_back(v); });
        if (!r2) {
            if (r2.error().resource.session_id != 99) return 2;
            producer.join();
            return 0;
        }
        // After the send the handle sits back at the Select, because
        // Continue resolves to the head of the Loop.
        watched = std::move(*r2);
    }

    producer.join();
    return 99;  // the consumer never observed the signal
}

int run_multi_peer_independent() {
    using P = Send<int, End>;

    std::deque<int> wire_a;
    std::deque<int> wire_b;
    OneShotFlag flag_server;
    OneShotFlag flag_other;

    Channel ch_a{&wire_a, 1};
    Channel ch_b{&wire_b, 2};

    auto bare_a = mint_session_handle<P>(std::move(ch_a));
    auto watched_a = mint_crash_watched_session<ServerPeer>(std::move(bare_a), flag_server);

    auto bare_b = mint_session_handle<P>(std::move(ch_b));
    auto watched_b = mint_crash_watched_session<OtherPeer>(std::move(bare_b), flag_other);

    flag_server.signal();

    auto r_a = std::move(watched_a).send(10, [](Channel& c, int v) noexcept { c.wire->push_back(v); });
    auto r_b = std::move(watched_b).send(20, [](Channel& c, int v) noexcept { c.wire->push_back(v); });

    if (r_a) return 1;
    if (!r_b) return 2;
    if (r_a.error().resource.session_id != 1) return 3;
    if (wire_b.size() != 1 || wire_b.front() != 20) return 4;

    using ErrorA = typename decltype(r_a)::error_type;
    using ErrorB = typename decltype(r_b)::error_type;
    static_assert(std::is_same_v<ErrorA::peer, ServerPeer>);
    static_assert(std::is_same_v<ErrorB::peer, OtherPeer>);

    auto recovered_b = std::move(*r_b).close();
    if (recovered_b.session_id != 2) return 5;
    return 0;
}

// This one is an end-to-end usage shape rather than a check of a single
// behaviour: the peer dies between request and reply, and the caller
// retries from the resource the crash event hands back.
int run_worked_example_cntp_pattern() {
    using RequestReply = Send<int, Recv<int, End>>;

    int resolved_result = 0;
    int attempts = 0;

    for (int retry = 0; retry < 3; ++retry) {
        std::deque<int> wire;
        OneShotFlag flag;
        Channel ch{&wire, 100 + retry};
        ++attempts;

        auto bare = mint_session_handle<RequestReply>(std::move(ch));
        auto watched = mint_crash_watched_session<ServerPeer>(std::move(bare), flag);

        auto r1 = std::move(watched).send(7, [](Channel& c, int v) noexcept { c.wire->push_back(v * 2); });
        if (!r1) continue;  // the flag starts clean, so this is unreachable

        if (retry == 0) flag.signal();

        auto r2 = std::move(*r1).recv([](Channel& c) noexcept -> int {
            int x = c.wire->front();
            c.wire->pop_front();
            return x;
        });

        if (!r2) {
            // A real caller would re-establish from crash.resource.  The
            // retry here just builds a fresh channel instead.
            auto& crash = r2.error();
            (void)crash;
            continue;
        }

        auto [received, h_end] = std::move(*r2);
        (void)std::move(h_end).close();
        resolved_result = received;
        break;
    }

    if (resolved_result != 14) return 1;  // the transport doubles the 7
    if (attempts != 2) return 2;  // first attempt crashes, second succeeds
    return 0;
}

}  // anonymous namespace

int main() {
    if (int rc = run_happy_path(); rc != 0) return rc;
    if (int rc = run_crash_before_send(); rc != 0) return 100 + rc;
    if (int rc = run_permissioned_crash_before_transferable_send(); rc != 0) return 150 + rc;
    if (int rc = run_throw_grade_stop_terminal(); rc != 0) return 175 + rc;
    if (int rc = run_error_return_grade_crash_before_recv(); rc != 0) return 185 + rc;
    if (int rc = run_crash_mid_protocol(); rc != 0) return 200 + rc;
    if (int rc = run_cross_thread_crash(); rc != 0) return 300 + rc;
    if (int rc = run_multi_peer_independent(); rc != 0) return 400 + rc;
    if (int rc = run_worked_example_cntp_pattern(); rc != 0) return 500 + rc;

    std::puts("crash_transport: happy + crash-before + permissioned-crash + "
              "graded-stop + error-return + crash-mid + cross-thread + "
              "multi-peer + CNTP-pattern OK");
    return 0;
}
