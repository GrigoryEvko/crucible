#pragma once

// A machine and a session are two views of one typestate value.  A
// machine transition is a local computation.  A session step is an
// event an observer can log, replay or compare.  A bridge owns one
// machine and mints a session handle over it on demand, so the same
// state is driven imperatively from inside and read structurally from
// outside.
//
// The bridge is pinned because a minted handle stores the address of
// the machine it borrows.  Moving the bridge would leave the handle
// pointing at freed storage, and the next session step would follow it.
//
// Proto is not constrained to a single party.  Party count is a claim
// about the protocol a caller picked, not a structural property of any
// type here, so a concept asserting it would reject multi-party
// protocols whose local view is a perfectly valid session over this
// machine.  Single-party typestate is the intended use and is left to
// caller discipline.
//
// Old spelling: include/crucible/bridges/MachineSessionBridge.h.

#include <fixy/Machine.h>
#include <fixy/session/Handle.h>

#include <foundation/Pinned.h>

#include <atomic>
#include <concepts>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

// The claims the carriers in this header make that no lattice grades.
// foundation/diag/RowHash.h folds each identity, so every carrier here
// takes a cache slot of its own rather than the zero a bare payload has.
namespace fixy::row_discipline {
template <typename Proto>
struct session_from_machine;
}  // namespace fixy::row_discipline

namespace fixy::session {

template <typename State, typename Proto, std::meta::info Edges>
class SessionFromMachine;

// The bridge's one door.  fixy::Machine made its own constructor
// private and put a mint in front of it for the same reason: the
// admission — here, that Proto can actually carry a runnable handle —
// belongs at one call site that a reader can enumerate, not at every
// constructor a caller might reach.
//
// Args build the State.  The machine's relation namespace defaults to
// the shared one, exactly as it does for a machine minted directly.
template <typename Proto, typename State, std::meta::info Edges = ^^::fixy::machine::admitted_transitions,
          typename... Args>
    requires WellFormedRunnableProtocol<Proto> && std::is_constructible_v<State, Args...>
[[nodiscard]] constexpr auto
mint_session_from_machine(Args&&... args) noexcept(std::is_nothrow_constructible_v<State, Args...>)
    -> SessionFromMachine<State, Proto, Edges>;

template <typename State, typename Proto, std::meta::info Edges = ^^::fixy::machine::admitted_transitions>
class [[nodiscard]] SessionFromMachine : public ::foundation::Pinned<SessionFromMachine<State, Proto, Edges>> {
    static_assert(is_well_formed_v<Proto>, "fixy::session::diagnostic [Protocol_Ill_Formed]: "
                                           "SessionFromMachine<State, Proto>: Proto must be well-formed "
                                           "under is_well_formed_v.  Every Continue must have an "
                                           "enclosing Loop.");

    ::fixy::Machine<State, Edges> machine_;

    constexpr explicit SessionFromMachine(::fixy::Machine<State, Edges> m) noexcept : machine_{std::move(m)} {}

    template <typename FProto, typename FState, std::meta::info FEdges, typename... Args>
        requires WellFormedRunnableProtocol<FProto> && std::is_constructible_v<FState, Args...>
    friend constexpr auto
    mint_session_from_machine(Args&&... args) noexcept(std::is_nothrow_constructible_v<FState, Args...>)
        -> SessionFromMachine<FState, FProto, FEdges>;

public:
    using state_type = State;
    using machine_type = ::fixy::Machine<State, Edges>;
    using protocol = Proto;
    static constexpr std::meta::info edges = Edges;
    using row_discipline = ::fixy::row_discipline::session_from_machine<Proto>;
    using row_payload = machine_type;

    using session_handle_type = decltype(mint_session_handle<Proto>(std::declval<machine_type*>()));

    ~SessionFromMachine() = default;

    [[nodiscard]] constexpr machine_type& machine() & noexcept { return machine_; }

    [[nodiscard]] constexpr const machine_type& machine() const& noexcept { return machine_; }

    [[nodiscard]] constexpr const State& state() const& noexcept { return machine_.data(); }

    [[nodiscard]] constexpr State& state_mut() & noexcept { return machine_.data_mut(); }

    // The bridge is the resource anchor and each handle is a lens for
    // one observable transition, so a fresh handle is minted per call.
    // At most one may be live at a time: two handles would drive the
    // same machine state against each other.  Nothing here enforces
    // that, exactly as nothing enforces it for two mutable borrows of
    // the machine.
    [[nodiscard]] auto session_view() & noexcept -> session_handle_type {
        return mint_session_handle<Proto>(&machine_);
    }

    [[nodiscard]] static constexpr std::string_view protocol_name() noexcept { return type_display_name_v<Proto>; }

    // The bridge cannot be moved, so this is the only way the state
    // leaves it.  The bridge is moved-from afterwards and neither view
    // is usable.
    [[nodiscard]] constexpr State extract() && noexcept(std::is_nothrow_move_constructible_v<State>) {
        return std::move(machine_).extract();
    }
};

template <typename Proto, typename State, std::meta::info Edges, typename... Args>
    requires WellFormedRunnableProtocol<Proto> && std::is_constructible_v<State, Args...>
[[nodiscard]] constexpr auto
mint_session_from_machine(Args&&... args) noexcept(std::is_nothrow_constructible_v<State, Args...>)
    -> SessionFromMachine<State, Proto, Edges> {
    return SessionFromMachine<State, Proto, Edges>{::fixy::mint_machine<State, Edges>(std::forward<Args>(args)...)};
}

// The recovered pointer is a borrow.  The bridge that minted the handle
// has to outlive every pointer recovered from it.

template <typename State, std::meta::info Edges, typename LoopCtx, AbandonmentPolicy Policy>
[[nodiscard]] constexpr ::fixy::Machine<State, Edges>*
machine_from_session(SessionHandle<End, ::fixy::Machine<State, Edges>*, LoopCtx, Policy>&& handle) noexcept {
    return std::move(handle).close();
}

// Recovering the machine before the protocol ends is the lossy
// direction of the mapping.  The remaining steps the protocol declared
// never happen, and the detach is what tells the handle's destructor
// that the abandonment was deliberate.  The usual reason is that the
// bridge is about to be destroyed.
template <typename Proto, typename State, std::meta::info Edges, typename LoopCtx, AbandonmentPolicy Policy>
    requires(!std::is_same_v<Proto, End>)
[[nodiscard]] constexpr ::fixy::Machine<State, Edges>*
machine_from_session(SessionHandle<Proto, ::fixy::Machine<State, Edges>*, LoopCtx, Policy>&& handle) noexcept {
    ::fixy::Machine<State, Edges>* machine = handle.resource();
    std::move(handle).detach(detach_reason::OwnerLifetimeBoundEarlyExit{});
    return machine;
}

// A state machine whose address is its cross-thread publication
// identity cannot be owned by a bridge: readers on other threads hold
// that address and need explicit acquire ordering on every read.  The
// atomic form therefore borrows the cell rather than owning a machine,
// and the session view is the observation and replay surface over it.
//
// Reads are uniform, so the concept below fixes their shape.  Writes
// are not: each cell decides which transitions are legal and can delete
// the illegal ones under its own diagnostic, so the write helper only
// forwards to the cell's own publish step.

template <typename Cell>
concept AtomicMachineCell = requires(const std::remove_cvref_t<Cell>& cell, std::memory_order order) {
    typename std::remove_cvref_t<Cell>::state_type;
    { cell.load(order) } -> std::same_as<typename std::remove_cvref_t<Cell>::state_type>;
};

// The cell is taken by non-const reference on purpose.  A protocol over
// a cell is written in terms of the transitions the cell publishes, and
// publishing mutates it; minting over a const cell yields a handle
// whose Send steps have no transport that can run.  A reader that only
// wants the current state calls atomic_machine_state, which takes the
// cell by const reference and needs no handle at all.
template <typename Proto, typename Cell>
    requires(AtomicMachineCell<Cell> && WellFormedRunnableProtocol<Proto>)
[[nodiscard]] constexpr auto mint_atomic_session(Cell& cell) noexcept {
    return mint_session_handle<Proto>(&cell);
}

template <typename Cell>
    requires AtomicMachineCell<Cell>
[[nodiscard]] constexpr typename std::remove_cvref_t<Cell>::state_type
atomic_machine_state(const Cell& cell, std::memory_order order = std::memory_order_acquire) noexcept {
    return cell.load(order);
}

// The transport for a Send step over an atomic cell.  The signature is
// the one a handle's send() expects — void(Resource&, Event&&), where
// Resource is the cell pointer the mint stored — so a call site passes
// this directly rather than writing the same lambda at each step.
template <typename Event, AtomicMachineCell Cell>
constexpr void publish_atomic_machine_transition(Cell*& cell, Event&& event) noexcept(
    noexcept(cell->publish_from_session(std::forward<Event>(event), std::memory_order_release)))
    requires requires { cell->publish_from_session(std::forward<Event>(event), std::memory_order_release); }
{
    cell->publish_from_session(std::forward<Event>(event), std::memory_order_release);
}

}  // namespace fixy::session
