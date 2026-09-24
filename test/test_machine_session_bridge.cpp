// The bridge gives one state machine two views of itself: an
// imperative one that mutates the state directly, and a protocol one
// that mints a typed handle over the same machine.  What this file
// checks is that the two views stay in agreement, whichever of them a
// change went through.

#include <crucible/bridges/MachineSessionBridge.h>

#include <cstdint>
#include <cstdio>
#include <utility>

namespace {

using namespace crucible::safety;

struct VigilState {
    enum class Mode : uint8_t {
        Idle,
        Recording,
        Replaying,
        Serving
    };
    Mode mode = Mode::Idle;
    uint32_t ticks = 0;
};

// One party, one transition per loop iteration, and an explicit
// branch that closes.  The shape is deliberately minimal: the bridge
// accepts any well-formed protocol, so a richer one would prove
// nothing extra here.
using VigilProto = proto::Loop<proto::Select<proto::Send<int, proto::Continue>, proto::End>>;

using VigilBridge = SessionFromMachine<VigilState, VigilProto>;

// These are also asserted where the bridge is defined.  Repeating
// them here makes a change to its surface fail visibly at a call site
// rather than only inside the definition.
static_assert(!std::is_copy_constructible_v<VigilBridge>);
static_assert(!std::is_move_constructible_v<VigilBridge>);
static_assert(std::is_base_of_v<Pinned<VigilBridge>, VigilBridge>);

// A debug build may carry extra state, so the size claim only holds
// once that is compiled out.
#ifdef NDEBUG
static_assert(sizeof(VigilBridge) == sizeof(VigilState), "Release-mode bridge must add zero bytes beyond State.");
#endif

int run_construct_from_state() {
    VigilBridge bridge{VigilState{VigilState::Mode::Recording, 7}};

    if (bridge.state().mode != VigilState::Mode::Recording) return 1;
    if (bridge.state().ticks != 7) return 2;
    return 0;
}

int run_construct_in_place() {
    VigilBridge bridge{std::in_place, VigilState::Mode::Serving, 42u};

    if (bridge.state().mode != VigilState::Mode::Serving) return 1;
    if (bridge.state().ticks != 42) return 2;
    return 0;
}

int run_construct_from_machine() {
    auto m = mint_machine<VigilState>(VigilState::Mode::Replaying, 100u);
    VigilBridge bridge{std::move(m)};

    if (bridge.state().mode != VigilState::Mode::Replaying) return 1;
    if (bridge.state().ticks != 100) return 2;
    return 0;
}

int run_imperative_mutation_via_machine() {
    VigilBridge bridge{VigilState{}};

    if (bridge.state().mode != VigilState::Mode::Idle) return 1;
    if (bridge.state().ticks != 0) return 2;

    bridge.state_mut().mode = VigilState::Mode::Recording;
    bridge.state_mut().ticks = 1;
    if (bridge.state().mode != VigilState::Mode::Recording) return 3;
    if (bridge.state().ticks != 1) return 4;

    bridge.machine().data_mut().ticks += 5;
    if (bridge.state().ticks != 6) return 5;

    return 0;
}

// A handle points at the bridge's own machine, so work driven through
// the handle shows up in the imperative view afterwards.
int run_session_view_mints_handle_pointing_at_bridge() {
    VigilBridge bridge{VigilState{VigilState::Mode::Idle, 0}};

    auto handle = bridge.session_view();

    auto* machine_ptr = handle.resource();
    if (machine_ptr != &bridge.machine()) return 1;

    // The branch is chosen locally, with no transport involved.
    auto send_handle = std::move(handle).select_local<0>();

    // The transport is where the state transition happens.  Driving a
    // send is how a protocol handle acts as a typed lens over the
    // machine underneath it.
    auto next = std::move(send_handle).send(99, [](Machine<VigilState>*& m, int v) noexcept {
        m->data_mut().mode = VigilState::Mode::Recording;
        m->data_mut().ticks = static_cast<uint32_t>(v);
    });

    if (bridge.state().mode != VigilState::Mode::Recording) return 2;
    if (bridge.state().ticks != 99) return 3;

    // A handle left unfinished trips the abandonment check, so it is
    // detached explicitly.  Ownership of the machine stays with the
    // bridge either way.
    std::move(next).detach(proto::detach_reason::TestInstrumentation{});

    return 0;
}

// Recovering the machine from a handle part-way through the protocol
// is a clean exit and does not trip the abandonment check.
int run_machine_from_session_mid_protocol() {
    VigilBridge bridge{VigilState{VigilState::Mode::Replaying, 11}};

    auto handle = bridge.session_view();  // SessionHandle<Select<...>>
    auto* m = machine_from_session(std::move(handle));

    if (m != &bridge.machine()) return 1;
    if (m->data().mode != VigilState::Mode::Replaying) return 2;
    if (m->data().ticks != 11) return 3;
    return 0;
}

// The same recovery once the protocol has reached its end.
int run_machine_from_session_end_state() {
    VigilBridge bridge{VigilState{VigilState::Mode::Serving, 5}};

    auto handle = bridge.session_view();  // Select<...>
    auto end_handle = std::move(handle).select_local<1>();  // End

    static_assert(
        std::is_same_v<decltype(end_handle), proto::SessionHandle<proto::End, Machine<VigilState>*, VigilProto>>);

    auto* m = machine_from_session(std::move(end_handle));
    if (m != &bridge.machine()) return 1;
    if (m->data().mode != VigilState::Mode::Serving) return 2;
    if (m->data().ticks != 5) return 3;
    return 0;
}

// The bridge cannot be moved, so extraction is the only way to get
// the state back out.  It consumes the bridge, which is why the one
// below is scoped to die immediately after.
int run_extract_yields_state() {
    VigilState recovered{};
    {
        VigilBridge bridge{VigilState{VigilState::Mode::Recording, 77}};
        recovered = std::move(bridge).extract();
    }
    if (recovered.mode != VigilState::Mode::Recording) return 1;
    if (recovered.ticks != 77) return 2;
    return 0;
}

// The rendered protocol name comes from a compiler-provided string
// whose exact spelling is not portable, so it is matched here by
// substring at runtime rather than compared as a constant.
int run_protocol_name_static() {
    auto name = VigilBridge::protocol_name();
    if (name.empty()) return 1;
    if (name.find("Loop") == std::string_view::npos) return 2;
    if (name.find("Select") == std::string_view::npos) return 3;
    if (name.find("Send") == std::string_view::npos) return 4;
    return 0;
}

// Three transitions, the first two driven through the protocol view
// and the third through the imperative one, with the state read back
// the same way after each.
int run_worked_example_mode_transitions() {
    VigilBridge bridge{VigilState{}};

    {
        auto h0 = bridge.session_view();
        auto h1 = std::move(h0).select_local<0>();
        auto h2 = std::move(h1).send(1, [](Machine<VigilState>*& m, int v) noexcept {
            m->data_mut().mode = VigilState::Mode::Recording;
            m->data_mut().ticks = static_cast<uint32_t>(v);
        });
        std::move(h2).detach(proto::detach_reason::TestInstrumentation{});
    }
    if (bridge.state().mode != VigilState::Mode::Recording) return 1;
    if (bridge.state().ticks != 1) return 2;

    {
        auto h = bridge.session_view();
        auto h1 = std::move(h).select_local<0>();
        auto h2 = std::move(h1).send(2, [](Machine<VigilState>*& m, int v) noexcept {
            m->data_mut().mode = VigilState::Mode::Replaying;
            m->data_mut().ticks = static_cast<uint32_t>(v);
        });
        std::move(h2).detach(proto::detach_reason::TestInstrumentation{});
    }
    if (bridge.state().mode != VigilState::Mode::Replaying) return 3;
    if (bridge.state().ticks != 2) return 4;

    {
        bridge.state_mut().mode = VigilState::Mode::Serving;
        bridge.state_mut().ticks = 3;
    }
    if (bridge.state().mode != VigilState::Mode::Serving) return 5;
    if (bridge.state().ticks != 3) return 6;

    return 0;
}

}  // anonymous namespace

int main() {
    if (int rc = run_construct_from_state(); rc != 0) return rc;
    if (int rc = run_construct_in_place(); rc != 0) return 100 + rc;
    if (int rc = run_construct_from_machine(); rc != 0) return 200 + rc;
    if (int rc = run_imperative_mutation_via_machine(); rc != 0) return 300 + rc;
    if (int rc = run_session_view_mints_handle_pointing_at_bridge(); rc != 0) return 400 + rc;
    if (int rc = run_machine_from_session_mid_protocol(); rc != 0) return 500 + rc;
    if (int rc = run_machine_from_session_end_state(); rc != 0) return 600 + rc;
    if (int rc = run_extract_yields_state(); rc != 0) return 700 + rc;
    if (int rc = run_protocol_name_static(); rc != 0) return 800 + rc;
    if (int rc = run_worked_example_mode_transitions(); rc != 0) return 900 + rc;

    std::puts("machine_session_bridge: dual views + handle minting + worked example OK");
    return 0;
}
