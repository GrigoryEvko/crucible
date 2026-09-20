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
// Old spelling: include/crucible/bridges/VigilModeHandle.h.

#include <fixy/session/MachineBridge.h>

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

}  // namespace admitted_mode_transitions

template <Mode From, Mode To>
inline constexpr bool mode_transition_allowed_v =
    ::foundation::fail_closed::Admitted<^^admitted_mode_transitions, mode_tag<From>, mode_tag<To>>;

// A transition as a value the session sends.  The static_assert is what
// a caller sees when it names a pair the relation does not admit.
template <Mode From, Mode To>
struct ModeTransition {
    static_assert(mode_transition_allowed_v<From, To>,
                  "fixy::session::diagnostic [VigilModeBridge_IllegalTransition]: the persistent Vigil mode "
                  "transitions are RECORDING -> COMPILED and COMPILED -> RECORDING, and they are declared as "
                  "edges in fixy::session::vigil_mode::admitted_mode_transitions.  DIVERGED is a replay status "
                  "rather than a persistent mode, so it is neither a source nor a target.  Adding a transition "
                  "means declaring its edge in that namespace, which is also what makes it greppable.");
    static constexpr Mode from = From;
    static constexpr Mode to = To;
};

using ModeRecordingToCompiled = ModeTransition<Mode::RECORDING, Mode::COMPILED>;
using ModeCompiledToRecording = ModeTransition<Mode::COMPILED, Mode::RECORDING>;

// The observer drives this protocol, not the thread that owns the cell.
// It reads the current mode, selects a branch, and either performs one
// typed transition or ends.
using ModeProtocol =
    Loop<Select<Send<ModeRecordingToCompiled, Continue>, Send<ModeCompiledToRecording, Continue>, End>>;

static_assert(is_well_formed_v<ModeProtocol>);

class ModeCell {
    std::atomic<Mode> value_{Mode::RECORDING};

public:
    using state_type = Mode;

    constexpr ModeCell() noexcept = default;

    ModeCell(const ModeCell&) = delete("Vigil mode cell is process-local state");
    ModeCell& operator=(const ModeCell&) = delete("Vigil mode cell is process-local state");
    ModeCell(ModeCell&&) = delete("atomic mode cell is the channel identity");
    ModeCell& operator=(ModeCell&&) = delete("atomic mode cell is the channel identity");

    // The default load and the two direct publishers are relaxed.  They
    // serve the thread that owns the cell, which needs no ordering
    // against itself.  The session-driven publishers default to release
    // instead, because the observer reading through a handle takes the
    // matching acquire.

    [[nodiscard]] Mode load(std::memory_order order = std::memory_order_relaxed) const noexcept {
        return value_.load(order);
    }

    void publish_compiled() noexcept { value_.store(Mode::COMPILED, std::memory_order_relaxed); }

    void publish_recording_after_divergence() noexcept { value_.store(Mode::RECORDING, std::memory_order_relaxed); }

    bool publish_from_session(ModeRecordingToCompiled, std::memory_order order = std::memory_order_release) noexcept {
        value_.store(Mode::COMPILED, order);
        return true;
    }

    bool publish_from_session(ModeCompiledToRecording, std::memory_order order = std::memory_order_release) noexcept {
        value_.store(Mode::RECORDING, order);
        return true;
    }
};

static_assert(sizeof(ModeCell) == sizeof(std::atomic<Mode>));
static_assert(AtomicMachineCell<ModeCell>);

using ModeSessionHandle = decltype(mint_atomic_session<ModeProtocol>(std::declval<ModeCell&>()));

// The handle borrows the cell mutably, which the ported header did not.
// It minted over a `const ModeCell&` and pinned the resource as `const
// ModeCell*`, and that leaves two of ModeProtocol's three branches
// unreachable: the transport a Send branch needs is the cell's
// publish_from_session, both overloads are non-const, and no transport
// a call site could write would compile through a const pointer.  Only
// the End branch is walkable, so the factory hands back a handle for a
// protocol it cannot perform.
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
static_assert(std::is_same_v<typename ModeSessionHandle::resource_type, ModeCell*>);

// The factory is a constrained template rather than a plain function
// taking the cell directly.  The concept pins the parameter to exactly
// this cell, so a call with some other atomic cell fails at the concept
// by name instead of as a substitution failure deep inside the
// substrate.

template <class Cell>
concept CanMintVigilModeBridge = std::same_as<std::remove_cvref_t<Cell>, ModeCell>;

template <class Cell>
    requires CanMintVigilModeBridge<Cell>
[[nodiscard]] constexpr ModeSessionHandle mint_vigil_mode_bridge(Cell& cell) noexcept {
    return mint_atomic_session<ModeProtocol>(cell);
}

}  // namespace fixy::session::vigil_mode
