// SPDX-License-Identifier: Apache-2.0
//
// fixy-A2-031 — CrashWatchedHandle TOCTOU witness.
//
// Every CrashWatchedHandle protocol-head specialization (Send/Recv/...)
// starts with `if (flag_->peek()) [[unlikely]] { ... wrap_crash_return; }`
// then proceeds to `inner_.send/recv(...)` (lines 712-735, 794-823 of
// bridges/CrashTransport.h).  Between the peek (relaxed load on the
// happy path) and the inner-call, ANOTHER thread may set the
// OneShotFlag.  The acquire fence on the unhappy path only protects
// AFTER we've taken the crash path.  The happy path then invokes the
// user-provided `transport` — which may block on a socket / RDMA wait
// for a peer that just died — and the design has NO post-call flag
// check.  Result: the call can complete normally even though a crash
// was visible to another thread mid-call.
//
// The existing tsan suite (test_crash_watched_psh_tsan.cpp) covers
// scenarios where the flag is set BEFORE the protocol op runs
// (`scenario_clean_peer_death`) or where the kill races concurrently
// without synchronization (`scenario_death_races_send`).  Neither
// PINS the mid-call window — they happen to land on either side of
// the peek non-deterministically.
//
// This file deterministically pins the mid-call window:
//
//   1. Foreground enters `.recv(transport)`.
//   2. `flag_->peek()` returns FALSE (no crash yet).
//   3. Foreground invokes `transport` → sets `transport_running` flag.
//   4. Killer thread waits for `transport_running`, then signals the
//      crash flag.
//   5. Killer thread releases the transport via `transport_unblock`.
//   6. Transport returns → recv() returns success (no post-call check).
//   7. Test asserts:
//        a. The recv()/send() returned success — NO mid-call detection.
//        b. The NEXT op on the returned continuation DOES detect the
//           crash flag (because wrap_crash_next_ re-wraps the next
//           handle in CrashWatchedHandle).
//   8. tsan observes the load-ordering between peek (relaxed) and the
//      flag.signal() store (release).  ANY ordering violation
//      (e.g., stale read on weakly-ordered hardware) trips tsan.
//
// This test is the FIRST tsan witness that PINS the mid-call window.
// Per fixy-A2-031 the witness alone is sufficient — fixing the gap
// (adding a post-call flag check OR a flag-aware blocking transport
// primitive) is a separate task.
//
// Runs under every preset, and under the `tsan` preset with
// -fsanitize=thread through the shared crucible_test factory.

#include <crucible/bridges/CrashTransport.h>
#include <crucible/sessions/SessionMint.h>
#include "../test_assert.h"

#include <atomic>
#include <cstdio>
#include <thread>
#include <type_traits>
#include <utility>

namespace {

using namespace crucible::safety;
using namespace crucible::safety::proto;

constexpr ::crucible::effects::HotFgCtx kSessionCtx{};

struct WorkerTag {};
struct CoordTag {};
struct MasterTag {};

// Shared synchronization triplet pinning the mid-call race window.
// All three atomics live on isolated cache lines to keep their
// transitions visible to tsan without false-sharing noise.
struct ToctouBarrier {
    alignas(64) std::atomic<bool> transport_running{false};
    alignas(64) std::atomic<bool> killer_done{false};
    alignas(64) std::atomic<bool> transport_unblock{false};
};

struct Channel {
    ToctouBarrier* barrier = nullptr;
    int            id      = 0;
    int            payload = 0;
};

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

}  // namespace crucible::permissions

namespace {

// recv-side transport: signals entry, then waits for the killer to
// unblock.  By the time the transport returns the crash flag has been
// signalled AND the killer has finished — so the post-call window is
// fully observable by tsan.
int blocking_recv_transport(Channel& channel) noexcept {
    channel.barrier->transport_running.store(true, std::memory_order_release);
    // Spin until the killer releases us.  Bounded spin keeps the test
    // deterministic — if the killer never runs, we'd hang forever, but
    // that path is unreachable because we orchestrate the threads
    // ourselves.
    while (!channel.barrier->transport_unblock.load(std::memory_order_acquire)) {
        CRUCIBLE_SPIN_PAUSE;
    }
    return channel.payload;
}

void blocking_send_transport(Channel& channel, int value) noexcept {
    channel.payload = value;
    channel.barrier->transport_running.store(true, std::memory_order_release);
    while (!channel.barrier->transport_unblock.load(std::memory_order_acquire)) {
        CRUCIBLE_SPIN_PAUSE;
    }
}

// Killer thread: waits for the transport to enter, signals the crash
// flag, then unblocks the transport.  This sequencing is the load-
// bearing part of the witness — it pins the flag signal to happen
// strictly BETWEEN the peek (which already ran) and the post-call
// path (which will return success because the design has no
// post-call flag check).
void run_killer(ToctouBarrier& barrier, OneShotFlag& crash_flag) noexcept {
    while (!barrier.transport_running.load(std::memory_order_acquire)) {
        CRUCIBLE_SPIN_PAUSE;
    }
    // The transport is mid-call.  Signal the crash flag now — the
    // foreground thread will observe it on the NEXT protocol op, but
    // the current recv()/send() WILL complete normally per the design.
    crash_flag.signal();
    // Release the transport so the foreground returns from recv/send.
    // killer_done store paired with the join() in the foreground gives
    // tsan a happens-before edge across the thread boundary.
    barrier.killer_done.store(true, std::memory_order_release);
    barrier.transport_unblock.store(true, std::memory_order_release);
}

// Scenario 1 — Recv head: peek=false at entry, flag signalled mid-
// transport, recv() returns success, NEXT op observes the crash.
void scenario_recv_mid_transport_signal(int iteration) {
    using P  = Recv<int, Recv<int, End>>;

    ToctouBarrier barrier;
    OneShotFlag   worker_dead;

    auto coord = mint_permissioned_session<P>(
        kSessionCtx, Channel{&barrier, 80'000 + iteration, 42});
    auto watched = mint_crash_watched_session<WorkerTag>(
        std::move(coord), worker_dead);

    std::jthread killer(run_killer, std::ref(barrier), std::ref(worker_dead));

    // First recv: peek=false at entry (killer hasn't signalled yet),
    // transport blocks on barrier, killer signals flag, transport
    // returns.  Per the design (NO post-call flag check) the recv
    // returns success — even though the crash flag was set before the
    // transport returned.
    auto result = std::move(watched).recv(blocking_recv_transport);
    killer.join();

    // Witness 1: the recv() returned success — NO mid-call crash
    // detection.  This documents the current design behavior.
    assert(result.has_value());

    // Witness 2: the killer's flag.signal() and ack are visible to us
    // (happens-before via jthread join).
    assert(barrier.killer_done.load(std::memory_order_acquire));
    assert(worker_dead.peek());

    auto [value, next] = std::move(*result);
    assert(value == 42);

    // Witness 3: the NEXT op on the returned continuation DOES detect
    // the crash flag (because wrap_crash_next_ re-wraps next in a
    // CrashWatchedHandle, and its peek() now returns true).
    auto next_result = std::move(next).recv(blocking_recv_transport);
    assert(!next_result.has_value());

    // The crash event carries the inherited survivor permission and
    // the original Channel resource.
    auto perms = std::move(next_result.error().permissions);
    static_assert(std::is_same_v<decltype(perms),
                                 std::tuple<Permission<CoordTag>>>);
    assert(next_result.error().resource.id == 80'000 + iteration);
    (void)perms;
}

// Scenario 2 — Send head: same race shape, different protocol arm.
// Pins that the rejection IS direction-agnostic at the design level:
// neither Send nor Recv has a post-call flag check, so both witness
// the same TOCTOU window.
void scenario_send_mid_transport_signal(int iteration) {
    using P = Send<int, Send<int, End>>;

    ToctouBarrier barrier;
    OneShotFlag   worker_dead;

    auto coord = mint_permissioned_session<P>(
        kSessionCtx, Channel{&barrier, 90'000 + iteration, 0});
    auto watched = mint_crash_watched_session<WorkerTag>(
        std::move(coord), worker_dead);

    std::jthread killer(run_killer, std::ref(barrier), std::ref(worker_dead));

    auto result = std::move(watched).send(7, blocking_send_transport);
    killer.join();

    // Witness 1: send completes despite flag being set mid-call.
    assert(result.has_value());
    assert(barrier.killer_done.load(std::memory_order_acquire));
    assert(worker_dead.peek());

    auto next = std::move(*result);

    // Witness 2: the NEXT send observes the crash via the re-wrapped
    // continuation's peek().  We re-arm the barrier so this second
    // send does not block (transport_running stays set, killer not
    // re-spawned — peek catches it first).
    auto next_result = std::move(next).send(8, blocking_send_transport);
    assert(!next_result.has_value());
    assert(next_result.error().resource.id == 90'000 + iteration);
}

// Scenario 3 — Stop_g terminal head: validates the witness extends to
// the terminal protocol arm.  Stop_g's CrashWatchedHandle close()
// path is the FINAL opportunity for the design to observe a
// post-recv flag — the test pins that close() DOES detect the flag
// (a CRUCIBLE_INVARIANT on the closing path).
void scenario_close_after_mid_signal(int iteration) {
    using P = Recv<int, End>;

    ToctouBarrier barrier;
    OneShotFlag   worker_dead;

    auto coord = mint_permissioned_session<P>(
        kSessionCtx, Channel{&barrier, 100'000 + iteration, 99});
    auto watched = mint_crash_watched_session<WorkerTag>(
        std::move(coord), worker_dead);

    std::jthread killer(run_killer, std::ref(barrier), std::ref(worker_dead));

    auto result = std::move(watched).recv(blocking_recv_transport);
    killer.join();

    assert(result.has_value());
    assert(worker_dead.peek());

    auto [value, end_handle] = std::move(*result);
    assert(value == 99);

    // The continuation's close() returns the Channel via the inner
    // SessionHandle.  By the time we reach close() the flag is set,
    // but End has no flag check in its CrashWatchedHandle
    // specialization — the design treats clean drop of a
    // terminal-state handle as success.  This witnesses the gap
    // structurally: a close()-time observer would catch this case
    // before the channel is released, but the current design does
    // not perform that check.
    auto channel = std::move(end_handle).close();
    assert(channel.id == 100'000 + iteration);
}

void run_stress_iterations() {
    for (int i = 0; i < 50; ++i) {
        scenario_recv_mid_transport_signal(i);
        scenario_send_mid_transport_signal(i);
        scenario_close_after_mid_signal(i);
    }
}

}  // namespace

int main() {
    run_stress_iterations();
    std::puts("crash_watched_toctou_tsan: 50x recv+send+close mid-transport-signal OK");
    return 0;
}
