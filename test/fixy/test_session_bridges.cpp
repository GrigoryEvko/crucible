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
// is never stepped proves only that the types line up.  Its protocol can
// then be mostly unreachable through its own factory, and no check
// notices.

#include <fixy/session/VigilMode.h>

#include <foundation/Platform.h>
#include <foundation/effects/Computation.h>
#include <foundation/effects/Ctx.h>

#include <cstdio>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

namespace s = fixy::session;
namespace vm = fixy::session::vigil_mode;
namespace eff = ::foundation::effects;

namespace {

using BgCtx = eff::detail::ctx_witnesses::BgWitness;
using BgIoCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO>>;

struct Payload {
    int ticks = 0;
};

using Reporting = s::Loop<s::Select<s::Send<int, s::Continue>, s::End>>;
using Bridge = s::SessionFromMachine<Payload, Reporting>;

// ── The context gate ─────────────────────────────────────────────────
//
// A view and an atomic session are sessions, so the context must admit
// the effect row of each payload, as the context of mint_session must.
using SendsIo = s::Send<eff::Computation<eff::Row<eff::Effect::IO>, int>, s::End>;
using IoBridge = s::SessionFromMachine<Payload, SendsIo>;
static_assert(s::CtxFitsSession<BgIoCtx, SendsIo, IoBridge&>);
static_assert(!s::CtxFitsSession<BgCtx, SendsIo, IoBridge&>, "the background context holds no IO");
static_assert(!s::CtxFitsAtomicSession<BgCtx, SendsIo, vm::ModeCell>, "the background context holds no IO");
static_assert(s::CtxFitsAtomicSession<BgIoCtx, SendsIo, vm::ModeCell>);
static_assert(!s::CtxFitsAtomicSession<int, vm::ModeProtocol, vm::ModeCell>, "an int is not an execution context");

// ── The bridge owns, and cannot move ─────────────────────────────────
//
// A minted handle holds a reference to the bridge, so a bridge that
// moved would leave every live handle referring to freed storage and the
// next step would follow the reference.
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
// unroll is what binds the Continue inside the body.  The Resource is a
// reference to the Pinned bridge, never a pointer to the machine.
using ExpectedView = s::SessionHandle<s::Select<s::Send<int, s::Continue>, s::End>, Bridge&, Reporting>;
static_assert(std::is_same_v<s::session_view_t<Bridge, BgCtx>, ExpectedView>);
static_assert(s::SessionResource<Bridge&>);
static_assert(!s::SessionResource<::fixy::Machine<Payload>*>, "a pointer to the machine is a second holder");

// The bridge adds nothing to the state it owns: the Pinned base is
// empty and the machine is the only member.  An assertion of this under
// `#ifdef NDEBUG` would hide it from every build that runs the test
// suite.  Nothing here varies with the build mode, because the bridge
// stores no handle and therefore no abandonment policy.
static_assert(sizeof(Bridge) == sizeof(Payload));
static_assert(sizeof(s::SessionFromMachine<char, s::End>) == sizeof(char));
static_assert(sizeof(s::SessionFromMachine<double, s::End>) == sizeof(double));

// ── The atomic cell ──────────────────────────────────────────────────

static_assert(s::AtomicMachineCell<vm::ModeCell>);
static_assert(!s::AtomicMachineCell<Payload>);
static_assert(s::WellFormedRunnableProtocol<vm::ModeProtocol>);
static_assert(sizeof(vm::ModeCell) == sizeof(std::atomic<vm::Mode>));

// The handle borrows the cell mutably, which is what lets a Send branch
// reach publish_from_session.  The address of the cell is its identity,
// so the cell is Pinned and the handle holds a reference to it.
static_assert(std::is_same_v<typename vm::ModeSessionHandle::resource_type, vm::ModeCell&>);
static_assert(std::is_base_of_v<::foundation::Pinned<vm::ModeCell>, vm::ModeCell>);

// A cell with the read shape that is not Pinned is refused: a copy of
// such a cell is a second cell, and a pointer to it is a second holder.
struct LoosePhaseCell {
    using state_type = vm::Mode;
    [[nodiscard]] vm::Mode load() const noexcept { return vm::Mode::RECORDING; }
};
static_assert(!s::AtomicMachineCell<LoosePhaseCell>);

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
struct LookalikeCell : ::foundation::Pinned<LookalikeCell> {
    using state_type = vm::Mode;
    [[nodiscard]] vm::Mode load() const noexcept { return vm::Mode::RECORDING; }
};
static_assert(s::AtomicMachineCell<LookalikeCell>);
static_assert(vm::CtxFitsVigilModeBridge<BgCtx, vm::ModeCell>);
static_assert(!vm::CtxFitsVigilModeBridge<BgCtx, LookalikeCell>);
static_assert(!vm::CtxFitsVigilModeBridge<int, vm::ModeCell>, "an int is not an execution context");

// ── Runtime: the bridge is a lens on the machine it owns ─────────────

[[nodiscard]] int bridge_owns_and_lends() {
    auto bridge = s::mint_session_from_machine<Reporting, Payload>(5);
    if (bridge.state().ticks != 5) {
        std::fprintf(stderr, "the mint did not build the state from its arguments\n");
        return 1;
    }

    // The bridge keeps its own imperative view while a handle is out.
    bridge.state_mut().ticks = 6;

    const BgCtx ctx{eff::testing::bg()};
    auto view = bridge.session_view(ctx);
    if (&view.resource() != &bridge || &view.resource().machine() != &bridge.machine()) {
        std::fprintf(stderr, "the handle borrowed something other than the bridge\n");
        return 1;
    }

    // A step reaches the machine through the bridge that the handle holds.
    auto sending = std::move(view).select<0>(s::no_label);
    auto again = std::move(sending).send(7, [](Bridge& held, int& value) noexcept {
        held.state_mut().ticks = value;
        return true;
    });
    if (bridge.state().ticks != 7) {
        std::fprintf(stderr, "the send did not reach the machine of the bridge\n");
        return 1;
    }

    // The protocol has not ended.  The detach tells the destructor that
    // the abandonment is deliberate.  Without it this function would
    // abort under the checking policy.
    std::move(again).detach(s::detach_reason::OwnerLifetimeBoundEarlyExit{});

    // extract() is the only way the state leaves the bridge, because
    // the bridge itself cannot be moved.
    const Payload taken = std::move(bridge).extract();
    if (taken.ticks != 7) {
        std::fprintf(stderr, "extract did not carry the state out\n");
        return 1;
    }

    if (Bridge::protocol_name().find("Loop") == std::string_view::npos) {
        std::fprintf(stderr, "protocol_name did not render the protocol\n");
        return 1;
    }
    return 0;
}

// At End the protocol owes nothing, so close() hands back the reference
// to the bridge that the handle held.
[[nodiscard]] int terminal_view_closes() {
    auto bridge = s::mint_session_from_machine<s::End, Payload>(1);
    const BgCtx ctx{eff::testing::bg()};
    auto view = bridge.session_view(ctx);
    static_assert(std::is_same_v<typename decltype(view)::protocol, s::End>);

    auto& recovered = std::move(view).close();
    if (&recovered != &bridge || &recovered.machine() != &bridge.machine()) {
        std::fprintf(stderr, "close returned a different bridge\n");
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
    auto apply = []<typename Transition>(vm::ModeCell& target, Transition& transition) noexcept {
        return s::publish_atomic_machine_transition(target, transition);
    };

    const BgCtx ctx{eff::testing::bg()};
    auto session = vm::mint_vigil_mode_bridge(ctx, cell);

    auto to_compiled = std::move(session).select<0>(s::no_label);
    auto after_compiled = std::move(to_compiled).send(vm::ModeRecordingToCompiled{}, apply);
    if (s::atomic_machine_state(cell) != vm::Mode::COMPILED) {
        std::fprintf(stderr, "the RECORDING -> COMPILED send did not reach the cell\n");
        return 1;
    }

    auto to_recording = std::move(after_compiled).select<1>(s::no_label);
    auto after_recording = std::move(to_recording).send(vm::ModeCompiledToRecording{}, apply);
    if (s::atomic_machine_state(cell) != vm::Mode::RECORDING) {
        std::fprintf(stderr, "the COMPILED -> RECORDING send did not reach the cell\n");
        return 1;
    }

    // Branch 2 is End, which is the protocol's only exit.
    auto terminal = std::move(after_recording).select<2>(s::no_label);
    vm::ModeCell& recovered = std::move(terminal).close();
    if (&recovered != &cell) {
        std::fprintf(stderr, "close returned a different cell\n");
        return 1;
    }

    // The direct publishers stay available to the thread that owns the
    // cell.  They publish with release stores.
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

// ── Runtime: a published mode orders the writes before it ────────────
//
// The owner writes a plain value and then publishes COMPILED.  The
// observer waits for COMPILED with the default load and then reads the
// value.  The release store and the acquire load order the two accesses.
// With relaxed accesses the read is a data race, and the tsan preset
// reports it.
[[nodiscard]] int published_mode_orders_the_owner_writes() {
    vm::ModeCell cell{};
    int written_before_publish = 0;
    int observed = 0;
    {
        std::jthread observer{[&cell, &written_before_publish, &observed] {
            while (cell.load() != vm::Mode::COMPILED) CRUCIBLE_SPIN_PAUSE;
            observed = written_before_publish;
        }};
        written_before_publish = 42;
        cell.publish_compiled();
    }
    if (observed != 42) {
        std::fprintf(stderr, "the observer read %d, not the value written before the publish\n", observed);
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    if (const int rc = bridge_owns_and_lends(); rc != 0) return rc;
    if (const int rc = terminal_view_closes(); rc != 0) return rc;
    if (const int rc = mode_protocol_round_trip(); rc != 0) return rc;
    if (const int rc = published_mode_orders_the_owner_writes(); rc != 0) return rc;
    return 0;
}
