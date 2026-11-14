#pragma once

// A machine and a session are two views of one typestate value.  A
// machine transition is a local computation.  A session step is an
// event an observer can log, replay or compare.  A bridge owns one
// machine and runs a session over it on demand, so the same state is
// driven imperatively from inside and read structurally from outside.
//
// A view is a borrow of the machine, and the bridge lends it to one body
// at a time.  with_view runs Proto over a MachineLease, a move-only
// Resource that reaches the machine and nothing else of the bridge.  The
// body is a function object with no state, so it holds nothing of its
// caller: not the bridge, so no second view and no extract while the view
// lives.  The view ends when the body returns, and only then can the
// caller extract the state.  A body still reaches an object of static
// storage by its name, and a bridge with static storage is outside this
// guarantee.
//
// The bridge is pinned because a lease holds the address of its machine.
// Moving the bridge would leave the address at freed storage, and Pinned
// deletes the move.
//
// Proto is not constrained to a single party.  Party count is a claim
// about the protocol a caller picked, not a structural property of any
// type here, so a concept asserting it would reject multi-party
// protocols whose local view is a perfectly valid session over this
// machine.  Single-party typestate is the intended use and is left to
// caller discipline.

#include <fixy/Machine.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>

#include <foundation/Lifetime.h>
#include <foundation/Pinned.h>

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

// The Resource of one view: the lease of the machine of one bridge for
// the span of the view.  It reaches the state of the machine, and nothing
// else of the bridge: not the machine itself, which an extract would
// consume.  It is move-only, so it is an owned SessionResource, and no
// copy of it reaches the machine a second time.  Only the bridge builds
// one.
template <typename State, std::meta::info Edges = ^^::fixy::machine::no_edges>
class [[nodiscard]] MachineLease {
    ::fixy::Machine<State, Edges>* machine_ = nullptr;
    [[no_unique_address]] MoveOnlyResource move_only_{};
    // No byte route builds a lease, so no lease reaches a machine that no
    // bridge lent.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    template <typename FState, typename FProto, std::meta::info FEdges>
    friend class SessionFromMachine;

    constexpr explicit MachineLease(::fixy::Machine<State, Edges>& machine) noexcept : machine_{&machine} {}

public:
    // A view drives the machine of its bridge and no peer, so each choice
    // of a view puts no label (Local choices in fixy/session/Handle.h).
    static constexpr Network session_network = Network::Local;

    [[nodiscard]] constexpr const State& state() const& noexcept { return machine_->data(); }
    [[nodiscard]] constexpr State& state_mut() & noexcept { return machine_->data_mut(); }
};

// The gate of a view: the gate of with_session over the lease, and a body
// with no state, which holds nothing of its caller.  A body that captures
// the bridge could open a second view of it or extract its state while the
// view lives, so the gate refuses each body that captures.
template <typename Ctx, typename Proto, typename Lease, typename Body>
concept CtxFitsMachineView = __is_empty(Body) && CtxFitsSessionBody<Ctx, Proto, Lease, DefaultAbandonmentPolicy, Body>;

// The bridge's one door.  fixy::Machine made its own constructor
// private and put a mint in front of it for the same reason: the
// admission — here, that Proto can actually carry a runnable handle —
// belongs at one call site that a reader can enumerate, not at every
// constructor a caller might reach.
//
// Args build the State.  The machine's relation namespace defaults to
// fixy::machine::no_edges, exactly as it does for a machine minted
// directly.
template <typename Proto, typename State, std::meta::info Edges = ^^::fixy::machine::no_edges, typename... Args>
    requires WellFormedRunnableProtocol<Proto> && std::is_constructible_v<State, Args...>
[[nodiscard]] constexpr auto
mint_session_from_machine(Args&&... args) noexcept(std::is_nothrow_constructible_v<State, Args...>)
    -> SessionFromMachine<State, Proto, Edges>;

template <typename State, typename Proto, std::meta::info Edges = ^^::fixy::machine::no_edges>
class [[nodiscard]] SessionFromMachine : public ::foundation::Pinned<SessionFromMachine<State, Proto, Edges>> {
    static_assert(is_well_formed_v<Proto>, "fixy::session::diagnostic [Protocol_Ill_Formed]: "
                                           "SessionFromMachine<State, Proto>: Proto must be well-formed "
                                           "under is_well_formed_v.  Every Continue must have an "
                                           "enclosing Loop.");

    ::fixy::Machine<State, Edges> machine_;
    // No byte route builds a bridge, so no bridge stands over a protocol
    // that its mint did not admit.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

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
    using lease_type = MachineLease<State, Edges>;
    static constexpr std::meta::info edges = Edges;
    using row_discipline = ::fixy::row_discipline::session_from_machine<Proto>;
    using row_payload = machine_type;

    ~SessionFromMachine() = default;

    [[nodiscard]] constexpr machine_type& machine() & noexcept { return machine_; }

    [[nodiscard]] constexpr const machine_type& machine() const& noexcept { return machine_; }

    [[nodiscard]] constexpr const State& state() const& noexcept { return machine_.data(); }

    [[nodiscard]] constexpr State& state_mut() & noexcept { return machine_.data_mut(); }

    // Runs Proto over a lease of the machine inside the body, and ends the
    // lease when the body returns the End handle (with_session of
    // fixy/session/Entry.h).  The gate is the gate of with_session, so the
    // context admits the effect row of each payload of Proto, and the body
    // has no state, so it holds no second door to this bridge.  One view
    // lives at a time, and none lives after the call.  No code of the tree
    // throws, so the view is noexcept for each body.
    template <typename Ctx, typename Body>
        requires CtxFitsMachineView<Ctx, Proto, MachineLease<State, Edges>, Body>
    constexpr void with_view(Ctx const& ctx, Body body) & noexcept {
        static_cast<void>(with_session<Proto, DefaultAbandonmentPolicy, Ctx, lease_type>(ctx, lease_type{machine_},
                                                                                         static_cast<Body&&>(body)));
    }

    [[nodiscard]] static constexpr std::string_view protocol_name() noexcept { return type_display_name_v<Proto>; }

    // The bridge cannot be moved, so this is the only way the state
    // leaves it.  No view lives outside with_view, so no view sees the
    // bridge after the extract.
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

// A state machine whose address is its cross-thread publication
// identity cannot be owned by a bridge: readers on other threads hold
// that address and need explicit acquire ordering on every read.  The
// atomic form therefore borrows the cell rather than owning a machine,
// and the session view is the observation and replay surface over it.
//
// Reads are uniform, so the concept below fixes their shape: a load with
// no argument, which the cell does with acquire order.  Writes
// are not: each cell decides which transitions are legal and can delete
// the illegal ones under its own diagnostic, so the write helper only
// forwards to the cell's own publish step.
//
// The address of a cell is its identity, so the concept also asks that
// the cell derive from foundation::Pinned.  A handle then holds the cell
// by lvalue reference, and no copy of a pointer to it exists.

template <typename Cell>
concept AtomicMachineCell =
    std::derived_from<std::remove_cvref_t<Cell>, ::foundation::Pinned<std::remove_cvref_t<Cell>>>
    && requires(const std::remove_cvref_t<Cell>& cell) {
           typename std::remove_cvref_t<Cell>::state_type;
           { cell.load() } -> std::same_as<typename std::remove_cvref_t<Cell>::state_type>;
       };

// The cell is taken by non-const reference on purpose.  A protocol over
// a cell is written in terms of the transitions the cell publishes, and
// publishing mutates it; minting over a const cell yields a handle
// whose Send steps have no transport that can run.  A reader that only
// wants the current state calls atomic_machine_state, which takes the
// cell by const reference and needs no handle at all.
//
// The gate is the gate of mint_session over the reference to the cell, so
// the context admits the effect row of each payload of Proto.
template <typename Ctx, typename Proto, typename Cell>
concept CtxFitsAtomicSession = AtomicMachineCell<Cell> && CtxFitsSession<Ctx, Proto, Cell&>;

template <typename Proto, typename Ctx, typename Cell>
    requires CtxFitsAtomicSession<Ctx, Proto, Cell>
[[nodiscard]] constexpr auto mint_atomic_session(Ctx const& ctx, Cell& cell) noexcept {
    return mint_session<Proto, DefaultAbandonmentPolicy, Ctx, Cell&>(ctx, cell);
}

template <typename Cell>
    requires AtomicMachineCell<Cell>
[[nodiscard]] constexpr typename std::remove_cvref_t<Cell>::state_type atomic_machine_state(const Cell& cell) noexcept {
    return cell.load();
}

// The transport for a Send step over an atomic cell.  The signature is
// the try-write that a handle's send() accepts, bool(Resource&, Event&),
// where Resource is the reference to the cell that the mint stored.  The
// result is the answer of the cell: true when the cell took the event.
// A cell that answers false makes the handle wait through the watch and
// try again, so a cell must answer false only when a later try can
// succeed.  The cell chooses the memory order of its publication, because
// the order is part of what the cell promises its observers.
template <typename Event, AtomicMachineCell Cell>
[[nodiscard]] constexpr bool
publish_atomic_machine_transition(Cell& cell,
                                  Event& event) noexcept(noexcept(cell.publish_from_session(std::move(event))))
    requires requires {
        { cell.publish_from_session(std::move(event)) } -> std::same_as<bool>;
    }
{
    return cell.publish_from_session(std::move(event));
}

}  // namespace fixy::session
