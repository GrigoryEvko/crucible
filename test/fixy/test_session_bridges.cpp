// What fixy/session/MachineBridge.h and fixy/session/VigilMode.h claim,
// checked.
//
// The two bridges answer different questions.  SessionFromMachine owns
// a machine and mints a lens over it, so the checks are about ownership
// and about the exact handle a Proto produces.  The atomic form borrows
// a cell another thread publishes into, so the checks are about the
// concept that admits a cell and about walking the protocol far enough
// to observe the state actually change.
//
// The walk matters more than it looks.  A session bridge that mints and
// is never stepped proves only that the types line up; the frozen tree
// had exactly that for the mode bridge, and the protocol it named was
// two thirds unreachable through its own factory.

#include <fixy/session/VigilMode.h>

#include <cstdio>
#include <string_view>
#include <type_traits>
#include <utility>

namespace s = fixy::session;
namespace vm = fixy::session::vigil_mode;

namespace {

struct Payload {
    int ticks = 0;
};

using Reporting = s::Loop<s::Select<s::Send<int, s::Continue>, s::End>>;
using Bridge = s::SessionFromMachine<Payload, Reporting>;

// ── The bridge owns, and cannot move ─────────────────────────────────
//
// A minted handle stores the address of the machine it borrows, so a
// bridge that moved would leave every live handle pointing at freed
// storage and the next step would follow the pointer.
static_assert(!std::is_copy_constructible_v<Bridge>);
static_assert(!std::is_move_constructible_v<Bridge>);
static_assert(!std::is_copy_assignable_v<Bridge>);
static_assert(!std::is_move_assignable_v<Bridge>);
static_assert(std::is_base_of_v<::foundation::Pinned<Bridge>, Bridge>);

// The mint is the only door: the value constructor is private, so a
// call site cannot build a bridge around a protocol the gate refused.
static_assert(!std::is_constructible_v<Bridge, ::fixy::Machine<Payload>>);
static_assert(!std::is_constructible_v<Bridge, Payload>);

static_assert(std::is_same_v<typename Bridge::state_type, Payload>);
static_assert(std::is_same_v<typename Bridge::machine_type, ::fixy::Machine<Payload>>);
static_assert(std::is_same_v<typename Bridge::protocol, Reporting>);

// A loop is unrolled one step at mint time, so the handle carries the
// loop body as its protocol and the loop itself as the context.  That
// unroll is what binds the Continue inside the body.
using ExpectedView =
    s::SessionHandle<s::Select<s::Send<int, s::Continue>, s::End>, ::fixy::Machine<Payload>*, Reporting>;
static_assert(std::is_same_v<typename Bridge::session_handle_type, ExpectedView>);

// The bridge adds nothing to the state it owns: the Pinned base is
// empty and the machine is the only member.  The ported header asserted
// this under `#ifdef NDEBUG`, which hid it from every build that runs
// the test suite; nothing here varies with the build mode, because the
// bridge stores no handle and therefore no abandonment policy.
static_assert(sizeof(Bridge) == sizeof(Payload));
static_assert(sizeof(s::SessionFromMachine<char, s::End>) == sizeof(char));
static_assert(sizeof(s::SessionFromMachine<double, s::End>) == sizeof(double));

// ── The atomic cell ──────────────────────────────────────────────────

static_assert(s::AtomicMachineCell<vm::ModeCell>);
static_assert(!s::AtomicMachineCell<Payload>);
static_assert(s::WellFormedRunnableProtocol<vm::ModeProtocol>);
static_assert(sizeof(vm::ModeCell) == sizeof(std::atomic<vm::Mode>));

// The handle borrows the cell mutably, which is what lets a Send branch
// reach publish_from_session.
static_assert(std::is_same_v<typename vm::ModeSessionHandle::resource_type, vm::ModeCell*>);

// ── The transition relation ──────────────────────────────────────────
//
// Two edges are declared, and the relation admits those two and nothing
// else.  Reading the namespace is what produces these answers, so a
// third Mode added without an edge is visibly absent rather than
// silently missing from a clause of a boolean expression.
static_assert(vm::mode_transition_allowed_v<vm::Mode::RECORDING, vm::Mode::COMPILED>);
static_assert(vm::mode_transition_allowed_v<vm::Mode::COMPILED, vm::Mode::RECORDING>);

static_assert(!vm::mode_transition_allowed_v<vm::Mode::RECORDING, vm::Mode::RECORDING>);
static_assert(!vm::mode_transition_allowed_v<vm::Mode::COMPILED, vm::Mode::COMPILED>);
static_assert(!vm::mode_transition_allowed_v<vm::Mode::DIVERGED, vm::Mode::DIVERGED>);

// DIVERGED is a replay status, not a persistent mode, so it is neither
// a source nor a target.  All four crossings are rejected.
static_assert(!vm::mode_transition_allowed_v<vm::Mode::RECORDING, vm::Mode::DIVERGED>);
static_assert(!vm::mode_transition_allowed_v<vm::Mode::COMPILED, vm::Mode::DIVERGED>);
static_assert(!vm::mode_transition_allowed_v<vm::Mode::DIVERGED, vm::Mode::RECORDING>);
static_assert(!vm::mode_transition_allowed_v<vm::Mode::DIVERGED, vm::Mode::COMPILED>);

static_assert(vm::ModeRecordingToCompiled::from == vm::Mode::RECORDING);
static_assert(vm::ModeRecordingToCompiled::to == vm::Mode::COMPILED);
static_assert(vm::ModeCompiledToRecording::from == vm::Mode::COMPILED);
static_assert(vm::ModeCompiledToRecording::to == vm::Mode::RECORDING);

// The mint's gate is nominal, not structural: a cell that reads exactly
// like ModeCell is still refused, so a call cannot pick up the mode
// protocol by accident.
struct LookalikeCell {
    using state_type = vm::Mode;
    [[nodiscard]] vm::Mode load(std::memory_order = std::memory_order_relaxed) const noexcept {
        return vm::Mode::RECORDING;
    }
};
static_assert(s::AtomicMachineCell<LookalikeCell>);
static_assert(vm::CanMintVigilModeBridge<vm::ModeCell>);
static_assert(!vm::CanMintVigilModeBridge<LookalikeCell>);

// ── Runtime: the bridge is a lens on the machine it owns ─────────────

[[nodiscard]] int bridge_owns_and_lends() {
    auto bridge = s::mint_session_from_machine<Reporting, Payload>(5);
    if (bridge.state().ticks != 5) {
        std::fprintf(stderr, "the mint did not build the state from its arguments\n");
        return 1;
    }

    // The bridge keeps its own imperative view while a handle is out.
    bridge.state_mut().ticks = 6;

    auto view = bridge.session_view();
    if (view.resource() != &bridge.machine()) {
        std::fprintf(stderr, "the handle borrowed something other than the bridge's machine\n");
        return 1;
    }

    // Recovering the machine before the protocol ends is the lossy
    // direction, and the detach inside machine_from_session is what
    // tells the destructor the abandonment was deliberate.  Without it
    // this function would abort under the checking policy.
    ::fixy::Machine<Payload>* recovered = s::machine_from_session(std::move(view));
    if (recovered != &bridge.machine() || recovered->data().ticks != 6) {
        std::fprintf(stderr, "machine_from_session did not return the borrowed machine\n");
        return 1;
    }

    // extract() is the only way the state leaves the bridge, because
    // the bridge itself cannot be moved.
    const Payload taken = std::move(bridge).extract();
    if (taken.ticks != 6) {
        std::fprintf(stderr, "extract did not carry the state out\n");
        return 1;
    }

    if (Bridge::protocol_name().find("Loop") == std::string_view::npos) {
        std::fprintf(stderr, "protocol_name did not render the protocol\n");
        return 1;
    }
    return 0;
}

// The End overload of machine_from_session closes rather than detaches,
// because at End the protocol owes nothing and the handle is entitled
// to hand its resource back.
[[nodiscard]] int terminal_recovery_closes() {
    auto bridge = s::mint_session_from_machine<s::End, Payload>(1);
    auto view = bridge.session_view();
    static_assert(std::is_same_v<typename decltype(view)::protocol, s::End>);

    ::fixy::Machine<Payload>* recovered = s::machine_from_session(std::move(view));
    if (recovered != &bridge.machine()) {
        std::fprintf(stderr, "the terminal overload returned a different machine\n");
        return 1;
    }
    return 0;
}

// ── Runtime: the mode protocol is actually walked ────────────────────

[[nodiscard]] int mode_protocol_round_trip() {
    vm::ModeCell cell{};
    if (s::atomic_machine_state(cell) != vm::Mode::RECORDING) {
        std::fprintf(stderr, "a fresh cell did not start in RECORDING\n");
        return 1;
    }

    // The transport is the cell's own publish step.  Each Send branch
    // exists for exactly one of its overloads, so the branch index and
    // the transition type cannot drift apart without a compile error.
    auto apply = []<typename Transition>(vm::ModeCell*& target, Transition&& transition) noexcept {
        s::publish_atomic_machine_transition(target, std::forward<Transition>(transition));
    };

    auto session = vm::mint_vigil_mode_bridge(cell);

    auto to_compiled = std::move(session).select_local<0>();
    auto after_compiled = std::move(to_compiled).send(vm::ModeRecordingToCompiled{}, apply);
    if (s::atomic_machine_state(cell) != vm::Mode::COMPILED) {
        std::fprintf(stderr, "the RECORDING -> COMPILED send did not reach the cell\n");
        return 1;
    }

    auto to_recording = std::move(after_compiled).select_local<1>();
    auto after_recording = std::move(to_recording).send(vm::ModeCompiledToRecording{}, apply);
    if (s::atomic_machine_state(cell) != vm::Mode::RECORDING) {
        std::fprintf(stderr, "the COMPILED -> RECORDING send did not reach the cell\n");
        return 1;
    }

    // Branch 2 is End, which is the protocol's only exit.
    auto terminal = std::move(after_recording).select_local<2>();
    vm::ModeCell* recovered = std::move(terminal).close();
    if (recovered != &cell) {
        std::fprintf(stderr, "close returned a different cell\n");
        return 1;
    }

    // The direct publishers stay available to the thread that owns the
    // cell; they are relaxed because that thread needs no ordering
    // against itself.
    cell.publish_compiled();
    if (s::atomic_machine_state(cell) != vm::Mode::COMPILED) {
        std::fprintf(stderr, "publish_compiled did not take\n");
        return 1;
    }
    cell.publish_recording_after_divergence();
    if (s::atomic_machine_state(cell) != vm::Mode::RECORDING) {
        std::fprintf(stderr, "publish_recording_after_divergence did not take\n");
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    if (const int rc = bridge_owns_and_lends(); rc != 0) return rc;
    if (const int rc = terminal_recovery_closes(); rc != 0) return rc;
    if (const int rc = mode_protocol_round_trip(); rc != 0) return rc;
    return 0;
}
