// Runtime + compile-time harness for safety/MachineSessionBridge.h
// (task #399, SAFEINT-A10 from misc/24_04_2026_safety_integration.md
// §10).
//
// Coverage:
//   * Compile-time: bridge is Pinned (deleted copy/move); typedefs
//     wire correctly; session_handle_type matches the Loop-unrolled
//     SessionHandle specialisation; static protocol_name() returns
//     a non-empty rendering.
//   * Runtime: bridge constructs from State, in-place args, or
//     Machine; .machine() / .state() / .state_mut() expose the
//     imperative view; .session_view() mints a fresh handle each
//     call; mid-protocol handles consumed via .send() recover the
//     Machine via machine_from_session(...&&) without firing the
//     abandonment-check; .extract() yields the underlying State.
//   * Worked example: a Vigil-style mode state machine driven via
//     both views — internal mutation through .machine() and
//     external observation through .session_view().
//
// Closes #399; advances #424 (Epoch I umbrella).  Production
// refactors of Vigil (#411) and Transaction (#412) will adopt this
// bridge; this header SHIPS THE INFRASTRUCTURE only — no production
// code changes here.

#include <crucible/safety/MachineSessionBridge.h>

#include <cstdint>
#include <cstdio>
#include <utility>

namespace {

using namespace crucible::safety;

// ── Fixture: a Vigil-style mode state machine ──────────────────────

struct VigilState {
    enum class Mode : uint8_t { Idle, Recording, Replaying, Serving };
    Mode     mode  = Mode::Idle;
    uint32_t ticks = 0;
};

// A single-party protocol — every transition is a "send tick" in a
// Loop with an explicit close branch.  Real Vigil shapes refine
// this; the bridge accepts any well-formed Proto.
using VigilProto = proto::Loop<
    proto::Select<
        proto::Send<int, proto::Continue>,
        proto::End>>;

using VigilBridge = SessionFromMachine<VigilState, VigilProto>;

// ── Compile-time witnesses (mirrored from header self-test for TU
//    safety — a regression in the bridge surface fails this TU
//    visibly rather than only at the framework's internal self-test) ─

static_assert(!std::is_copy_constructible_v<VigilBridge>);
static_assert(!std::is_move_constructible_v<VigilBridge>);
static_assert(std::is_base_of_v<Pinned<VigilBridge>, VigilBridge>);

// Bridge is zero-cost in release: same size as the underlying State.
#ifdef NDEBUG
static_assert(sizeof(VigilBridge) == sizeof(VigilState),
    "Release-mode bridge must add zero bytes beyond State.");
#endif

// ── Test: construct from a State value ─────────────────────────────

int run_construct_from_state() {
    VigilBridge bridge{VigilState{VigilState::Mode::Recording, 7}};

    if (bridge.state().mode  != VigilState::Mode::Recording) return 1;
    if (bridge.state().ticks != 7)                            return 2;
    return 0;
}

// ── Test: construct in-place via std::in_place_t ──────────────────

int run_construct_in_place() {
    VigilBridge bridge{std::in_place, VigilState::Mode::Serving, 42u};

    if (bridge.state().mode  != VigilState::Mode::Serving) return 1;
    if (bridge.state().ticks != 42)                         return 2;
    return 0;
}

// ── Test: construct from a pre-built Machine ──────────────────────

int run_construct_from_machine() {
    auto m = make_machine<VigilState>(VigilState::Mode::Replaying, 100u);
    VigilBridge bridge{std::move(m)};

    if (bridge.state().mode  != VigilState::Mode::Replaying) return 1;
    if (bridge.state().ticks != 100)                          return 2;
    return 0;
}

// ── Test: imperative view (Machine) mutates through the bridge ────

int run_imperative_mutation_via_machine() {
    VigilBridge bridge{VigilState{}};

    // Initial state via .state()
    if (bridge.state().mode  != VigilState::Mode::Idle) return 1;
    if (bridge.state().ticks != 0)                       return 2;

    // Mutate via .state_mut() (sugar over .machine().data_mut()).
    bridge.state_mut().mode  = VigilState::Mode::Recording;
    bridge.state_mut().ticks = 1;
    if (bridge.state().mode  != VigilState::Mode::Recording) return 3;
    if (bridge.state().ticks != 1)                            return 4;

    // Mutate via the explicit .machine().data_mut() path.
    bridge.machine().data_mut().ticks += 5;
    if (bridge.state().ticks != 6) return 5;

    return 0;
}

// ── Test: protocol view mints a fresh SessionHandle each call ─────
//
// .session_view() returns a handle pointing at the bridge's machine.
// Driving the handle's .send() through a transport that mutates the
// machine is observable through .state() afterwards.

int run_session_view_mints_handle_pointing_at_bridge() {
    VigilBridge bridge{VigilState{VigilState::Mode::Idle, 0}};

    // Mint a handle.  Its compile-time Proto is the Loop's body
    // (Select<Send<int, Continue>, End>) with Loop<...> as LoopCtx.
    auto handle = bridge.session_view();

    // The handle's resource is a pointer to the bridge's Machine.
    auto* machine_ptr = handle.resource();
    if (machine_ptr != &bridge.machine()) return 1;

    // Pick branch 0 (the Send<int, Continue> branch) — no transport,
    // in-memory choice — and obtain the Send-state handle.
    auto send_handle = std::move(handle).select<0>();

    // Drive the Send.  The transport mutates the Machine's State as
    // a side effect; this is the canonical pattern for using a
    // SessionHandle as a typed lens for Machine transitions.
    auto next = std::move(send_handle).send(
        99,
        [](Machine<VigilState>*& m, int v) noexcept {
            m->data_mut().mode  = VigilState::Mode::Recording;
            m->data_mut().ticks = static_cast<uint32_t>(v);
        });

    // The mutation is observable through the bridge's imperative view.
    if (bridge.state().mode  != VigilState::Mode::Recording) return 2;
    if (bridge.state().ticks != 99)                            return 3;

    // `next` is the Continue resolution — back at the Loop body's
    // top (Select).  Detach it cleanly to satisfy the abandonment
    // check; the bridge still owns the Machine.
    std::move(next).detach(proto::detach_reason::TestInstrumentation{});

    return 0;
}

// ── Test: machine_from_session recovers Machine* via mid-protocol
//          detach without firing the abandonment-check ──────────────

int run_machine_from_session_mid_protocol() {
    VigilBridge bridge{VigilState{VigilState::Mode::Replaying, 11}};

    auto handle = bridge.session_view();   // SessionHandle<Select<...>>
    auto* m = machine_from_session(std::move(handle));

    if (m != &bridge.machine()) return 1;
    if (m->data().mode != VigilState::Mode::Replaying) return 2;
    if (m->data().ticks != 11) return 3;
    return 0;
}

// ── Test: machine_from_session at End ─────────────────────────────
//
// Drive the protocol through the End branch (.select<1>()) and
// recover the Machine* via the End-state overload of
// machine_from_session.

int run_machine_from_session_end_state() {
    VigilBridge bridge{VigilState{VigilState::Mode::Serving, 5}};

    auto handle    = bridge.session_view();    // Select<...>
    auto end_handle = std::move(handle).select<1>();  // End

    static_assert(std::is_same_v<
        decltype(end_handle),
        proto::SessionHandle<proto::End,
                              Machine<VigilState>*,
                              VigilProto>>);

    auto* m = machine_from_session(std::move(end_handle));
    if (m != &bridge.machine())                       return 1;
    if (m->data().mode  != VigilState::Mode::Serving) return 2;
    if (m->data().ticks != 5)                          return 3;
    return 0;
}

// ── Test: extract() consumes the bridge and yields the State ──────
//
// The bridge is Pinned (no move), so .extract() is the sole legitimate
// path to recover the State.  Bridge must be a stack-local that goes
// out of scope after extraction — calling it on a long-lived bridge
// would still compile (rvalue ref), but logically the bridge is dead
// after.

int run_extract_yields_state() {
    VigilState recovered{};
    {
        VigilBridge bridge{VigilState{VigilState::Mode::Recording, 77}};
        recovered = std::move(bridge).extract();
    }
    if (recovered.mode  != VigilState::Mode::Recording) return 1;
    if (recovered.ticks != 77)                            return 2;
    return 0;
}

// ── Test: protocol_name() static accessor returns a non-empty
//          rendering (forwards to SessionHandleBase::protocol_name) ─

int run_protocol_name_static() {
    // Treat protocol_name() as a runtime helper, not a constexpr
    // literal — the header documents the cross-TU __PRETTY_FUNCTION__
    // capture caveat.  Compile-time identity checks live in the
    // header self-test (.size() > 0 etc.); substring-match runtime
    // checks belong here.
    auto name = VigilBridge::protocol_name();
    if (name.empty())                                  return 1;
    if (name.find("Loop")   == std::string_view::npos) return 2;
    if (name.find("Select") == std::string_view::npos) return 3;
    if (name.find("Send")   == std::string_view::npos) return 4;
    return 0;
}

// ── Worked example: drive Vigil through a sequence of mode
//    transitions, observing each via the protocol view.
//
// Real Vigil's transitions are richer; this exercises the bridge's
// composition pattern: imperative mutation via .machine(), wire-event
// observation via .session_view() per transition.

int run_worked_example_mode_transitions() {
    VigilBridge bridge{VigilState{}};

    // Tick #1: Idle → Recording.
    {
        auto h0    = bridge.session_view();
        auto h1    = std::move(h0).select<0>();
        auto h2    = std::move(h1).send(
            1,
            [](Machine<VigilState>*& m, int v) noexcept {
                m->data_mut().mode  = VigilState::Mode::Recording;
                m->data_mut().ticks = static_cast<uint32_t>(v);
            });
        std::move(h2).detach(proto::detach_reason::TestInstrumentation{});
    }
    if (bridge.state().mode  != VigilState::Mode::Recording) return 1;
    if (bridge.state().ticks != 1)                            return 2;

    // Tick #2: Recording → Replaying.
    {
        auto h = bridge.session_view();
        auto h1 = std::move(h).select<0>();
        auto h2 = std::move(h1).send(
            2,
            [](Machine<VigilState>*& m, int v) noexcept {
                m->data_mut().mode  = VigilState::Mode::Replaying;
                m->data_mut().ticks = static_cast<uint32_t>(v);
            });
        std::move(h2).detach(proto::detach_reason::TestInstrumentation{});
    }
    if (bridge.state().mode  != VigilState::Mode::Replaying) return 3;
    if (bridge.state().ticks != 2)                            return 4;

    // Tick #3: Replaying → Serving.
    {
        bridge.state_mut().mode  = VigilState::Mode::Serving;
        bridge.state_mut().ticks = 3;
    }
    if (bridge.state().mode  != VigilState::Mode::Serving) return 5;
    if (bridge.state().ticks != 3)                          return 6;

    return 0;
}

}  // anonymous namespace

int main() {
    if (int rc = run_construct_from_state();                          rc != 0) return rc;
    if (int rc = run_construct_in_place();                            rc != 0) return 100 + rc;
    if (int rc = run_construct_from_machine();                        rc != 0) return 200 + rc;
    if (int rc = run_imperative_mutation_via_machine();               rc != 0) return 300 + rc;
    if (int rc = run_session_view_mints_handle_pointing_at_bridge();  rc != 0) return 400 + rc;
    if (int rc = run_machine_from_session_mid_protocol();             rc != 0) return 500 + rc;
    if (int rc = run_machine_from_session_end_state();                rc != 0) return 600 + rc;
    if (int rc = run_extract_yields_state();                          rc != 0) return 700 + rc;
    if (int rc = run_protocol_name_static();                          rc != 0) return 800 + rc;
    if (int rc = run_worked_example_mode_transitions();               rc != 0) return 900 + rc;

    std::puts("machine_session_bridge: dual views + handle minting + worked example OK");
    return 0;
}
