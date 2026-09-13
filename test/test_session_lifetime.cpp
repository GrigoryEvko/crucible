// The destructor aborts on an abandoned handle only in a debug build,
// and no death-test framework is available here.  Every case below
// therefore drives a path that must NOT abort.

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCheckpoint.h>
#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionDelegate.h>
#include <crucible/sessions/SessionMint.h>

#include <cstdio>
#include <deque>
#include <string>
#include <utility>

namespace {

using namespace crucible::safety::proto;

static_assert(is_terminal_state_v<End>);
static_assert(is_terminal_state_v<Stop>);
static_assert(!is_terminal_state_v<Send<int, End>>);
static_assert(!is_terminal_state_v<Recv<int, End>>);
static_assert(!is_terminal_state_v<Select<Send<int, End>>>);
static_assert(!is_terminal_state_v<Offer<Recv<int, End>>>);
static_assert(!is_terminal_state_v<Loop<Send<int, Continue>>>);
static_assert(!is_terminal_state_v<Delegate<Send<int, End>, End>>);
static_assert(!is_terminal_state_v<Accept<Send<int, End>, End>>);

// Terminality of a checkpointed session is the conjunction over its two
// branches.  When both are terminal the handle has no work left whichever
// arm is taken, so abandoning it is safe.
static_assert(is_terminal_state_v<CheckpointedSession<End, End>>);
static_assert(is_terminal_state_v<CheckpointedSession<Stop, End>>);
static_assert(is_terminal_state_v<CheckpointedSession<End, Stop>>);
static_assert(!is_terminal_state_v<CheckpointedSession<Send<int, End>, End>>);
static_assert(!is_terminal_state_v<CheckpointedSession<End, Recv<int, End>>>);
static_assert(!is_terminal_state_v<CheckpointedSession<Send<int, End>, Recv<int, End>>>);

struct FakeRes {};

// The base takes a Derived parameter naming the wrapper class, and every
// specialization passes itself.  That lets the abandonment diagnostic
// spell the wrapper even when several wrappers share one protocol, and it
// is why the base appears here spelled with its third argument.
static_assert(
    std::is_base_of_v<SessionHandleBase<End, SessionHandle<End, FakeRes, void>>, SessionHandle<End, FakeRes>>);
static_assert(std::is_base_of_v<SessionHandleBase<Send<int, End>, SessionHandle<Send<int, End>, FakeRes, void>>,
                                SessionHandle<Send<int, End>, FakeRes>>);
static_assert(
    std::is_base_of_v<SessionHandleBase<Stop, SessionHandle<Stop, FakeRes, void>>, SessionHandle<Stop, FakeRes>>);
static_assert(std::is_base_of_v<SessionHandleBase<Delegate<Send<int, End>, End>,
                                                  SessionHandle<Delegate<Send<int, End>, End>, FakeRes, void>>,
                                SessionHandle<Delegate<Send<int, End>, End>, FakeRes>>);
static_assert(std::is_base_of_v<SessionHandleBase<CheckpointedSession<End, End>,
                                                  SessionHandle<CheckpointedSession<End, End>, FakeRes, void>>,
                                SessionHandle<CheckpointedSession<End, End>, FakeRes>>);

int run_consume_via_close() {
    auto h = mint_session_handle<End>(FakeRes{});
    auto r = std::move(h).close();
    (void)r;
    return 0;
}

struct Wire {
    std::deque<std::string>* bytes = nullptr;
};

struct Ping {
    int n;
};
struct Pong {
    int n;
};

auto send_ping = [](Wire& w, Ping&& p) noexcept { w.bytes->push_back("PING:" + std::to_string(p.n)); };
auto recv_ping = [](Wire& w) noexcept -> Ping {
    std::string s = std::move(w.bytes->front());
    w.bytes->pop_front();
    return Ping{std::atoi(s.data() + 5)};
};
auto send_pong = [](Wire& w, Pong&& p) noexcept { w.bytes->push_back("PONG:" + std::to_string(p.n)); };
auto recv_pong = [](Wire& w) noexcept -> Pong {
    std::string s = std::move(w.bytes->front());
    w.bytes->pop_front();
    return Pong{std::atoi(s.data() + 5)};
};

using PingPongClient = Send<Ping, Recv<Pong, End>>;

int run_consume_via_chain() {
    std::deque<std::string> wire;
    Wire a{&wire};
    Wire b{&wire};
    crucible::effects::HotFgCtx ctx{};

    auto [client, server] = mint_channel<PingPongClient>(ctx, ctx, std::move(a), std::move(b));

    auto c1 = std::move(client).send(Ping{42}, send_ping);
    auto [got_ping, s1] = std::move(server).recv(recv_ping);
    if (got_ping.n != 42) return 1;

    auto s2 = std::move(s1).send(Pong{42}, send_pong);
    auto [got_pong, c2] = std::move(c1).recv(recv_pong);
    if (got_pong.n != 42) return 2;

    (void)std::move(c2).close();
    (void)std::move(s2).close();
    return 0;
}

using InfiniteProducer = Loop<Send<Ping, Continue>>;

int run_detach_infinite_loop() {
    std::deque<std::string> wire;
    Wire res{&wire};

    auto h = mint_session_handle<InfiniteProducer>(std::move(res));
    // The protocol carries no close branch, so detach is the only way
    // to leave it.
    auto h2 = std::move(h).send(Ping{99}, send_ping);
    std::move(h2).detach(detach_reason::TestInstrumentation{});
    return 0;
}

int run_stop_terminal() {
    auto h = mint_session_handle<Stop>(FakeRes{});
    // Stop is terminal, so the destructor check skips it.  close() is
    // available for symmetry with End but is not required.
    auto r = std::move(h).close();
    (void)r;
    return 0;
}

// A moved-from handle still runs its destructor at end of scope.  The
// base's move constructor marks the source consumed, so the abandonment
// check skips it.

int run_moved_from_safe() {
    std::deque<std::string> wire;
    Wire res{&wire};

    auto h = mint_session_handle<PingPongClient>(std::move(res));
    auto h2 = std::move(h).send(Ping{1}, send_ping);
    auto [_p, h3] = std::move(h2).recv(recv_pong);
    (void)_p;
    (void)std::move(h3).close();

    // These two land after the receive on purpose.  The case covers
    // lifetime mechanics, not the wire protocol.
    wire.push_back("PING:1");
    wire.push_back("PONG:1");
    return 0;
}

struct MinimalRes {
    int x;
};

// A debug build gives the tracker a one-byte flag to drive the
// abandonment check, which the resource's alignment then pads out.  A
// release build makes the tracker empty, the base empty, and the handle
// collapses onto the resource.
#ifdef NDEBUG
static_assert(sizeof(SessionHandle<End, MinimalRes>) == sizeof(MinimalRes),
              "Release-mode: SessionHandle<End, R> must equal sizeof(R) (zero-cost).");
#else
static_assert(sizeof(SessionHandle<End, MinimalRes>) > sizeof(MinimalRes),
              "Debug-mode: SessionHandle carries the consumed_tracker flag (+ padding).");
#endif

// The rendering falls out of __PRETTY_FUNCTION__, so its exact spelling
// belongs to the compiler.  These cases match substrings, which survive a
// compiler that tweaks whitespace or introduces an alias.

int run_protocol_name_smoke() {
    constexpr auto end_name = SessionHandle<End, FakeRes>::protocol_name();
    if (end_name.find("End") == std::string_view::npos) return 1;

    using SI = Send<int, End>;
    constexpr auto si_name = SessionHandle<SI, FakeRes>::protocol_name();
    if (si_name.find("Send") == std::string_view::npos) return 10;
    if (si_name.find("int") == std::string_view::npos) return 11;
    if (si_name.find("End") == std::string_view::npos) return 12;

    // A looped protocol unrolls at mint time, so the handle sits at the
    // body with the loop carried as context.  The rendering shows the
    // body shape only, which is why no case looks for the loop itself.
    using LSP_Body = Send<Ping, Continue>;
    using LSP_Ctx = Loop<LSP_Body>;
    constexpr auto lsp_name = SessionHandle<LSP_Body, FakeRes, LSP_Ctx>::protocol_name();
    if (lsp_name.find("Send") == std::string_view::npos) return 21;
    if (lsp_name.find("Ping") == std::string_view::npos) return 22;
    if (lsp_name.find("Continue") == std::string_view::npos) return 23;

    constexpr auto stop_name = SessionHandle<Stop, FakeRes>::protocol_name();
    if (stop_name.find("Stop") == std::string_view::npos) return 30;

    // The rendering points into program-lifetime data, so repeated calls
    // return the same address.
    if (end_name.data() != SessionHandle<End, FakeRes>::protocol_name().data()) return 40;
    if (lsp_name.data() != SessionHandle<LSP_Body, FakeRes, LSP_Ctx>::protocol_name().data()) return 41;

    if (end_name == si_name) return 50;
    if (si_name == lsp_name) return 51;
    return 0;
}

static_assert(SessionHandle<End, FakeRes>::protocol_name().size() > 0,
              "protocol_name() must yield a non-empty rendering at compile time.");

// The accessor is inherited, so it must hold on every specialization.
static_assert(SessionHandle<Send<int, End>, FakeRes>::protocol_name().size() > 0);
static_assert(SessionHandle<Recv<int, End>, FakeRes>::protocol_name().size() > 0);
static_assert(SessionHandle<Stop, FakeRes>::protocol_name().size() > 0);
static_assert(SessionHandle<Delegate<Send<int, End>, End>, FakeRes>::protocol_name().size() > 0);
static_assert(SessionHandle<Accept<Send<int, End>, End>, FakeRes>::protocol_name().size() > 0);
static_assert(SessionHandle<CheckpointedSession<End, End>, FakeRes>::protocol_name().size() > 0);

// Self-move-assignment is reachable through aliasing or a chain of
// moves.  The standard leaves the object valid but unspecified.  This
// contract is stronger: the tracker flag must come through unchanged, or
// an unconsumed handle is silently marked consumed and the destructor
// stops reporting a genuine leak.
//
// The tracker primitive is driven directly.  In a release build it is
// empty and the case passes trivially.  In a debug build it exercises the
// flag-preservation invariant.

int run_self_move_preserves_tracker_state() {
    detail::consumed_tracker fresh;
#ifndef NDEBUG
    if (fresh.was_marked()) return 1;
#endif
    fresh.move_from(fresh);
#ifndef NDEBUG
    if (fresh.was_marked()) return 2;
#endif

    // The flag is monotonic, so consumed stays consumed.
    detail::consumed_tracker stale;
    stale.mark();
#ifndef NDEBUG
    if (!stale.was_marked()) return 3;
#endif
    stale.move_from(stale);
#ifndef NDEBUG
    if (!stale.was_marked()) return 4;
#endif

    // The tracker is private, so this case cannot inspect it through
    // the assignment operator.  It shows only that the operator compiles,
    // survives the self-assignment short-circuit, and leaves the handle
    // usable for one more operation.  The primitive cases above carry
    // the actual claim.
    {
        auto h = mint_session_handle<Send<int, End>>(FakeRes{});
        auto& alias = h;
        h = std::move(alias);
        std::move(h).detach(detach_reason::TestInstrumentation{});
    }

    return 0;
}

// A transactional handle whose base and rollback branches are both
// closed leaves the peer no remaining work, so dropping it must not fire
// the abandonment check.  A handle whose base branch still carries a Send
// stays non-terminal above, and abandoning that one does abort.

int run_terminal_twin_checkpoint_drops_clean() {
    {
        auto h = mint_session_handle<CheckpointedSession<End, End>>(FakeRes{});
        (void)h;
    }

    {
        auto h = mint_session_handle<CheckpointedSession<Stop, Stop>>(FakeRes{});
        (void)h;
    }

    // A mixed pair is still terminal in both arms.
    {
        auto h = mint_session_handle<CheckpointedSession<End, Stop>>(FakeRes{});
        (void)h;
    }
    {
        auto h = mint_session_handle<CheckpointedSession<Stop, End>>(FakeRes{});
        (void)h;
    }

    return 0;
}

}  // anonymous namespace

int main() {
    if (int rc = run_consume_via_close(); rc != 0) return rc;
    if (int rc = run_consume_via_chain(); rc != 0) return rc;
    if (int rc = run_detach_infinite_loop(); rc != 0) return rc;
    if (int rc = run_stop_terminal(); rc != 0) return rc;
    if (int rc = run_moved_from_safe(); rc != 0) return rc;
    if (int rc = run_protocol_name_smoke(); rc != 0) return rc;
    if (int rc = run_self_move_preserves_tracker_state(); rc != 0) return rc;
    if (int rc = run_terminal_twin_checkpoint_drops_clean(); rc != 0) return rc;
    std::puts(
        "session_lifetime: close + chain + detach + Stop + moved-from + protocol_name + self_move + ckpt-terminal-drop OK");
    return 0;
}
