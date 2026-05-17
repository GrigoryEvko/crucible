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
#include <crucible/sessions/SessionPatterns.h>

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
// ── φ-predicate re-exports (FIXY-AUDIT-B5) ─────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// The FX paper §11.18 catalogs seven session-safety levels: safe, df
// (deadlock-free), term (terminating), nterm (non-terminating live),
// live, live+ (positive liveness), live++ (precise liveness).  The
// substrate ships a partial set today: `is_well_formed_v` is the
// strongest soundness predicate in production use, and the crash-stop
// family (`is_dual_v`, `is_terminal_state_v`, `is_stop_v`,
// `is_crash_v`, `is_reliable_v`) covers the orthogonal crash axis.
// The remaining φ-predicates (true deadlock-freedom, termination
// proof, positive liveness, precise liveness) live as substrate gaps
// tracked in Task #346 / #348 / #381 per CLAUDE.md L0 §Safety wrappers.
//
// Production callers spelling `fixy::sess::phi_<name>_v<Proto>` get the
// strongest predicate available today.  When the substrate ships a
// dedicated predicate per the gap TODOs, the alias here flips to the
// substrate name with no call-site churn.
//
// Coverage map (substrate → fixy::sess::phi_*):
//
//   safe      → is_well_formed_v        — Honda 1998 binary session
//                                          well-formedness (the
//                                          strongest predicate today)
//   df        → is_well_formed_v        — TODO(FIXY-AUDIT-B5-substrate):
//                                          phi_df awaits a dedicated
//                                          deadlock-freedom proof from
//                                          sessions/SessionDeadlockFree.h
//                                          (Task #346 / GAPS-002).
//                                          Aliased to is_well_formed_v
//                                          as the conservative floor.
//   term      → is_terminal_state_v     — terminal-state check; not the
//                                          full Honda 1998 termination
//                                          proof but the closest
//                                          substrate predicate.
//                                          TODO(FIXY-AUDIT-B5-substrate):
//                                          dedicated termination
//                                          predicate awaits Task #348.
//   nterm     → !is_terminal_state_v    — complement of term.
//                                          TODO(FIXY-AUDIT-B5-substrate):
//                                          dedicated non-termination
//                                          predicate awaits Task #348.
//   live      → is_well_formed_v        — TODO(FIXY-AUDIT-B5-substrate):
//                                          phi_live awaits a coinductive
//                                          liveness proof (Task #381).
//   live_plus → is_well_formed_v        — TODO(FIXY-AUDIT-B5-substrate):
//                                          phi_live_plus awaits the
//                                          positive-liveness refinement
//                                          (GPPSY23, Task #381).
//   live_pp   → is_well_formed_v        — TODO(FIXY-AUDIT-B5-substrate):
//                                          phi_live_pp awaits the
//                                          precise-async refinement
//                                          (PMY25, Task #381).
//
// Crash-stop family (BSYZ22 / BHYZ23) re-exported verbatim under
// `fixy::sess::is_*_v` for production callers that need them
// alongside the φ-predicate aliases.

template <typename P>
inline constexpr bool phi_safe_v =
    ::crucible::safety::proto::is_well_formed_v<P>;

template <typename P>
inline constexpr bool phi_df_v =
    ::crucible::safety::proto::is_well_formed_v<P>;

template <typename P>
inline constexpr bool phi_term_v =
    ::crucible::safety::proto::is_terminal_state_v<P>;

template <typename P>
inline constexpr bool phi_nterm_v =
    !::crucible::safety::proto::is_terminal_state_v<P>;

template <typename P>
inline constexpr bool phi_live_v =
    ::crucible::safety::proto::is_well_formed_v<P>;

template <typename P>
inline constexpr bool phi_live_plus_v =
    ::crucible::safety::proto::is_well_formed_v<P>;

template <typename P>
inline constexpr bool phi_live_pp_v =
    ::crucible::safety::proto::is_well_formed_v<P>;

// ─── Crash-stop family (BSYZ22 / BHYZ23) ──────────────────────────
//
// These ship as first-class substrate predicates; aliasing them under
// `fixy::sess::` keeps the surface coherent for production callers.

using ::crucible::safety::proto::is_well_formed_v;
using ::crucible::safety::proto::is_terminal_state_v;
using ::crucible::safety::proto::is_dual_v;

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
// ── Verified session patterns (FIXY-AUDIT-C6) ──────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// SessionPatterns.h ships ~24 user-facing protocol aliases under
// `safety::proto::pattern::*`.  Each alias names a structurally-verified
// session shape (RequestResponse, Pipeline, Transaction, FanOut/In,
// MPMC, 2PC, SWIM, Handshake) plus its dual and any crash-aware /
// delegate-compatible refinement.  Re-export them under
// `fixy::sess::pattern::` so the fixy surface is structurally complete:
// production callers spell `fixy::sess::pattern::RequestResponse_Client<Req,
// Resp>` and the type IS `safety::proto::pattern::RequestResponse_Client`.
//
// All 24 baseline aliases ship today.  No new aliases introduced; this
// is pure name-lookup re-export.  Crash-aware refinements (CrashAware*)
// + delegate-compatibility contracts live in SessionPatterns.h's self-
// test section under `pattern::detail::`; those are not user-facing and
// are NOT re-exported here.

// One namespace alias makes every name in `safety::proto::pattern::*`
// reachable via `fixy::sess::pattern::*` with identical type identity
// (verified in the sentinel TU).  Coverage map:
//
//   Request/response (6):  RequestResponseOnce_{Client,Server},
//                          RequestResponse_{Client,Server},
//                          RequestResponseLoop_{Client,Server}
//   Pipeline (3):          PipelineSource, PipelineSink, PipelineStage
//   Transaction (2):       Transaction_{Client,Server}
//   Fan-out / Fan-in (4):  FanOut, FanIn, Broadcast, ScatterGather
//   MPMC (2):              MpmcProducer, MpmcConsumer
//   2PC (2):               TwoPhaseCommit_{Coord,Follower}
//   SWIM (2):              SwimProbe_{Client,Server}
//   Handshake (2):         Handshake_{Client,Server}
//   Contract markers (8):  CrashSafetyVerified/Pending, DelegateCompatible/
//                          Pending, BaselinePatternNeedsCrashAwareVariant,
//                          PatternHasNoDelegateBoundaryConstraints,
//                          PatternCrashSafety, PatternDelegateCompatibility
//   Predicates (4):        pattern_crash_safety_{verified,pending}_v,
//                          pattern_delegate_{compatible,pending}_v
//
// Crash-aware refinements (CrashAware*) live in the substrate's self-
// test scope under pattern::detail::pattern_self_test::; they are not
// user-facing and not re-exported.

namespace pattern = ::crucible::safety::proto::pattern;

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
