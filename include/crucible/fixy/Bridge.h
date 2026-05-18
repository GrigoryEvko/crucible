#pragma once

// ── crucible::fixy::bridge — Bridge minters under fixy:: ───────────
//
// Phase C re-export per misc/16_05_2026_fixy.md.  Surfaces the
// failure-recovery / persistence / mode-bridge wrap factories under
// `fixy::bridge::` so callers who include only the fixy umbrella
// never have to descend into the bridges/ tree to wrap a handle.
//
// Per CLAUDE.md §XXI Universal Mint Pattern: every re-export
// preserves the substrate's wrap-shape concept gate (the handle
// parameter dictates whether the wrap is permitted, surfaced via
// SFINAE/concept overload resolution), the `[[nodiscard]]
// constexpr noexcept` qualifiers, and the bridge composition
// algebra (Recording over Crash-watched over PermissionedSession).
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   safety::proto::mint_recording_session(handle, ...)
//   safety::proto::mint_crash_watched_session(handle, ...)
//   safety::proto::mint_persisted_session(ctx, handle, cipher, ...)
//   bridges::mint_recording_endpoint(handle, ...)
//   bridges::mint_crash_watched_endpoint(handle, ...)
//   crucible::mint_vigil_mode_bridge(vigil)
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — re-exports introduce no new state path.
//   TypeSafe — using-declarations preserve the substrate's concept
//              gates (IsSessionHandle, PeerTag deduction, etc.).
//   NullSafe — wrapped handles inherit substrate's pointer discipline.
//   MemSafe  — allocation discipline differs per minter (see below);
//              every alloc is owned via RAII whose destructor runs.
//   BorrowSafe — wrap algebra preserves linearity of inner handle.
//   ThreadSafe — log/flag pointers obey substrate's atomic discipline.
//   LeakSafe — wrappers' destructor runs inner handle's destructor;
//              for the allocating minter (mint_persisted_session)
//              this transitively closes the SessionPersistenceState
//              owned via std::unique_ptr.
//   DetSafe  — bit-exact composition; pure wrap.
//
// ── Allocation discipline (fixy-A4-010) ───────────────────────────
//
// Three classes of mint behavior in this header — `grep make_unique`
// in bridges/SessionPersistence.h returns the canonical heap sites:
//
//   1. Pure-wrap minters (zero heap, EBO-collapsible):
//      mint_recording_session, mint_crash_watched_session,
//      mint_recording_endpoint, mint_crash_watched_endpoint,
//      mint_vigil_mode_bridge.
//      Compose handles via using-declarations + struct-by-value;
//      sizeof(result) == sizeof(inner) + O(byte) for the wrap tag.
//
//   2. Allocating minter (one heap alloc per mint, RAII-owned):
//      mint_persisted_session.
//      Performs `std::make_unique<SessionPersistenceState<CallerRow>>`
//      at the mint boundary (bridges/SessionPersistence.h:701/755/812)
//      because the persistence state owns a SessionEventLog drain ring
//      whose lifetime must exceed the inner RecordingSessionHandle's.
//      The unique_ptr is moved into PersistedSessionHandle; the
//      handle's destructor runs the state's destructor (flush + close).
//      MemSafe via LeakSafe-RAII, NOT via "no heap".
//
// Hot-path callers (per-op record / per-op recv) MUST use class 1.
// Class 2 is a Cipher cold-tier roll-forward mint — appropriate at
// session-establish time, not in the per-message loop.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Class 1: zero. using-declarations are pure name-lookup directives.
// Class 2: one heap alloc per mint_persisted_session call (plus
//          whatever SessionPersistenceState's ctor does — typically
//          one ring-buffer allocation + Cipher::OpenView capture).

// ── Include discipline (FIXY-AUDIT-C5) ─────────────────────────────
//
// All five substrate headers are pulled in directly, per CLAUDE.md
// §XV "Self-contained. Every header compiles standalone. Add
// required includes directly; never rely on transitive pull-in."
//
// `<crucible/Vigil.h>` is STRUCTURALLY REQUIRED for the
// `mint_vigil_mode_bridge` re-export below: the substrate signature
// returns `Vigil::ModeSessionHandle`, a nested type whose definition
// (`decltype(safety::mint_atomic_session<ModeProtocol>(...))`)
// cannot be named through a forward declaration.  A `class Vigil;`
// forward decl is insufficient because (a) the return type names a
// nested member, and (b) the substrate function is `inline` with its
// body in Vigil.h, so any caller that *invokes* the re-export still
// needs the complete definition for the call-expression to resolve.
// Forward-declaring the function with `auto` deduction is also a
// non-option — `auto` requires the definition for deduction.
//
// The four `bridges/*` headers (CrashTransport, EndpointMint,
// RecordingSessionHandle, SessionPersistence) are each required
// directly because they declare distinct mint factories surfaced
// under the fixy::bridge:: namespace — none subsumes another.
// fixy-A2-014: SessionPersistence.h no longer transitively pulls Cipher.h;
// fixy::bridge:: re-exports mint_persisted_session and its companions,
// so the umbrella restores the convenience pull.
#include <crucible/Cipher.h>
#include <crucible/Vigil.h>
#include <crucible/bridges/CrashTransport.h>
#include <crucible/bridges/EndpointMint.h>
#include <crucible/bridges/RecordingSessionHandle.h>
#include <crucible/bridges/SessionPersistence.h>

namespace crucible::fixy::bridge {

// ═════════════════════════════════════════════════════════════════════
// ── Recording session — wrap with SessionEventLog emission ─────────
// ═════════════════════════════════════════════════════════════════════
//
// `mint_recording_session(handle, log, self, peer)` returns a
// RecordingSessionHandle<Proto, Resource, Log> that emits structured
// events on every Send/Recv for replay / debugging.

using ::crucible::safety::proto::mint_recording_session;
using ::crucible::safety::proto::RecordingSessionHandle;

// ═════════════════════════════════════════════════════════════════════
// ── Crash-watched session — wrap with Stop_g<C> peer-crash gate ────
// ═════════════════════════════════════════════════════════════════════
//
// `mint_crash_watched_session<PeerTag>(handle, flag)` returns a
// CrashWatchedHandle<Proto, Resource, PeerTag, LoopCtx> that
// observes peer.crashed() before every Recv and surfaces Stop_g<C>
// when set.

using ::crucible::safety::proto::mint_crash_watched_session;
using ::crucible::safety::proto::CrashWatchedHandle;

// ═════════════════════════════════════════════════════════════════════
// ── Persisted session — Cipher cold-tier roll-forward wrapper ──────
// ═════════════════════════════════════════════════════════════════════
//
// `mint_persisted_session(ctx, handle, cipher, ...)` composes
// RecordingSessionHandle with Cipher::OpenView so every recorded
// event is persisted for crash-recovery replay across the
// reincarnation boundary.  Bare overloads without (ctx, cipher) are
// `=delete`d with diagnostics.

using ::crucible::safety::proto::mint_persisted_session;

// ═════════════════════════════════════════════════════════════════════
// ── Endpoint-side wraps (bridges/EndpointMint.h) ───────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Sister factories that wrap the underlying Endpoint's handle rather
// than a free SessionHandle.  Useful when the substrate is owned by
// a Pipeline stage that already minted Endpoints — wrap once at the
// endpoint construction site rather than at every Stage body entry.

using ::crucible::bridges::mint_recording_endpoint;
using ::crucible::bridges::mint_crash_watched_endpoint;

// ═════════════════════════════════════════════════════════════════════
// ── Vigil mode bridge (Vigil.h) ────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// `mint_vigil_mode_bridge(vigil)` exposes the Vigil's mode-tracking
// SessionHandle for cold observers (Keeper telemetry, Canopy
// recovery probes).  Documents the read-only access discipline at
// the bridge level; production hot path never calls this.

using ::crucible::mint_vigil_mode_bridge;

}  // namespace crucible::fixy::bridge
