#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::vigil_mode — Vigil mode-cell + Session bridge (fixy-H-23)
//
// Standalone header carrying ONLY the type-level machinery that
// `crucible::Vigil` exposes as its publicly-observable mode bridge:
// the Mode enum, the two legal ModeTransition typestates, the
// ModeCell that owns the atomic, the ModeProtocol that types
// foreign observation, and the ModeSessionHandle alias that
// fixy::bridge re-exports.
//
// ─── Why this header exists ────────────────────────────────────────
//
// `crucible/Vigil.h` is the run-time hub (1.1 KLoC).  It transitively
// pulls BackgroundThread, MerkleDag, Cipher, perf::Senses,
// warden::DeadlineWatchdog, and the substrate's full hot-path
// dependency closure.  Pre-fixy-H-23, `fixy/Bridge.h` had to include
// Vigil.h SOLELY because `mint_vigil_mode_bridge` returned the
// nested `Vigil::ModeSessionHandle` type — every consumer of the
// fixy bridge umbrella paid the full Vigil compile cost.
//
// This header carves the Mode machinery out of `Vigil.h`'s class
// scope into the standalone namespace `crucible::vigil_mode`.  The
// types and the `mint_vigil_mode_bridge(const ModeCell&)` factory
// depend on nothing heavier than the MachineSessionBridge substrate
// itself (which pulls only safety/Machine, safety/Pinned, and
// sessions/Session).  Vigil.h keeps backward-compat `using` aliases
// inside `class Vigil` so call sites like `Vigil::Mode` and
// `Vigil::ModeSessionHandle` continue to resolve identically.
//
// ─── Axiom coverage ────────────────────────────────────────────────
//
//   InitSafe — Mode has the typical RECORDING-start NSDMI on the
//              atomic; the cell zero-inits the atomic to RECORDING.
//   TypeSafe — Mode is `enum class : uint8_t`; ModeTransition<From,To>
//              tags a single statically-allowed transition.  The
//              consteval `mode_transition_allowed` predicate rejects
//              every other (From, To) pair with a structured
//              static_assert message.
//   NullSafe — `mint_vigil_mode_bridge(const ModeCell&)` takes a
//              reference; the substrate `mint_atomic_session` takes
//              `&cell` and the handle stores a non-null pointer.
//   MemSafe  — ModeCell is non-copy non-move (Pinned by deletion);
//              the cell is the channel identity.
//   BorrowSafe — single producer (the holder of the ModeCell, i.e.
//              Vigil) writes via publish_*; observers read via
//              load(acquire) through the SessionHandle.
//   ThreadSafe — atomic store/load with acquire/release at the
//              session boundary; relaxed at the hot-path
//              `Vigil::mode()` reader (documented).
//   LeakSafe — no heap; ModeCell is sizeof(std::atomic<Mode>);
//              ModeSessionHandle is a thin pointer wrapper.
//   DetSafe  — same input event sequence → same final mode;
//              acquire/release ordering preserves replay.
//
// ─── §XXI Universal Mint Pattern compliance ─────────────────────────
//
//   `mint_vigil_mode_bridge(const ModeCell&)` is a TOKEN MINT
//   (authority derives from the cell that owns the atomic, not from
//   a ctx parameter).  Per §XXI: `[[nodiscard]] constexpr noexcept`,
//   the `requires`-equivalent gate is structural (the parameter is
//   the strongly-typed ModeCell — no other ctor matches), the return
//   type is the concrete `ModeSessionHandle`.  Vigil.h adds an
//   overload `mint_vigil_mode_bridge(const Vigil&)` for the
//   convenience of callers that already hold a Vigil; both overloads
//   live in `crucible::` via a using-declaration here.
//
// ─── Inclusion cost (vs the pre-fixy-H-23 baseline) ────────────────
//
//   Old: <crucible/Vigil.h>             ~1125 LOC + DAG + Cipher + perf
//   New: <crucible/bridges/VigilModeHandle.h>  ~140 LOC
//        which pulls MachineSessionBridge.h ~580 LOC
//        which pulls safety/{Machine,Pinned} + sessions/Session.
//
//   Bridge.h consumers no longer pay the Vigil hub cost.  The Vigil
//   hub itself still owns the runtime — it just exposes the mode
//   machinery as a thin standalone surface.

#include <crucible/bridges/MachineSessionBridge.h>
#include <crucible/sessions/Session.h>

#include <atomic>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::vigil_mode {

// ─── Mode enum + the two legal transitions ─────────────────────────
//
// RECORDING ↔ COMPILED are the two persistent modes.  DIVERGED is a
// transient replay status — never a persistent mode and never a
// legal transition target through this protocol.
enum class Mode : uint8_t {
    RECORDING,
    COMPILED,
    DIVERGED,
};

[[nodiscard]] consteval bool mode_transition_allowed(Mode from,
                                                     Mode to) noexcept {
    return (from == Mode::RECORDING && to == Mode::COMPILED)
        || (from == Mode::COMPILED && to == Mode::RECORDING);
}

template <Mode From, Mode To>
struct ModeTransition {
    static_assert(mode_transition_allowed(From, To),
        "crucible::vigil::diagnostic "
        "[VigilModeBridge_IllegalTransition]: persistent Vigil mode "
        "transitions are RECORDING -> COMPILED and COMPILED -> "
        "RECORDING only. DIVERGED is a replay status, not a persistent "
        "mode; SERVING/REPLAYING are stale design-doc modes.");
    static constexpr Mode from = From;
    static constexpr Mode to   = To;
};

using ModeRecordingToCompiled =
    ModeTransition<Mode::RECORDING, Mode::COMPILED>;
using ModeCompiledToRecording =
    ModeTransition<Mode::COMPILED, Mode::RECORDING>;

// ─── Session-typed protocol over the cell ──────────────────────────
//
// Loop<Select<Send<R→C>, Send<C→R>, End>>: a cold observer (Keeper
// telemetry, replay bisimulator) reads the cell's state through the
// SessionHandle, picks one of the three branches, and either drives
// a typed transition (Send) or terminates (End).
using ModeProtocol = safety::proto::Loop<safety::proto::Select<
    safety::proto::Send<ModeRecordingToCompiled, safety::proto::Continue>,
    safety::proto::Send<ModeCompiledToRecording, safety::proto::Continue>,
    safety::proto::End>>;

static_assert(safety::proto::is_well_formed_v<ModeProtocol>);

// ─── ModeCell — atomic carrier ─────────────────────────────────────
//
// Pinned by deletion: the atomic IS the channel identity; any move
// would invalidate every SessionHandle that captured `&cell`.
class ModeCell {
    std::atomic<Mode> value_{Mode::RECORDING};

public:
    using state_type = Mode;

    constexpr ModeCell() noexcept = default;

    ModeCell(const ModeCell&)            = delete("Vigil mode cell is process-local state");
    ModeCell& operator=(const ModeCell&) = delete("Vigil mode cell is process-local state");
    ModeCell(ModeCell&&)                 = delete("atomic mode cell is the channel identity");
    ModeCell& operator=(ModeCell&&)      = delete("atomic mode cell is the channel identity");

    [[nodiscard]] Mode load(std::memory_order order = std::memory_order_relaxed)
        const noexcept
    {
        return value_.load(order);
    }

    void publish_compiled() noexcept {
        value_.store(Mode::COMPILED, std::memory_order_relaxed);
    }

    void publish_recording_after_divergence() noexcept {
        value_.store(Mode::RECORDING, std::memory_order_relaxed);
    }

    bool publish_from_session(
        ModeRecordingToCompiled,
        std::memory_order order = std::memory_order_release) noexcept
    {
        value_.store(Mode::COMPILED, order);
        return true;
    }

    bool publish_from_session(
        ModeCompiledToRecording,
        std::memory_order order = std::memory_order_release) noexcept
    {
        value_.store(Mode::RECORDING, order);
        return true;
    }
};

static_assert(sizeof(ModeCell) == sizeof(std::atomic<Mode>));
static_assert(safety::AtomicMachineCell<ModeCell>);

// ─── ModeSessionHandle alias ───────────────────────────────────────
//
// Names the concrete SessionHandle returned by
// `safety::mint_atomic_session<ModeProtocol>(modecell)`.  fixy
// consumers reach this alias without including Vigil.h.
using ModeSessionHandle = decltype(
    safety::mint_atomic_session<ModeProtocol>(
        std::declval<const ModeCell&>()));

static_assert(std::is_same_v<
    typename ModeSessionHandle::resource_type, const ModeCell*>);

// ─── §XXI Universal Mint Pattern — token mint ──────────────────────
//
// Synthesizes a fresh authoritative ModeSessionHandle whose
// authority derives from the carrier ModeCell.  No ctx parameter
// (token-mint flavor): the cell IS the authority anchor.
//
// FIXY-V-020: gated on CanMintVigilModeBridge<Cell> (§XXI single-
// concept rule) — pins Cell to ModeCell at concept-evaluation time
// so a wrong-cell call (e.g. an unrelated AtomicMachineCell) is
// rejected with a clean concept-violation diagnostic, not a deep
// substitution failure through mint_atomic_session.

template <class Cell>
concept CanMintVigilModeBridge =
    std::same_as<std::remove_cvref_t<Cell>, ModeCell>;

template <class Cell>
    requires CanMintVigilModeBridge<Cell>
[[nodiscard]] constexpr ModeSessionHandle mint_vigil_mode_bridge(
    Cell const& cell) noexcept
{
    return safety::mint_atomic_session<ModeProtocol>(cell);
}

}  // namespace crucible::vigil_mode

namespace crucible {

// Lift the ModeCell-taking factory into ::crucible:: so the existing
// `using ::crucible::mint_vigil_mode_bridge;` re-export in
// `fixy/Bridge.h` resolves without requiring Vigil.h.  Vigil.h adds
// a second overload (taking `const Vigil&`) — both overloads live in
// the same namespace and ADL/overload-resolution selects per
// argument type.
using vigil_mode::mint_vigil_mode_bridge;

}  // namespace crucible
