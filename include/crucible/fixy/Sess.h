#pragma once

// ── crucible::fixy::sess — Session minters under fixy:: ────────────
//
// Phase C re-export per misc/16_05_2026_fixy.md §4.  Stable surface
// names for the binary + MPST session-type combinators under
// `crucible::safety::proto::*` plus the canonical mint factories
// from `sessions/SessionMint.h`.
//
// **Purely additive.**  No new types, no logic.  The `using`
// declarations are name-lookup directives only; every name in
// `fixy::sess::` IS the safety::proto::* symbol it aliases.
//
// **Mint factories** per CLAUDE.md §XXI Universal Mint Pattern:
//   - `mint_permissioned_session<Proto>(ctx, resource, perms...)`
//   - `mint_channel<Proto>(ctx_a, ctx_b, res_a, res_b)`
//   - `mint_session_handle<Proto>(resource)`  (token mint)
//   - `mint_recording_session(handle, ...)`   (bridge wrap)
//   - `mint_crash_watched_session(handle, ...)` (bridge wrap)
//
// Federation 3-role projection:
//   - `mint_sender<KeyTag>(role_id)`
//   - `mint_receiver<KeyTag>(role_id)`
//   - `mint_coord<KeyTag>(role_id)`
//   - `mint_channel<KeyTag>(role_id_a, role_id_b)`  (federation overload)
//
// ── References ──────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §4 Phase C
//   sessions/Session.h            Send/Recv/Select/Offer/Loop/Continue/End
//   sessions/SessionCrash.h       Stop_g, Stop = Stop_g<Abort>, CrashClass
//   sessions/SessionDelegate.h    Delegate, Accept, EpochedDelegate
//   sessions/SessionCheckpoint.h  CheckpointedSession
//   sessions/SessionMint.h        mint_permissioned_session, mint_channel
//   sessions/FederationProtocol.h mint_sender/receiver/coord (federation)
//   bridges/CrashTransport.h      mint_crash_watched_session
//   bridges/RecordingSessionHandle.h  mint_recording_session

#include <crucible/bridges/CrashTransport.h>
#include <crucible/bridges/RecordingSessionHandle.h>
#include <crucible/sessions/FederationProtocol.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCheckpoint.h>
#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionDelegate.h>
#include <crucible/sessions/SessionMint.h>

namespace crucible::fixy::sess {

// ═════════════════════════════════════════════════════════════════════
// ── Core protocol combinators (from sessions/Session.h) ────────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::safety::proto::Send;
using ::crucible::safety::proto::Recv;
using ::crucible::safety::proto::Select;
using ::crucible::safety::proto::Offer;
using ::crucible::safety::proto::Sender;
using ::crucible::safety::proto::AnonymousPeer;
using ::crucible::safety::proto::Loop;
using ::crucible::safety::proto::Continue;
using ::crucible::safety::proto::End;
using ::crucible::safety::proto::VendorPinned;

// ═════════════════════════════════════════════════════════════════════
// ── Crash-stop family (from sessions/SessionCrash.h) ───────────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::safety::proto::Stop_g;
using ::crucible::safety::proto::Stop;
using ::crucible::safety::proto::CrashClass;

// ═════════════════════════════════════════════════════════════════════
// ── Delegate / Accept / EpochedDelegate (sessions/SessionDelegate.h) ─
// ═════════════════════════════════════════════════════════════════════

using ::crucible::safety::proto::Delegate;
using ::crucible::safety::proto::Accept;
using ::crucible::safety::proto::EpochedDelegate;

// ═════════════════════════════════════════════════════════════════════
// ── Checkpoint (sessions/SessionCheckpoint.h) ──────────────────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::safety::proto::CheckpointedSession;

// ═════════════════════════════════════════════════════════════════════
// ── Recording / crash-watched handle re-exports ────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Wraps a SessionHandle with SessionEventLog event emission
// (Recording*) or Stop_g<C> peer-crash propagation (CrashWatched*).
// Both orthogonal to the protocol layer; compose with the typed-
// session stack via fixy::sess.

using ::crucible::safety::proto::RecordingSessionHandle;
using ::crucible::safety::proto::CrashWatchedHandle;

// ═════════════════════════════════════════════════════════════════════
// ── Mint factories (CLAUDE.md §XXI Universal Mint Pattern) ─────────
// ═════════════════════════════════════════════════════════════════════
//
// Note: bare `mint_session<Proto>(ctx, resource)` is `=delete`d in
// sessions/SessionMint.h — production code uses
// `mint_permissioned_session<Proto>(ctx, resource, perms...)` for
// both the empty-PermSet shim AND the non-empty form.  The deleted
// declaration is re-exported so stale call sites surface the
// canonical diagnostic via the fixy:: path.

using ::crucible::safety::proto::mint_session;
using ::crucible::safety::proto::mint_permissioned_session;
using ::crucible::safety::proto::mint_channel;
using ::crucible::safety::proto::mint_session_handle;
using ::crucible::safety::proto::mint_recording_session;
using ::crucible::safety::proto::mint_crash_watched_session;

// ═════════════════════════════════════════════════════════════════════
// ── Federation 3-role projection (FederationProtocol.h) ────────────
// ═════════════════════════════════════════════════════════════════════
//
// `mint_sender<KeyTag>(role_id)` / `mint_receiver` / `mint_coord` are
// the per-role tag mints; `mint_channel<KeyTag>(...)` is the paired-
// role channel mint (lives in `crucible::sessions::federation` rather
// than `crucible::safety::proto`).

namespace federation = ::crucible::safety::proto::federation;
using federation::mint_sender;
using federation::mint_receiver;
using federation::mint_coord;
// Note: federation::mint_channel name-collides with proto::mint_channel
// above when both are introduced into the same namespace; we leave
// federation's channel mint reachable via `fixy::sess::federation::mint_channel`
// to keep the surface unambiguous.

// ═════════════════════════════════════════════════════════════════════
// ── Self-test — identity check for the re-export ──────────────────
// ═════════════════════════════════════════════════════════════════════

namespace self_test {

static_assert(std::is_same_v<Send<int, End>,
                             ::crucible::safety::proto::Send<int, End>>,
    "fixy::sess::Send alias must be identical to safety::proto::Send.");

static_assert(std::is_same_v<Recv<int, End>,
                             ::crucible::safety::proto::Recv<int, End>>,
    "fixy::sess::Recv alias must be identical to safety::proto::Recv.");

static_assert(std::is_same_v<Loop<End>,
                             ::crucible::safety::proto::Loop<End>>,
    "fixy::sess::Loop alias must be identical to safety::proto::Loop.");

static_assert(std::is_same_v<Stop, ::crucible::safety::proto::Stop>,
    "fixy::sess::Stop alias must be identical to safety::proto::Stop.");

static_assert(std::is_same_v<End, ::crucible::safety::proto::End>,
    "fixy::sess::End alias must be identical to safety::proto::End.");

static_assert(std::is_same_v<Continue, ::crucible::safety::proto::Continue>,
    "fixy::sess::Continue alias must be identical to safety::proto::Continue.");

static_assert(std::is_same_v<
    EpochedDelegate<Send<int, End>, End, 0, 0>,
    ::crucible::safety::proto::EpochedDelegate<
        ::crucible::safety::proto::Send<int, ::crucible::safety::proto::End>,
        ::crucible::safety::proto::End, 0, 0>>,
    "fixy::sess::EpochedDelegate alias must be identical to "
    "safety::proto::EpochedDelegate.");

}  // namespace self_test

}  // namespace crucible::fixy::sess
