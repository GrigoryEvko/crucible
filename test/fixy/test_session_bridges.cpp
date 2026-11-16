// What fixy/session/MachineBridge.h and fixy/session/VigilMode.h claim,
// checked.
//
// The two bridges answer different questions.  SessionFromMachine owns
// a machine and lends it to one view at a time, so the checks are about
// ownership and about the lease that a view gets.  The atomic form borrows
// a cell another thread publishes into, so the checks are about the
// concept that admits a cell and about walking the protocol far enough
// to observe the state actually change.
//
// The walk matters more than it looks.  A session bridge that mints and
// is never stepped proves only that the types line up.  Its protocol can
// then be mostly unreachable through its own factory, and no check
// notices.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/session/VigilMode.h>

#include <foundation/Platform.h>
#include <foundation/effects/Computation.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

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
static_assert(s::CtxFitsSession<BgIoCtx, SendsIo, IoBridge::lease_type>);
static_assert(!s::CtxFitsSession<BgCtx, SendsIo, IoBridge::lease_type>, "the background context holds no IO");
static_assert(!s::CtxFitsAtomicSession<BgCtx, SendsIo, vm::ModeCell>, "the background context holds no IO");
static_assert(s::CtxFitsAtomicSession<BgIoCtx, SendsIo, vm::ModeCell>);
static_assert(!s::CtxFitsAtomicSession<int, vm::ModeProtocol, vm::ModeCell>, "an int is not an execution context");

// ── The bridge owns, and cannot move ─────────────────────────────────
//
// The lease of a view points at the machine of the bridge, so a bridge
// that moved during a view would leave the lease pointing at freed
// storage, and the next step would follow the pointer.
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

// The Resource of a view is the lease of the machine, never the bridge
// and never a bare pointer to the machine.  The lease reaches the state
// and nothing else, so a body cannot reach extract() or a second view
// through it.
static_assert(s::SessionResource<Bridge::lease_type>);
static_assert(!s::SessionResource<::fixy::Machine<Payload>*>, "a pointer to the machine is a second holder");

// The bridge adds nothing to the machine it owns: the Pinned base is
// empty and the machine is the only member.  An assertion of this under
// `#ifdef NDEBUG` would hide it from every build that runs the test
// suite.  Nothing here varies with the build mode, because the bridge
// stores no handle and therefore no abandonment policy.
static_assert(sizeof(Bridge) == sizeof(::fixy::Machine<Payload>));
static_assert(sizeof(s::SessionFromMachine<char, s::End>) == sizeof(::fixy::Machine<char>));
static_assert(sizeof(s::SessionFromMachine<double, s::End>) == sizeof(::fixy::Machine<double>));

// ── The atomic cell ──────────────────────────────────────────────────

static_assert(s::AtomicMachineCell<vm::ModeCell>);
static_assert(!s::AtomicMachineCell<Payload>);
static_assert(s::WellFormedRunnableProtocol<vm::ModeProtocol>);
static_assert(sizeof(vm::ModeCell) <= 2 * sizeof(::foundation::core::Identity));

// The handle holds a lease: the cell and its one writer, which the owner
// lends for the span of the session.  The address of the cell is its
// identity, so the cell is Pinned and the lease holds its address.  The
// lease moves, and a copy of it would give the cell a second writer.
static_assert(std::is_same_v<typename vm::ModeSessionHandle<>::resource_type, vm::ModeLease>);
static_assert(std::is_base_of_v<::foundation::Pinned<vm::ModeCell>, vm::ModeCell>);
static_assert(s::SessionResource<vm::ModeLease>);
static_assert(!std::is_copy_constructible_v<vm::ModeLease>);
static_assert(!std::is_copy_constructible_v<vm::ModeWriter>);

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

// ── Runtime: the bridge lends the machine it owns to a view ──────────

// The body of a view holds no state.  It sends 7 through the lease, then
// leaves the loop, and returns the End handle that ends the view.
constexpr auto report_seven = [](auto head) noexcept {
    auto sending = std::move(head).template select<0>(s::no_label);
    auto again = std::move(sending).send(7, [](auto& lease, int& value) noexcept {
        lease.state_mut().ticks = value;
        return true;
    });
    return std::move(again).template select<1>(s::no_label);
};

[[nodiscard]] int bridge_owns_and_lends() {
    auto bridge = s::mint_session_from_machine<Reporting, Payload>(5);
    if (bridge.state().ticks != 5) {
        std::fprintf(stderr, "the mint did not build the state from its arguments\n");
        return 1;
    }

    // The bridge keeps its own imperative access when no view lives.
    bridge.state_mut().ticks = 6;

    // A step of the view reaches the machine through the lease.
    const BgCtx ctx{eff::testing::bg()};
    bridge.with_view(ctx, report_seven);
    if (bridge.state().ticks != 7) {
        std::fprintf(stderr, "the send did not reach the machine of the bridge\n");
        return 1;
    }

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

// At End the protocol owes nothing, so the body returns its head as it
// is, and the view ends with the state as it was.
[[nodiscard]] int terminal_view_ends() {
    auto bridge = s::mint_session_from_machine<s::End, Payload>(1);
    const BgCtx ctx{eff::testing::bg()};
    bridge.with_view(ctx, [](auto head) noexcept { return head; });
    if (bridge.state().ticks != 1) {
        std::fprintf(stderr, "a view of End changed the state\n");
        return 1;
    }
    return 0;
}

// ── Runtime: the mode protocol is actually walked ────────────────────

// The transport is the publish step of the lease.  Each Send branch exists
// for exactly one of its overloads, so the branch index and the transition
// type cannot drift apart without a compile error.
constexpr auto apply = []<typename Transition>(vm::ModeLease& lease, Transition& transition) noexcept {
    return lease.publish_from_session(transition);
};

[[nodiscard]] int mode_protocol_round_trip() {
    vm::ModeWriter writer = vm::mint_mode_writer();
    vm::ModeCell cell{writer};
    if (s::atomic_machine_state(cell) != vm::Mode::RECORDING) {
        std::fprintf(stderr, "a fresh cell did not start in RECORDING\n");
        return 1;
    }

    // The owner lends its writer to the session for the span of the
    // session.
    const BgCtx ctx{eff::testing::bg()};
    auto session = vm::mint_vigil_mode_bridge(ctx, cell, std::move(writer));

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

    // Branch 2 is End, which is the protocol's only exit.  The close gives
    // the lease back, and the lease gives the writer back to the owner.
    auto terminal = std::move(after_recording).select<2>(s::no_label);
    vm::ModeLease lease = std::move(terminal).close();
    const vm::ModeWriter returned = std::move(lease).return_writer();

    // The direct publishers stay available to the thread that owns the
    // cell, with the writer that came back.  The cell checks that the writer
    // is its own, so a lease of a different cell would end the process here.
    // They publish with release stores.
    cell.publish_compiled(returned);
    if (s::atomic_machine_state(cell) != vm::Mode::COMPILED) {
        std::fprintf(stderr, "publish_compiled did not take\n");
        return 1;
    }
    cell.publish_recording_after_divergence(returned);
    if (s::atomic_machine_state(cell) != vm::Mode::RECORDING) {
        std::fprintf(stderr, "publish_recording_after_divergence did not take\n");
        return 1;
    }
    return 0;
}

// ── Runtime: one writer, and no edge that the cell did not take ──────
//
// The owner publishes COMPILED and then lends the writer.  The session
// sends the edge RECORDING -> COMPILED, whose source the cell does not
// hold.  The session holds the one writer, so a later try cannot succeed,
// and the edge ends the process.  A cell that answered true here would
// let the session log an edge that it did not take.  The session runs in a
// child through expect_dies of the Check family.
void an_edge_from_another_mode_ends_the_process(::fixy::TestRunnerCtx const& test) {
    vm::ModeWriter writer = vm::mint_mode_writer();
    vm::ModeCell cell{writer};
    cell.publish_compiled(writer);
    const BgCtx ctx{eff::testing::bg()};
    ::fixy::check::expect_dies(
        test,
        [&cell, &writer, &ctx] noexcept {
            auto session = vm::mint_vigil_mode_bridge(ctx, cell, std::move(writer));
            auto to_compiled = std::move(session).select<0>(s::no_label);
            auto after = std::move(to_compiled).send(vm::ModeRecordingToCompiled{}, apply);
            std::move(after).detach(s::detach_reason::TestInstrumentation{});
        },
        ::fixy::TextView{"fixy::session::diagnostic [VigilModeBridge_Edge_Not_At_Source]"});
}

// ── Runtime: a published mode orders the writes before it ────────────
//
// The owner writes a plain value and then publishes COMPILED.  The
// observer waits for COMPILED with the default load and then reads the
// value.  The release store and the acquire load order the two accesses.
// With relaxed accesses the read is a data race, and the tsan preset
// reports it.
[[nodiscard]] int published_mode_orders_the_owner_writes() {
    const vm::ModeWriter writer = vm::mint_mode_writer();
    vm::ModeCell cell{writer};
    int written_before_publish = 0;
    int observed = 0;
    {
        std::jthread observer{[&cell, &written_before_publish, &observed] {
            while (cell.load() != vm::Mode::COMPILED)
                CRUCIBLE_SPIN_PAUSE;
            observed = written_before_publish;
        }};
        written_before_publish = 42;
        cell.publish_compiled(writer);
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
    if (const int rc = terminal_view_ends(); rc != 0) return rc;
    if (const int rc = mode_protocol_round_trip(); rc != 0) return rc;
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    an_edge_from_another_mode_ends_the_process(test);
    if (const int rc = published_mode_orders_the_owner_writes(); rc != 0) return rc;
    return 0;
}
