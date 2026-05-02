#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::safety — Machine ↔ Session bridge (SAFEINT-A10, #399,
//                    misc/24_04_2026_safety_integration.md §10)
//
// Crucible has TWO equally valid ways to model a typestate-bearing
// value:
//
//   * `safety::Machine<State>` — typestate primitive, transitions are
//     local computations (no message exchange, no permission handoff).
//     Single-threaded, ergonomic for in-place mutation.
//
//   * `safety::proto::SessionHandle<Proto, Resource>` — protocol
//     primitive, transitions are observable as wire events (logging,
//     replay, bisimulation).  Participates in a global type G,
//     subject to L7 φ-properties.
//
// Several long-pending Crucible components are borderline: their
// internal logic wants Machine's ergonomics, but external observers
// (Augur metrics broadcast, replay-deterministic bisimulation,
// debug-time protocol introspection) want Session's structural view.
// Examples: Vigil mode (#34, #78); Transaction (#101); Keeper startup;
// FLR recovery (#164); pending_region/pending_activation (#33).
//
// The bridge resolves the false choice: BOTH views over the SAME
// underlying machine.  Internal code mutates via `.machine()`;
// external code reads protocol-level metadata via `.session_view()`
// or the static `protocol_name()` accessor.
//
// ─── Design ─────────────────────────────────────────────────────────
//
//     SessionFromMachine<State, Proto>
//         is a Pinned wrapper that owns a Machine<State> AND can mint
//         a SessionHandle<Proto, Machine<State>*> on demand.  Pinned
//         because the minted handle stores `&machine_`, and any move
//         of the bridge would invalidate the pointer (a use-after-
//         free at the next session step — the very hazard
//         SessionResource_NotPinned (#406) closes for handles
//         themselves).
//
//     machine_from_session<...>(handle)
//         dual operation: given a SessionHandle whose Resource is
//         Machine<State>*, recover the Machine* by consuming the
//         handle.  Two overloads cover the canonical cases:
//             - End-state handle: consume via .close()
//             - Mid-protocol handle: extract pointer + .detach()
//                                     (caller asserts the bridge's
//                                     Machine still exists).
//
// ─── Why "single-party" is not a compile constraint ────────────────
//
// The integration doc §10 reads "SessionFromMachine where Proto is
// single-party".  We do NOT install a SingleParty<Proto> concept
// because the framework has no notion of party count baked in — it's
// a semantic claim about the user's chosen Proto, not a structural
// property of the type system.  A multi-party Proto could in
// principle be wrapped here (the resulting handle would still be a
// valid local-view).  We document the intended use (single-party
// typestate) and let user discipline pick appropriate Protos.
//
// ─── Resolved tasks (per misc/24_04_2026_safety_integration.md §10.3)
//
//   * #34  (Vigil mode_ as safety::Machine) — bridge gives the Machine
//   * #78  (Vigil Mode as Session<...>)      — bridge gives the Session
//     Both ship via SessionFromMachine; they are NOT contradictory.
//   * #101 (Transaction Session<TxStatus, ...>) — bridge for the
//          imperative API + single-party Session for replay/logging.
//   * #164 (Fallback boundary Session type-state) — bridge composes
//          with ScopedView for non-consuming inspection.
//
// These remain separate tasks (Vigil/Transaction/Fallback's actual
// production refactors are §29-§30 work, Epoch IV); this header
// SHIPS THE INFRASTRUCTURE ONLY.
//
// ─── Cost ───────────────────────────────────────────────────────────
//
//   sizeof(SessionFromMachine<State, Proto>) == sizeof(Machine<State>)
//                                            == sizeof(State)
//
// Pinned is empty + EBO-collapsed; Proto is a phantom; the Machine's
// state is the only runtime data.  Each .session_view() call
// constructs a SessionHandle in registers — no allocation, no atomic.
//
// ─── References ────────────────────────────────────────────────────
//
//   misc/24_04_2026_safety_integration.md §10 — design rationale.
//   safety/Machine.h         — typestate primitive.
//   safety/Session.h         — SessionHandle, mint_session_handle,
//                              the SessionResource concept (#406)
//                              that this header upholds via Pinned.
//   safety/Pinned.h          — address-stability mixin.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/safety/Machine.h>
#include <crucible/safety/Pinned.h>
#include <crucible/sessions/Session.h>

#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// ═════════════════════════════════════════════════════════════════════
// ── SessionFromMachine<State, Proto> ───────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Bridge object: owns a Machine<State>, exposes both Machine and
// SessionHandle views.  Pinned for address stability — the minted
// SessionHandle stores `&machine_`, which would dangle on bridge move.

template <typename State, typename Proto>
class [[nodiscard]] SessionFromMachine
    : public Pinned<SessionFromMachine<State, Proto>>
{
    static_assert(safety::proto::is_well_formed_v<Proto>,
        "crucible::session::diagnostic [Protocol_Ill_Formed]: "
        "SessionFromMachine<State, Proto>: Proto must be well-formed "
        "(every Continue must have an enclosing Loop).  See "
        "safety/Session.h's is_well_formed_v for the structural rule.");

    Machine<State> machine_;

public:
    using state_type   = State;
    using machine_type = Machine<State>;
    using protocol     = Proto;

    // The session view's concrete type — the handle's Proto and
    // Resource fully determine its specialisation.  Exposed so callers
    // can name the type without re-deriving it.
    using session_handle_type =
        decltype(safety::proto::mint_session_handle<Proto>(
            std::declval<machine_type*>()));

    // ── Constructors ─────────────────────────────────────────────────
    //
    // Three forms mirror Machine<State>: from a State value, in-place
    // construction, or from an already-built Machine.

    constexpr explicit SessionFromMachine(State s)
        noexcept(std::is_nothrow_move_constructible_v<State>)
        : machine_{std::move(s)} {}

    template <typename... Args>
        requires std::is_constructible_v<State, Args...>
    constexpr explicit SessionFromMachine(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<State, Args...>)
        : machine_{std::in_place, std::forward<Args>(args)...} {}

    constexpr explicit SessionFromMachine(machine_type m) noexcept
        : machine_{std::move(m)} {}

    // Pinned base deletes copy and move; ~SessionFromMachine is the
    // only special member needed and defaults cleanly.
    ~SessionFromMachine() = default;

    // ── Imperative view (Machine) ────────────────────────────────────
    //
    // For ergonomic in-place state mutation.  The Machine's data() /
    // data_mut() methods do not consume the Machine, so the bridge
    // remains in a valid state across these borrows.

    [[nodiscard]] constexpr machine_type& machine() & noexcept {
        return machine_;
    }

    [[nodiscard]] constexpr const machine_type& machine() const & noexcept {
        return machine_;
    }

    // ── Direct state borrow (sugar over machine().data() / data_mut())
    //
    // Common case: callers want the State value, not the wrapping
    // Machine.  Forwards to Machine's borrowing API.

    [[nodiscard]] constexpr const State& state() const & noexcept {
        return machine_.data();
    }

    [[nodiscard]] constexpr State& state_mut() & noexcept {
        return machine_.data_mut();
    }

    // ── Protocol view (SessionHandle) ────────────────────────────────
    //
    // Mints a fresh SessionHandle<Proto, Machine<State>*> on each
    // call.  The handle is move-only and consumed on protocol
    // advance; calling .session_view() again gives a NEW handle.
    // This is intentional — the bridge is the resource anchor; the
    // handle is a typed lens for one observable transition (logging,
    // replay-step, bisimulation check).
    //
    // Discipline: ONE active handle per bridge at a time.  Concurrent
    // handles would race on the underlying Machine's state; the
    // framework does not enforce this — the caller's responsibility
    // is the same as for any &-borrow of the Machine.

    [[nodiscard]] auto session_view() & noexcept -> session_handle_type {
        return safety::proto::mint_session_handle<Proto>(&machine_);
    }

    // ── Static accessor — protocol name without instantiating ────────
    //
    // Forwards to SessionHandle's protocol_name() (#379).  Useful when
    // a caller needs the human-readable protocol shape but does not
    // want to mint a session handle (e.g., logging the bridge's
    // identity in a startup banner before any state mutation).
    //
    // CAVEAT — `constexpr` capture across TU boundaries.  The string
    // returned is derived from __PRETTY_FUNCTION__, which is COMDAT-
    // deduplicated by the linker across TUs that instantiate the same
    // type_name<Proto> specialisation.  Capturing the result into a
    // constexpr string_view at one TU and performing runtime find()
    // on it can resolve to a different COMDAT instance than the one
    // the constexpr captured — observable as printf("%.*s") rendering
    // the right bytes but find() returning npos (the find walks a
    // different copy).  Treat protocol_name() as a RUNTIME helper
    // and call it without `constexpr` capture for substring matching;
    // use the static_assert form (.size() > 0, type identity) for
    // pure compile-time checks where the constexpr value never
    // materialises at runtime.

    [[nodiscard]] static constexpr std::string_view protocol_name() noexcept {
        return safety::proto::detail::type_name<Proto>();
    }

    // ── Terminal extraction ──────────────────────────────────────────
    //
    // Consume the bridge, yielding the underlying State value.  After
    // this, the bridge is moved-from; both .machine() and
    // .session_view() are no longer valid.  Pinned forbids the bridge
    // itself from being moved, so .extract() is the sole legitimate
    // path to recover the State.

    [[nodiscard]] constexpr State extract() &&
        noexcept(std::is_nothrow_move_constructible_v<State>)
    {
        return std::move(machine_).extract();
    }
};

// ═════════════════════════════════════════════════════════════════════
// ── machine_from_session — recover Machine* from a SessionHandle ────
// ═════════════════════════════════════════════════════════════════════
//
// Dual operation: given a SessionHandle whose Resource is
// Machine<State>* (the shape produced by SessionFromMachine), recover
// the underlying Machine*.  Two overloads cover the two reachable
// states:
//
//   1. End-state handle: consumed via .close(), which yields the
//      Resource (the pointer).  Most common case for one-shot
//      protocols that ran to completion.
//
//   2. Mid-protocol handle: extracted via .resource() + .detach()
//      (the caller is intentionally abandoning the protocol;
//      typically because the bridge that owns the Machine is about
//      to be destroyed and .detach() suppresses the abandonment
//      diagnostic).  Caller asserts the bridge still exists when
//      this overload is invoked; the returned pointer remains valid
//      for the bridge's lifetime.
//
// Returned Machine* is a non-owning borrow.  The caller's discipline:
// the SessionFromMachine that minted the handle must outlive every
// Machine* extracted from it — same contract as the handle's own
// Resource pointer.

// End-state overload: clean consume via .close().
template <typename State, typename LoopCtx>
[[nodiscard]] constexpr Machine<State>* machine_from_session(
    safety::proto::SessionHandle<safety::proto::End,
                                  Machine<State>*,
                                  LoopCtx>&& sh) noexcept
{
    return std::move(sh).close();
}

// Mid-protocol overload: extract pointer + detach (abandons protocol).
//
// The detach()-on-abandon discipline is the framework's reviewable
// escape hatch for "I am intentionally dropping this handle without
// reaching End/Stop" — the destructor's abandonment check sees the
// consumed flag and does not abort.  This overload routes any
// non-End SessionHandle through that escape hatch in one step.
template <typename Proto, typename State, typename LoopCtx>
    requires (!std::is_same_v<Proto, safety::proto::End>)
[[nodiscard]] constexpr Machine<State>* machine_from_session(
    safety::proto::SessionHandle<Proto, Machine<State>*, LoopCtx>&& sh) noexcept
{
    Machine<State>* p = sh.resource();
    std::move(sh).detach(safety::proto::detach_reason::OwnerLifetimeBoundEarlyExit{});
    return p;
}

// ═════════════════════════════════════════════════════════════════════
// ── Zero-cost guarantees (release-mode) ────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// The bridge is pure indirection over Machine<State>; in release
// builds it must add ZERO bytes beyond the Machine itself, which in
// turn is zero bytes beyond the State.  Verified via static_assert
// for representative State sizes.

#ifdef NDEBUG
namespace detail::msb_release_size_test {

struct OneByteState   { char x; };
struct FourByteState  { int  x; };
struct EightByteState { double x; };

using Proto1 = safety::proto::End;
using Proto2 = safety::proto::Loop<safety::proto::Send<int, safety::proto::Continue>>;

static_assert(sizeof(SessionFromMachine<OneByteState, Proto1>)
              == sizeof(OneByteState),
    "Release-mode SessionFromMachine must add zero bytes beyond State.");

static_assert(sizeof(SessionFromMachine<FourByteState, Proto2>)
              == sizeof(FourByteState),
    "Release-mode SessionFromMachine must add zero bytes beyond State.");

static_assert(sizeof(SessionFromMachine<EightByteState, Proto1>)
              == sizeof(EightByteState),
    "Release-mode SessionFromMachine must add zero bytes beyond State.");

}  // namespace detail::msb_release_size_test
#endif

// ═════════════════════════════════════════════════════════════════════
// ── Framework self-test static_asserts ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Verify the bridge's structural properties at header-inclusion time.
// Runtime behaviour exercised by test/test_machine_session_bridge.cpp.

namespace detail::msb_self_test {

// Fixture: a typestate carrying a Vigil-style mode plus a counter.
struct VigilModeState {
    enum class Mode : uint8_t { Idle, Recording, Replaying, Serving };
    Mode      mode    = Mode::Idle;
    uint32_t  ticks   = 0;
};

// A single-party protocol matching the Vigil mode-transition graph
// shape: an unbounded loop offering one of four mode-transition
// branches per tick.  Real Vigil shapes will refine this; the bridge
// works with any well-formed Proto.
using VigilProto = safety::proto::Loop<
    safety::proto::Select<
        safety::proto::Send<int, safety::proto::Continue>,
        safety::proto::End>>;

using Bridge = SessionFromMachine<VigilModeState, VigilProto>;

// Pinned discipline: copy / move both deleted via Pinned base.
static_assert(!std::is_copy_constructible_v<Bridge>);
static_assert(!std::is_move_constructible_v<Bridge>);
static_assert(!std::is_copy_assignable_v<Bridge>);
static_assert(!std::is_move_assignable_v<Bridge>);

// Inheritance witness: Bridge IS-A Pinned<Bridge>.
static_assert(std::is_base_of_v<Pinned<Bridge>, Bridge>);

// Public typedefs are correctly wired.
static_assert(std::is_same_v<typename Bridge::state_type,   VigilModeState>);
static_assert(std::is_same_v<typename Bridge::machine_type, Machine<VigilModeState>>);
static_assert(std::is_same_v<typename Bridge::protocol,     VigilProto>);

// session_handle_type is the SessionHandle specialisation produced
// by mint_session_handle<Proto>(&machine_).  Loop unrolls one step
// at construction, so the resulting handle's compile-time Proto is
// the loop body (a Select), with Loop<...> as the LoopCtx.
using ExpectedSession = safety::proto::SessionHandle<
    safety::proto::Select<
        safety::proto::Send<int, safety::proto::Continue>,
        safety::proto::End>,
    Machine<VigilModeState>*,
    VigilProto>;
static_assert(std::is_same_v<typename Bridge::session_handle_type,
                              ExpectedSession>);

}  // namespace detail::msb_self_test

}  // namespace crucible::safety
