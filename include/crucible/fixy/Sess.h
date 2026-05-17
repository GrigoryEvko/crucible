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
#include <crucible/safety/Diagnostic.h>
#include <crucible/sessions/FederationProtocol.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCheckpoint.h>
#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionDelegate.h>
#include <crucible/sessions/SessionMint.h>
#include <crucible/sessions/SessionPatterns.h>

#include <string_view>
#include <type_traits>
#include <utility>

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
// ── φ-predicate re-exports (FIXY-AUDIT-B5, fixy-CR-12) ─────────────
// ═════════════════════════════════════════════════════════════════════
//
// The FX paper §11.18 catalogs seven session-safety levels: safe, df
// (deadlock-free), term (terminating), nterm (non-terminating live),
// live, live+ (positive liveness), live++ (precise liveness).  Of
// those, only THREE map honestly to the predicates the substrate
// proves today:
//
//   safe   → is_well_formed_v       — Honda 1998 binary session
//                                      well-formedness.  Equivalent
//                                      to FX phi_safe by definition.
//   term   → is_terminal_state_v    — terminal-state check on the
//                                      protocol head.  The closest
//                                      substrate predicate to FX
//                                      phi_term, sound but partial.
//   nterm  → !is_terminal_state_v   — complement of term.
//
// fixy-CR-12 closure: pre-CR-12 this header also re-exported
// `phi_df_v`, `phi_live_v`, `phi_live_plus_v`, `phi_live_pp_v` — all
// four ALIASED to `is_well_formed_v` even though well-formedness is
// strictly weaker than each of those FX-paper properties (a well-
// formed protocol can still deadlock or starve).  The names lied:
// callers reading `phi_df_v<Proto>` plausibly believed the predicate
// witnessed deadlock-freedom, but it accepted protocols that visibly
// deadlock under any reasonable execution semantics.  CR-12 removes
// those four aliases entirely — no backwards-compat shim — so the
// floor `is_well_formed_v` must be spelled out at every call site
// that wants it.
//
// When the substrate ships dedicated predicates (Task #346 for df,
// #348 for term/nterm refinement, #381 for live/live_plus/live_pp),
// re-introduce the φ-predicates here as actual aliases over those
// substrate names — at which point the names will match the proofs.

template <typename P>
inline constexpr bool phi_safe_v =
    ::crucible::safety::proto::is_well_formed_v<P>;

template <typename P>
inline constexpr bool phi_term_v =
    ::crucible::safety::proto::is_terminal_state_v<P>;

template <typename P>
inline constexpr bool phi_nterm_v =
    !::crucible::safety::proto::is_terminal_state_v<P>;

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
// ── FixyMintSessionRemoved — structured deletion diagnostic ────────
// ═════════════════════════════════════════════════════════════════════
//
// FIXY-AUDIT-B6.  Bare `mint_session<Proto>(...)` was `= delete`d
// (see sessions/SessionMint.h line 950-961) — production code uses
// `mint_permissioned_session<Proto>(ctx, resource, perms...)` for
// both the empty-PermSet shim AND the non-empty form.  The plain
// `= delete("...")` message that GCC emits is non-classified: it does
// not participate in `safety::diag::is_diagnostic_class_v` and is not
// reachable via `safety::diag::diagnostic_name_v<...>`.  This tag
// provides the structured surface so:
//
//   1. `grep FixyMintSessionRemoved` finds every deletion site, the
//      diagnostic tag, and the reachability test in one query.
//   2. `safety::diag::diagnostic_name_v<diag::FixyMintSessionRemoved>`
//      yields the canonical name string for introspection (used by
//      future tooling that walks structured diagnostics).
//   3. The tag inherits `safety::diag::tag_base`, so it participates
//      in `is_diagnostic_class_v` exactly like the 28-entry Catalog
//      tags AND the per-axis `FixyNotEngaged_*` tags.
//
// The tag does NOT enter the closed `safety::diag::Catalog` tuple /
// `Category` enum (those are foundation-internal, append-only, and
// touching them is a coordinated change per FOUND-E01 §Extension
// policy).  It is a fixy-local user-defined tag, identical model to
// the 20 `FixyNotEngaged_*` tags in fixy/Reject.h.

namespace diag {

struct FixyMintSessionRemoved final
    : ::crucible::safety::diag::tag_base {
    static constexpr ::std::string_view name = "FixyMintSessionRemoved";
    static constexpr ::std::string_view description =
        "Bare `mint_session<Proto>(ctx, resource)` and "
        "`mint_session<Proto>(resource)` are `= delete`d in "
        "sessions/SessionMint.h.  Production code constructs typed "
        "session handles via `mint_permissioned_session<Proto>(ctx, "
        "resource, perms...)`, which threads CSL Permission tokens "
        "through the protocol's position so the local row-flow "
        "closure check fires per FOUND-C v1.  The bare mint_session "
        "spelling was structurally unable to carry the permission "
        "evolution and was removed.";
    static constexpr ::std::string_view remediation =
        "Replace the call site with "
        "`mint_permissioned_session<Proto>(ctx, resource, perms...)`.  "
        "For protocols that do not transfer wire-level permissions, "
        "spell the call as "
        "`mint_permissioned_session<Proto>(ctx, resource)` — the "
        "perms pack is variadic and the empty-PermSet shim is the "
        "current default surface (sessions/SessionMint.h:935).";
};

static_assert(::crucible::safety::diag::is_diagnostic_class_v<
                  FixyMintSessionRemoved>,
    "FixyMintSessionRemoved must inherit safety::diag::tag_base.");

}  // namespace diag

// ═════════════════════════════════════════════════════════════════════
// ── Mint factories (CLAUDE.md §XXI Universal Mint Pattern) ─────────
// ═════════════════════════════════════════════════════════════════════
//
// Note: bare `mint_session<Proto>(ctx, resource)` is `=delete`d in
// sessions/SessionMint.h — production code uses
// `mint_permissioned_session<Proto>(ctx, resource, perms...)` for
// both the empty-PermSet shim AND the non-empty form.  The deleted
// declaration is re-exported so stale call sites surface the
// canonical diagnostic via the fixy:: path.  The structured
// diagnostic tag for the removal lives at `fixy::sess::diag::
// FixyMintSessionRemoved` (FIXY-AUDIT-B6).

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

// ── mint_channel name-collision discipline (FIXY-AUDIT-B10) ────────
//
// Two `mint_channel` factories live in the substrate:
//
//   1. `crucible::safety::proto::mint_channel<Proto>(
//          ctx_a, ctx_b, res_a, res_b)`
//      — session-protocol channel mint
//        (sessions/SessionMint.h:980).  Pairs two resource endpoints
//        through a dual-typed Proto into a Session<Proto, A> +
//        Session<dual<Proto>, B> pair under ctx-bound row-admission.
//        Re-exported above as `fixy::sess::mint_channel` (the primary
//        spelling — callers see this when they type
//        `fixy::sess::mint_channel<MyProto>(...)`).
//
//   2. `crucible::safety::proto::federation::mint_channel<KeyTag>(
//          ctx, sender_ep, receiver_ep)`
//      — federation 3-role channel mint
//        (sessions/FederationProtocol.h:142).  Pairs a sender and
//        receiver endpoint through the federation's KeyTag-indexed
//        Sender/Receiver protocols.  Different parameter shape (1 ctx,
//        2 endpoints, KeyTag template) from the session-protocol
//        channel mint above.
//
// Introducing BOTH into `fixy::sess::` via plain `using` declarations
// is a name-lookup collision — C++ overload resolution would try to
// disambiguate at every call site and produce confusing diagnostics
// when the wrong template parameter shape is supplied.
//
// Resolution: only the session-protocol `mint_channel` is hoisted into
// `fixy::sess::`.  The federation channel mint stays reachable under
// the namespace-aliased path `fixy::sess::federation::mint_channel` AND
// is exposed unambiguously under the explicit alias name
// `fixy::sess::mint_federation_channel` for discoverability via grep:
//
//     fixy::sess::mint_channel<Proto>(ctx_a, ctx_b, ra, rb);
//     fixy::sess::mint_federation_channel<KeyTag>(ctx, sender, recv);
//
// A `using federation::mint_channel;` here would silently shadow the
// session-protocol form because the federation overload takes fewer
// arguments and would win on some call sites.  Explicit alias names
// keep both surfaces stable and grep-discoverable.

template <typename Org,
          typename KeyTag = federation::AnyFederationKey,
          ::crucible::effects::IsExecCtx Ctx,
          typename SenderEndpoint,
          typename ReceiverEndpoint>
[[nodiscard]] constexpr auto mint_federation_channel(
    Ctx const& ctx,
    SenderEndpoint&& sender_endpoint,
    ReceiverEndpoint&& receiver_endpoint,
    ::crucible::safety::Permission<
        ::crucible::permissions::tag::FederatedPeer<Org>> const& admittance) noexcept
{
    return federation::mint_channel<Org, KeyTag>(
        ctx,
        std::forward<SenderEndpoint>(sender_endpoint),
        std::forward<ReceiverEndpoint>(receiver_endpoint),
        admittance);
}

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
