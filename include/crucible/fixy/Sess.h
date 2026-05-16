#pragma once

// ── crucible::fixy — Sess.h (FIXY-C, alias re-export of sessions/) ─────
//
// Stable surface names for the binary session-type combinators that
// ship under `crucible::safety::proto::*`.  Greenfield code that
// composes sessions through the fixy:: discipline uses
// `fixy::sess::*` so the spelling matches the discipline surface
// while the substrate header tree (`sessions/`) is the authoritative
// definition site.
//
// **Purely additive.**  No new types, no logic, no extra
// instantiations beyond the existing safety::proto::* family.  The
// `using` declarations are inert: every name in `fixy::sess::` IS
// the safety::proto::* symbol by name lookup.
//
// **Mint factories.**  Per CLAUDE.md §XXI Universal Mint Pattern,
// the mint family from `sessions/SessionMint.h` is re-exported
// alongside the protocol combinators — `mint_session`,
// `mint_channel`, `mint_permissioned_session` — so a greenfield site
// authoring a typed session does it through one fixy:: namespace.
//
// ── Surface ──────────────────────────────────────────────────────────
//
//   namespace fixy::sess {
//     // Core protocol combinators
//     using Send;
//     using Recv;
//     using Select;
//     using Offer;
//     using Sender;
//     using Loop;
//     using Continue;
//     using End;
//     using Stop;
//
//     // Delegate / Accept (higher-order session delegation)
//     using Delegate;
//     using Accept;
//
//     // Checkpoint (re-entrant cancel + replay)
//     using CheckpointedSession;
//
//     // Crash-stop family (BSYZ22)
//     using Stop_g;
//     using CrashClass;
//
//     // Mint factories (per CLAUDE.md §XXI)
//     using mint_session;            // ctx-bound, EmptyPermSet
//     using mint_permissioned_session;  // ctx-bound, non-empty PermSet
//     using mint_channel;            // paired-Ctx receiver-side
//   }
//
// ── References ──────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §4 Phase C — alias re-export deliverable
//   sessions/Session.h                  — Send/Recv/Select/Offer/Loop/Continue/End
//   sessions/SessionCrash.h             — Stop_g, Stop = Stop_g<Abort>
//   sessions/SessionDelegate.h          — Delegate, Accept
//   sessions/SessionCheckpoint.h        — CheckpointedSession
//   sessions/SessionMint.h              — mint_session, mint_channel, mint_permissioned_session

#include <crucible/bridges/CrashTransport.h>
#include <crucible/bridges/RecordingSessionHandle.h>
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
// ── Delegate / Accept (from sessions/SessionDelegate.h) ────────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::safety::proto::Delegate;
using ::crucible::safety::proto::Accept;

// ═════════════════════════════════════════════════════════════════════
// ── Checkpoint (from sessions/SessionCheckpoint.h) ─────────────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::safety::proto::CheckpointedSession;

// ═════════════════════════════════════════════════════════════════════
// ── Delegate epoch-versioned variant (sessions/SessionDelegate.h) ──
// ═════════════════════════════════════════════════════════════════════
//
// EpochedDelegate<T, K, MinEpoch, MinGeneration> is the canonical
// delegated session form for Canopy reshard-aware membership
// protocols: the delegated continuation only fires if the surrounding
// LoopCtx's epoch/generation lattice admits MinEpoch/MinGeneration.

using ::crucible::safety::proto::EpochedDelegate;

// ═════════════════════════════════════════════════════════════════════
// ── Recording / crash-watched handle re-exports ────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// RecordingSessionHandle wraps a SessionHandle with SessionEventLog
// event emission for replay / debugging.  CrashWatchedHandle wraps a
// SessionHandle with Stop_g<C> peer-crash propagation.  Both are
// orthogonal to the protocol layer and compose with the typed-session
// stack — production code that wires either through fixy::fn::Fn
// instantiates the handle template directly through fixy::sess.

using ::crucible::safety::proto::RecordingSessionHandle;
using ::crucible::safety::proto::CrashWatchedHandle;

// ═════════════════════════════════════════════════════════════════════
// ── Mint factories (CLAUDE.md §XXI Universal Mint Pattern) ─────────
// ═════════════════════════════════════════════════════════════════════
//
// Note: `mint_session<Proto>(ctx, resource)` is `=delete`d in
// sessions/SessionMint.h — production code uses
// `mint_permissioned_session<Proto>(ctx, resource, perms...)` for the
// empty-PermSet shim AND the non-empty form alike.  The deleted
// declarations stay re-exported so stale call sites surface the
// canonical diagnostic via the fixy namespace path.

using ::crucible::safety::proto::mint_session;
using ::crucible::safety::proto::mint_permissioned_session;
using ::crucible::safety::proto::mint_channel;
using ::crucible::safety::proto::mint_session_handle;
using ::crucible::safety::proto::mint_recording_session;
using ::crucible::safety::proto::mint_crash_watched_session;

// ═════════════════════════════════════════════════════════════════════
// ── Self-test — identity check for the re-export ──────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Every alias compiles iff it names the corresponding substrate type.
// The static_asserts below pin the round-trip: fixy::sess::X IS
// safety::proto::X by template-name equality.

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

// EpochedDelegate identity check — Phase C re-export.
static_assert(std::is_same_v<
    EpochedDelegate<Send<int, End>, End, 0, 0>,
    ::crucible::safety::proto::EpochedDelegate<
        ::crucible::safety::proto::Send<int, ::crucible::safety::proto::End>,
        ::crucible::safety::proto::End, 0, 0>>,
    "fixy::sess::EpochedDelegate alias must be identical to "
    "safety::proto::EpochedDelegate.");

}  // namespace self_test

}  // namespace crucible::fixy::sess
