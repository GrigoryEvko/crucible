#pragma once

// The mode machinery sits outside the class that owns the Vigil runtime, so
// that observing a mode costs nothing but this header.  Nested in that class
// the names would read more naturally, which is the alternative rejected here:
// the returned handle type would then be a nested type, and every consumer of
// the mode bridge would compile the whole runtime hub and its dependency
// closure to name it.  The hub keeps aliases, so the nested spellings still
// resolve.

#include <crucible/bridges/MachineSessionBridge.h>
#include <crucible/sessions/Session.h>

#include <atomic>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::vigil_mode {

enum class Mode : uint8_t {
    RECORDING,
    COMPILED,
    DIVERGED,
};

[[nodiscard]] consteval bool mode_transition_allowed(Mode from, Mode to) noexcept {
    return (from == Mode::RECORDING && to == Mode::COMPILED) || (from == Mode::COMPILED && to == Mode::RECORDING);
}

template <Mode From, Mode To>
struct ModeTransition {
    static_assert(mode_transition_allowed(From, To), "crucible::vigil::diagnostic "
                                                     "[VigilModeBridge_IllegalTransition]: the persistent Vigil mode "
                                                     "transitions are RECORDING -> COMPILED and COMPILED -> "
                                                     "RECORDING. DIVERGED is a replay status rather than a "
                                                     "persistent mode, so it is neither a source nor a target.");
    static constexpr Mode from = From;
    static constexpr Mode to = To;
};

using ModeRecordingToCompiled = ModeTransition<Mode::RECORDING, Mode::COMPILED>;
using ModeCompiledToRecording = ModeTransition<Mode::COMPILED, Mode::RECORDING>;

// The observer drives this protocol, not the thread that owns the cell.  It
// reads the current mode, selects a branch, and either performs one typed
// transition or ends.
using ModeProtocol = safety::proto::Loop<
    safety::proto::Select<safety::proto::Send<ModeRecordingToCompiled, safety::proto::Continue>,
                          safety::proto::Send<ModeCompiledToRecording, safety::proto::Continue>, safety::proto::End>>;

static_assert(safety::proto::is_well_formed_v<ModeProtocol>);

class ModeCell {
    std::atomic<Mode> value_{Mode::RECORDING};

public:
    using state_type = Mode;

    constexpr ModeCell() noexcept = default;

    ModeCell(const ModeCell&) = delete("Vigil mode cell is process-local state");
    ModeCell& operator=(const ModeCell&) = delete("Vigil mode cell is process-local state");
    ModeCell(ModeCell&&) = delete("atomic mode cell is the channel identity");
    ModeCell& operator=(ModeCell&&) = delete("atomic mode cell is the channel identity");

    // The default load and the two direct publishers are relaxed.  They serve
    // the thread that owns the cell, which needs no ordering against itself.
    // The session-driven publishers default to release instead, because the
    // observer reading through a handle takes the matching acquire.

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
static_assert(safety::AtomicMachineCell<ModeCell>);

using ModeSessionHandle = decltype(safety::mint_atomic_session<ModeProtocol>(std::declval<const ModeCell&>()));

static_assert(std::is_same_v<typename ModeSessionHandle::resource_type, const ModeCell*>);

// The factory is a constrained template rather than a plain function taking
// the cell directly.  The concept pins the parameter to exactly this cell, so
// a call with some other atomic cell fails at the concept by name instead of
// as a substitution failure deep inside the substrate.

template <class Cell>
concept CanMintVigilModeBridge = std::same_as<std::remove_cvref_t<Cell>, ModeCell>;

template <class Cell>
    requires CanMintVigilModeBridge<Cell>
[[nodiscard]] constexpr ModeSessionHandle mint_vigil_mode_bridge(Cell const& cell) noexcept {
    return safety::mint_atomic_session<ModeProtocol>(cell);
}

}  // namespace crucible::vigil_mode

namespace crucible {

// The factory is lifted into the parent namespace so a consumer that
// re-exports it by that name resolves without the runtime hub.  The hub adds a
// second overload there, taking the runtime object itself, and overload
// resolution picks between them by argument type.
using vigil_mode::mint_vigil_mode_bridge;

}  // namespace crucible
