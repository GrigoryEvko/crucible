#pragma once

// The Vigil runtime's mode, as a cell one thread owns and a session an
// observer drives.
//
// The mode machinery sits outside the class that owns the Vigil
// runtime, so that observing a mode costs nothing but this header.
// Nested in that class the names would read more naturally, which is
// the alternative rejected here: the returned handle type would then be
// a nested type, and every consumer of the mode bridge would compile
// the whole runtime hub and its dependency closure to name it.
//
// Old spelling: include/crucible/bridges/_VigilModeHandle.h.

#include <fixy/session/MachineBridge.h>

#include <foundation/Platform.h>
#include <foundation/diag/FailClosed.h>

#include <atomic>
#include <concepts>
#include <cstdint>
#include <type_traits>

namespace fixy::session::vigil_mode {

enum class Mode : std::uint8_t {
    RECORDING,
    COMPILED,
    DIVERGED,
};

// A mode as a type, so the legal transitions can be a fail-closed
// relation rather than a hand-written disjunction.  The ported header
// answered the question with
//
//     (from == RECORDING && to == COMPILED) || (from == COMPILED && to == RECORDING)
//
// which is closed but not enumerable: a reader cannot ask the relation
// what it admits, and a fourth Mode added later is silently absent from
// every clause of the expression rather than visibly missing an edge.
template <Mode M>
struct mode_tag {
    static constexpr Mode value = M;
};

// The relation.  Every legal mode transition in the tree is one line
// here, and a grep for `edge<mode_tag` finds all of them.  DIVERGED
// appears in neither direction because it is a replay status rather
// than a persistent mode — that is now a fact about this namespace's
// contents instead of an omission from a boolean expression.
namespace admitted_mode_transitions {

inline constexpr ::foundation::fail_closed::edge<mode_tag<Mode::RECORDING>, mode_tag<Mode::COMPILED>>
    recording_to_compiled{};

inline constexpr ::foundation::fail_closed::edge<mode_tag<Mode::COMPILED>, mode_tag<Mode::RECORDING>>
    compiled_to_recording{};

// Every read counts the members against this seal, so a member that
// another file adds stops the build rather than widening the relation.
inline constexpr ::foundation::fail_closed::seal sealed{.members = 2};
}  // namespace admitted_mode_transitions

template <Mode From, Mode To>
inline constexpr bool mode_transition_allowed_v =
    ::foundation::fail_closed::Admitted<^^admitted_mode_transitions, mode_tag<From>, mode_tag<To>>;

// A transition as a value the session sends.  Each admitted edge has one
// plain class, so the payload walk of fixy/concurrent/PayloadRow.h reads
// it as a class with no member, and its effect row is empty.
struct ModeRecordingToCompiled {
    static constexpr Mode from = Mode::RECORDING;
    static constexpr Mode to = Mode::COMPILED;
};

struct ModeCompiledToRecording {
    static constexpr Mode from = Mode::COMPILED;
    static constexpr Mode to = Mode::RECORDING;
};

namespace detail {

// The class of the edge From -> To.  The static_assert is what a caller
// sees when it names a pair that the relation does not admit.
template <Mode From, Mode To>
struct mode_transition {
    static_assert(mode_transition_allowed_v<From, To>,
                  "fixy::session::diagnostic [VigilModeBridge_IllegalTransition]: the persistent Vigil mode "
                  "transitions are RECORDING -> COMPILED and COMPILED -> RECORDING, and they are declared as "
                  "edges in fixy::session::vigil_mode::admitted_mode_transitions.  DIVERGED is a replay status "
                  "rather than a persistent mode, so it is neither a source nor a target.  Adding a transition "
                  "means declaring its edge in that namespace, which is also what makes it greppable.");
    using type = std::conditional_t<From == Mode::RECORDING, ModeRecordingToCompiled, ModeCompiledToRecording>;
    static_assert(!mode_transition_allowed_v<From, To> || (type::from == From && type::to == To),
                  "each admitted edge of admitted_mode_transitions has its own transition class");
};

}  // namespace detail

template <Mode From, Mode To>
using ModeTransition = typename detail::mode_transition<From, To>::type;

// The observer drives this protocol, not the thread that owns the cell.
// It reads the current mode, selects a branch, and either performs one
// typed transition or ends.
using ModeProtocol =
    Loop<Select<Send<ModeRecordingToCompiled, Continue>, Send<ModeCompiledToRecording, Continue>, End>>;

static_assert(is_well_formed_v<ModeProtocol>);

class ModeCell : public ::foundation::Pinned<ModeCell> {
    std::atomic<Mode> value_{Mode::RECORDING};

public:
    using state_type = Mode;

    // A session over the cell moves the cell and no peer, so each choice
    // puts no label (Local choices in fixy/session/Handle.h).
    static constexpr Network session_network = Network::Local;

    constexpr ModeCell() noexcept = default;

    ModeCell(const ModeCell&) = delete("Vigil mode cell is process-local state");
    ModeCell& operator=(const ModeCell&) = delete("Vigil mode cell is process-local state");
    ModeCell(ModeCell&&) = delete("atomic mode cell is the channel identity");
    ModeCell& operator=(ModeCell&&) = delete("atomic mode cell is the channel identity");

    // The owner of the cell publishes with release stores, and each load
    // is an acquire, so an observer that reads a mode also sees what the
    // owner wrote before it published that mode.  A session moves the
    // cell along one admitted edge with a compare-and-swap from the source
    // of the edge.  The cell holds one of two modes, so a compare that
    // fails finds the target.  A cell in a third mode stops the process at
    // the compare, and no session writes over it.

    [[nodiscard]] Mode load() const noexcept { return value_.load(std::memory_order_acquire); }

    void publish_compiled() noexcept { value_.store(Mode::COMPILED, std::memory_order_release); }

    void publish_recording_after_divergence() noexcept { value_.store(Mode::RECORDING, std::memory_order_release); }

    // Each returns true: the cell then stands at the target of the edge.
    bool publish_from_session(ModeRecordingToCompiled) noexcept { return take_edge_(Mode::RECORDING, Mode::COMPILED); }

    bool publish_from_session(ModeCompiledToRecording) noexcept { return take_edge_(Mode::COMPILED, Mode::RECORDING); }

private:
    bool take_edge_(Mode from, Mode to) noexcept {
        Mode seen = from;
        if (!value_.compare_exchange_strong(seen, to, std::memory_order_acq_rel, std::memory_order_acquire)) {
            CRUCIBLE_FATAL_INVARIANT(seen == to);
        }
        return true;
    }
};

static_assert(sizeof(ModeCell) == sizeof(std::atomic<Mode>));
static_assert(AtomicMachineCell<ModeCell>);

using ModeSessionHandle = ::fixy::session::detail::first_handle_t<ModeProtocol, ModeCell&, DefaultAbandonmentPolicy>;

// The handle borrows the cell mutably, which the ported header did not.
// It minted over a `const ModeCell&` and pinned the resource as `const
// ModeCell*`, and that leaves two of ModeProtocol's three branches
// unreachable: the transport a Send branch needs is the cell's
// publish_from_session, both overloads are non-const, and no transport
// a call site could write would compile through a const pointer.  Only
// the End branch is walkable, so the factory hands back a handle for a
// protocol it cannot perform.  The Resource is a reference to the
// Pinned cell, not a pointer, so no copy of it reaches the cell.
//
// The frozen tree's own test shows the split: through
// mint_vigil_mode_bridge it takes branch 2 and closes, and where it
// actually publishes the two transitions it drops to the generic
// mint_atomic_session over a non-const cell.  Taking the cell by
// non-const reference here removes the split — one factory, and the
// protocol it names is the protocol it can drive.
//
// Observing the mode does not need a handle and stays const: that is
// atomic_machine_state(cell), which takes a const reference.
static_assert(std::is_same_v<typename ModeSessionHandle::resource_type, ModeCell&>);

// The factory is a constrained template rather than a plain function
// taking the cell directly.  The concept pins the parameter to exactly
// this cell, so a call with some other atomic cell fails at the concept
// by name instead of as a substitution failure deep inside the
// substrate.  It also asks the gate of mint_atomic_session of the
// context.

template <class Ctx, class Cell>
concept CtxFitsVigilModeBridge =
    std::same_as<std::remove_cvref_t<Cell>, ModeCell> && CtxFitsAtomicSession<Ctx, ModeProtocol, Cell>;

template <class Ctx, class Cell>
    requires CtxFitsVigilModeBridge<Ctx, Cell>
[[nodiscard]] constexpr ModeSessionHandle mint_vigil_mode_bridge(Ctx const& ctx, Cell& cell) noexcept {
    return mint_atomic_session<ModeProtocol>(ctx, cell);
}

}  // namespace fixy::session::vigil_mode
