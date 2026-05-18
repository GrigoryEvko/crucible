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
//   MemSafe  — bridges are pure-wrappers; no heap.
//   BorrowSafe — wrap algebra preserves linearity of inner handle.
//   ThreadSafe — log/flag pointers obey substrate's atomic discipline.
//   LeakSafe — wrappers' destructor runs inner handle's destructor.
//   DetSafe  — bit-exact composition; pure wrap.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  using-declarations are pure name-lookup directives.

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
