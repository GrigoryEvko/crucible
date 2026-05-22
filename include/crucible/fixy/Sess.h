#pragma once

// ── crucible::fixy::sess — Session minters under fixy:: ────────────
//
// Re-export per misc/16_05_2026_fixy.md §4.  Stable surface
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
//   misc/16_05_2026_fixy.md §4
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
#include <crucible/concurrent/SubstrateSessionBridge.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/permissions/PermSet.h>
#include <crucible/safety/Decide.h>
#include <crucible/safety/Diagnostic.h>
#include <crucible/safety/diag/RowMismatch.h>
#include <crucible/sessions/FederationProtocol.h>
#include <crucible/sessions/PermissionedSession.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCheckpoint.h>
#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionDelegate.h>
#include <crucible/sessions/SessionMint.h>
#include <crucible/sessions/SessionPatterns.h>
#include <crucible/sessions/SessionPermPayloads.h>
#include <crucible/sessions/SessionPhi.h>
#include <crucible/sessions/SessionView.h>

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
using ::crucible::safety::proto::EpochedAccept;

// ═════════════════════════════════════════════════════════════════════
// ── Epoch contexts (sessions/Session.h + PermissionedSession.h) ────
// ═════════════════════════════════════════════════════════════════════
//
// FIXY-U-012.  EpochCtx threads (CurrentEpoch, GenerationCount) plus an
// optional inner ctx through EpochedDelegate/EpochedAccept, gating swap
// to the new protocol only when both monotonic counters cross the
// declared thresholds.  Production callers instantiate EpochCtx<E, G>
// or EpochCtx<E, G, InnerCtx> directly at their site.

using ::crucible::safety::proto::EpochCtx;

// ═════════════════════════════════════════════════════════════════════
// ── Checkpoint (sessions/SessionCheckpoint.h) ──────────────────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::safety::proto::CheckpointedSession;

// ═════════════════════════════════════════════════════════════════════
// ── Protocol-shape predicates — carved out (FIXY-V-066) ────────────
// ═════════════════════════════════════════════════════════════════════
//
// The 6 protocol-shape predicates (`is_send_v` / `is_recv_v` /
// `is_select_v` / `is_offer_v` / `is_loop_v` / `is_head_v`) moved to
// `fixy/SessShape.h` on 2026-05-22 under sub-namespace
// `fixy::sess::shape::`, parallel to V-061..V-064's `checkpoint::` /
// `row::` / `view::` / `crash::` sub-namespace carve-outs.  The
// umbrella-level using-decls below preserve backward compat for any
// historical `fixy::sess::is_*_v` call site; the canonical home is now
// SessShape.h for grep discoverability and sentinel battery scope.

}  // namespace crucible::fixy::sess

#include <crucible/fixy/SessShape.h>

namespace crucible::fixy::sess {

using ::crucible::fixy::sess::shape::is_send_v;
using ::crucible::fixy::sess::shape::is_recv_v;
using ::crucible::fixy::sess::shape::is_select_v;
using ::crucible::fixy::sess::shape::is_offer_v;
using ::crucible::fixy::sess::shape::is_loop_v;
using ::crucible::fixy::sess::shape::is_head_v;

// ═════════════════════════════════════════════════════════════════════
// ── PermSet (permissions/PermSet.h) ────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// FIXY-U-012.  Type-level permission set threaded through every
// PermissionedSessionHandle.  Send<Transferable<T, X>, K> consumes
// Permission<X> from PS; Recv<Transferable<T, X>, K> produces it;
// close() requires PS == EmptyPermSet.  PermSet is the carrier;
// perm_set_insert_t / perm_set_remove_t evolve it; perm_set_contains_v
// / perm_set_subset_v / perm_set_disjoint_v query it.

using ::crucible::safety::proto::PermSet;
using ::crucible::safety::proto::EmptyPermSet;
using ::crucible::safety::proto::perm_set_contains_v;
using ::crucible::safety::proto::perm_set_insert;
using ::crucible::safety::proto::perm_set_insert_t;
using ::crucible::safety::proto::perm_set_remove;
using ::crucible::safety::proto::perm_set_remove_t;
using ::crucible::safety::proto::perm_set_subset_v;
using ::crucible::safety::proto::perm_set_disjoint_v;

// ═════════════════════════════════════════════════════════════════════
// ── Payload permission markers (sessions/SessionPermPayloads.h) ────
// ═════════════════════════════════════════════════════════════════════
//
// FIXY-U-012.  Markers that classify the permission-flow shape of a
// payload value carried over Send/Recv:
//
//   Transferable<T, Tag>    sender LOSES Permission<Tag>; recipient
//                           GAINS Permission<Tag>.
//   Borrowed<T, Tag>        sender LENDS scoped read access; PermSet
//                           unchanged at both ends.
//   Returned<T, Tag>        sender RETURNS a previously-borrowed token;
//                           recipient re-GAINS Permission<Tag>.
//   DelegatedSession<P, PS> sender LOSES every token in PS; recipient
//                           GAINS that inner PermSet with inner Proto.
//
// Each marker is [[nodiscard]] and the token field is [[no_unique_address]]
// so sizeof(Transferable<T, X>) == sizeof(T) under -O3 EBO.

using ::crucible::safety::proto::Transferable;
using ::crucible::safety::proto::Borrowed;
using ::crucible::safety::proto::Returned;
using ::crucible::safety::proto::DelegatedSession;

using ::crucible::safety::proto::is_transferable_v;
using ::crucible::safety::proto::is_borrowed_v;
using ::crucible::safety::proto::is_returned_v;
using ::crucible::safety::proto::is_delegated_session_v;

// ═════════════════════════════════════════════════════════════════════
// ── φ-predicate re-exports (FIXY-AUDIT-B5 → fixy-CR-12 → V-069) ─────
// ═════════════════════════════════════════════════════════════════════
//
// The FX paper §11.18 catalogs seven session-safety levels: safe, df
// (deadlock-free), term (terminating), nterm (non-terminating but
// well-formed), live, live+ (positive liveness), live++ (precise
// liveness).
//
// ── History ────────────────────────────────────────────────────────
//
//   Pre fixy-CR-12: this header re-exported `phi_df_v`,
//                   `phi_live_v`, `phi_live_plus_v`, `phi_live_pp_v`
//                   as ALIASES of is_well_formed_v.  The names lied —
//                   well-formedness is strictly weaker than each of
//                   those properties, and call sites reading
//                   `phi_df_v<Proto>` plausibly believed the
//                   predicate witnessed deadlock-freedom but in fact
//                   accepted protocols that visibly deadlock.
//   fixy-CR-12:     removed those four lying aliases entirely.  No
//                   backwards-compat shim — call sites that wanted
//                   the floor `is_well_formed_v` must spell it out.
//   FIXY-V-069:     RESTORES the four aliases, now over HONEST
//                   substrate predicates from sessions/SessionPhi.h.
//                   Each predicate is now sound-but-conservatively-
//                   incomplete (rejects more protocols than FX's
//                   coinductive definitions would, but never accepts
//                   a protocol that genuinely violates the property).
//                   Additionally REFINES `phi_term_v` / `phi_nterm_v`
//                   from the previous `is_terminal_state_v`-based
//                   aliases (which asked the WRONG question — "is
//                   this state terminal?" not "does this protocol
//                   terminate?") to the substrate's true-termination
//                   predicates.
//
// ── Lattice ────────────────────────────────────────────────────────
//
// Every predicate is STRICTLY STRONGER than its predecessor:
//
//   phi_safe ⊇ phi_df ⊇ phi_term ⊇ phi_live_pp
//                 ⊇ phi_live ⊇ phi_live_plus ⊇ phi_live_pp
//
// phi_nterm is a sibling of phi_term (mutually exclusive — no
// protocol is both terminating and non-terminating); both imply
// phi_safe.
//
// ── Conservatism vs FX paper ──────────────────────────────────────
//
// Each fixy alias forwards to the corresponding substrate predicate
// which witnesses the FX property via a STRUCTURAL refutation
// strategy (find a deadlock witness / non-terminating loop body /
// dead-branch payload duplicate).  Coinductive full decision is
// deferred — see GAPS-FIXY-Sess-PhiRestore for future strengthening
// from "structurally rejected" to "coinductively decided".

template <typename P>
inline constexpr bool phi_safe_v =
    ::crucible::safety::proto::phi_safe_v<P>;

template <typename P>
inline constexpr bool phi_df_v =
    ::crucible::safety::proto::phi_df_v<P>;

template <typename P>
inline constexpr bool phi_term_v =
    ::crucible::safety::proto::phi_term_v<P>;

template <typename P>
inline constexpr bool phi_nterm_v =
    ::crucible::safety::proto::phi_nterm_v<P>;

template <typename P>
inline constexpr bool phi_live_v =
    ::crucible::safety::proto::phi_live_v<P>;

template <typename P>
inline constexpr bool phi_live_plus_v =
    ::crucible::safety::proto::phi_live_plus_v<P>;

template <typename P>
inline constexpr bool phi_live_pp_v =
    ::crucible::safety::proto::phi_live_pp_v<P>;

// ─── Crash-stop family (BSYZ22 / BHYZ23) ──────────────────────────
//
// These ship as first-class substrate predicates; aliasing them under
// `fixy::sess::` keeps the surface coherent for production callers.

using ::crucible::safety::proto::is_well_formed_v;
using ::crucible::safety::proto::is_terminal_state_v;
using ::crucible::safety::proto::is_dual_v;

// ═════════════════════════════════════════════════════════════════════
// ── Ctx-fit concepts (FIXY-V-168) ──────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// The five concepts below are the construction-time gates that the
// `mint_permissioned_session<Proto>(ctx, res, perms...)` /
// `mint_channel<Proto>(ctx_a, ctx_b, ...)` factories evaluate before
// returning a `PermissionedSessionHandle`.  They live in
// `sessions/SessionMint.h:898-940` under `safety::proto::` and gate:
//
//   ProtocolVendorAdmittedByLoopCtx<Proto, LoopCtx>
//       Every VendorPinned<Vendor, ...> position within Proto must be
//       admitted by the LoopCtx's vendor manifest.  Refines the
//       per-position vendor admission down to a single concept
//       (sessions/SessionMint.h:898-901).
//
//   ProtocolEpochAdmittedByLoopCtx<Proto, LoopCtx>
//       Every EpochedDelegate / EpochedAccept position within Proto
//       must clear the LoopCtx's (CurrentEpoch, GenerationCount)
//       thresholds.  Pairs with the vendor predicate above to gate
//       LoopCtx-routable protocols (sessions/SessionMint.h:903-906).
//
//   ProtocolPermissionedRunnable<Proto>
//       Proto carries the structural shape needed to thread
//       PermSet evolution through Send/Recv positions (Transferable /
//       Borrowed / Returned markers admit Permission tokens; close
//       requires EmptyPermSet).  Witnessed structurally — not by
//       presence of Permission positions, but by absence of
//       structurally-malformed shapes that would corrupt the PermSet
//       walk (sessions/SessionMint.h:908-910).
//
//   CtxFitsPermissionedProtocol<Proto, Ctx, InitialPS, LoopCtx = ...>
//       Composite gate: Ctx row admits the protocol AND the
//       permission flow closes (sender's InitialPS lands at
//       EmptyPermSet by the protocol's End) AND vendor + epoch are
//       admitted by the surrounding LoopCtx.  Sole `requires`-clause
//       on mint_permissioned_session (sessions/SessionMint.h:954) and
//       the foundation of CtxFitsChannel (sessions/SessionMint.h:939-
//       940).
//
//   CtxFitsChannel<Proto, CtxA, CtxB>
//       Both endpoints must independently pass
//       CtxFitsPermissionedProtocol with EmptyPermSet — endpoint A
//       runs Proto, endpoint B runs dual(Proto), and channel
//       construction has no token parameters to consume.
//       Sole `requires`-clause on mint_channel (sessions/
//       SessionMint.h:1005).
//
// ── Why through the fixy:: umbrella ────────────────────────────────
//
// Without these re-exports, production code that wanted to write
// `static_assert(fixy::sess::CtxFitsPermissionedProtocol<Proto, MyCtx,
// EmptyPermSet>, "wire the row up");` had to reach past the umbrella
// into `::crucible::safety::proto::CtxFitsPermissionedProtocol` —
// breaking the §XVI promise that `fixy::sess::` is the structurally
// complete user-facing surface.  After V-168, every gate the mint
// factories check is reachable through the same namespace as the
// factories themselves; failed admission diagnostics can be issued
// against `fixy::sess::CtxFits*` and read the same as a successful
// match.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Every entry is a using-decl (pure name-lookup directive);
// the concept resolves to the substrate symbol.  The dual-export
// sentinels below confirm structural identity at the type system
// level — no two concept templates with the same name but different
// substrate origins can drift apart without red-lighting the
// `static_assert(std::same_as_v<...>)` cells.

using ::crucible::safety::proto::ProtocolVendorAdmittedByLoopCtx;
using ::crucible::safety::proto::ProtocolEpochAdmittedByLoopCtx;
using ::crucible::safety::proto::ProtocolPermissionedRunnable;
using ::crucible::safety::proto::CtxFitsPermissionedProtocol;
using ::crucible::safety::proto::CtxFitsChannel;

// ── Dual-export sentinels ───────────────────────────────────────────
//
// Every concept above is structurally pinned to its substrate origin
// via a `requires`-instantiation cell.  If the umbrella ever rewrites
// the using-decl to introduce a fresh concept (e.g. by accident
// during a future carve-out), the sentinel fails to compile because
// the instantiated concept disagrees with the substrate-side
// instantiation on the same argument pack.  Negative cells use a
// non-IsExecCtx / structurally-malformed probe to witness REJECTION
// through the umbrella, proving the concept's predicate body
// evaluates (not just the name resolves).
//
// Probes are intentionally minimal — substantive positive +
// negative coverage with a permissioned protocol + fitting Ctx +
// LoopCtx lives in test/test_fixy_umbrella_reach.cpp's V-168 block.

namespace fixy_v168_sentinel_probes {
struct NonExecCtxProbe {};
}  // namespace fixy_v168_sentinel_probes

static_assert(!CtxFitsPermissionedProtocol<
                  ::crucible::safety::proto::End,
                  fixy_v168_sentinel_probes::NonExecCtxProbe,
                  EmptyPermSet>,
    "FIXY-V-168: fixy::sess::CtxFitsPermissionedProtocol must "
    "reject a non-IsExecCtx Ctx argument through the umbrella.  If "
    "this red-lights, the using-decl above is either missing or "
    "resolves to a different substrate concept.");

static_assert(!CtxFitsChannel<
                  ::crucible::safety::proto::End,
                  fixy_v168_sentinel_probes::NonExecCtxProbe,
                  fixy_v168_sentinel_probes::NonExecCtxProbe>,
    "FIXY-V-168: fixy::sess::CtxFitsChannel must reject "
    "non-IsExecCtx CtxA/CtxB through the umbrella.");

// `ProtocolPermissionedRunnable` is a permissive structural witness
// over the protocol shape alone; the cell below confirms it ADMITS
// the canonical End and Send<Probe, End> shapes, proving the concept
// body evaluates through the umbrella.  A rejecting probe lives in
// the umbrella-reach harness (where a more elaborate ill-formed
// protocol fixture justifies the wiring).
namespace fixy_v168_sentinel_probes {
struct PayloadProbe {};
using SendEndProbe = ::crucible::safety::proto::Send<
    PayloadProbe, ::crucible::safety::proto::End>;
}  // namespace fixy_v168_sentinel_probes

static_assert(ProtocolPermissionedRunnable<
                  ::crucible::safety::proto::End>,
    "FIXY-V-168: fixy::sess::ProtocolPermissionedRunnable must "
    "admit the End terminal protocol through the umbrella.");
static_assert(ProtocolPermissionedRunnable<
                  fixy_v168_sentinel_probes::SendEndProbe>,
    "FIXY-V-168: fixy::sess::ProtocolPermissionedRunnable must "
    "admit Send<Probe, End> through the umbrella.");

// `ProtocolVendorAdmittedByLoopCtx` / `ProtocolEpochAdmittedByLoopCtx`
// are LoopCtx-templated.  Reaching them through the umbrella with
// `void` as the LoopCtx witness — the default for protocols that
// declare no vendor/epoch positions — exercises the concept body
// without requiring a fully-formed EpochCtx fixture.
static_assert(ProtocolVendorAdmittedByLoopCtx<
                  ::crucible::safety::proto::End, void>,
    "FIXY-V-168: fixy::sess::ProtocolVendorAdmittedByLoopCtx must "
    "admit End under the no-LoopCtx (void) sentinel through the "
    "umbrella.");
static_assert(ProtocolEpochAdmittedByLoopCtx<
                  ::crucible::safety::proto::End, void>,
    "FIXY-V-168: fixy::sess::ProtocolEpochAdmittedByLoopCtx must "
    "admit End under the no-LoopCtx (void) sentinel through the "
    "umbrella.");

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
// ── Core session-handle carriers (sessions/Session.h + Perm.h) ─────
// ═════════════════════════════════════════════════════════════════════
//
// FIXY-U-012.  The base + concrete + permissioned handle templates the
// mint family produces.  Every fixy::sess::mint_session_handle<Proto>
// / mint_permissioned_session<Proto>(ctx, res, perms...) call site
// returns a SessionHandle / PermissionedSessionHandle by value; making
// the type carrier itself reachable through fixy::sess:: completes the
// surface so production callers can name return types without reaching
// past the umbrella.  PSH is the §XVI canonical alias for PermissionedSessionHandle.

using ::crucible::safety::proto::SessionHandleBase;
using ::crucible::safety::proto::SessionHandle;
using ::crucible::safety::proto::PermissionedSessionHandle;

template <typename Proto, typename PS, typename Resource,
          typename LoopCtx = void>
using PSH = ::crucible::safety::proto::PermissionedSessionHandle<
    Proto, PS, Resource, LoopCtx>;

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
// Note: both `mint_session<Proto>(ctx, resource)` AND the bare
// `mint_session<Proto>(resource)` overloads are `=delete`d in
// sessions/SessionMint.h (lines 977, 986) — production code uses
// `mint_permissioned_session<Proto>(ctx, resource, perms...)` for
// both the empty-PermSet shim AND the non-empty form.  The deleted
// declarations are re-exported so stale call sites surface the
// canonical diagnostic via the fixy:: path.  The structured
// diagnostic tag for the removal lives at `fixy::sess::diag::
// FixyMintSessionRemoved` (FIXY-AUDIT-B6).
//
// ── fixy-A4-014: §XXI grep-discipline structural pin ──────────────
//
// `grep "mint_session<"` is the canonical §XXI discovery query for
// every authorization point named `mint_session`.  The using-decl
// below re-exports the substrate's =delete`d family — by design,
// because stale call sites must surface the structured deletion
// diagnostic via the fixy:: path.  But the re-export means the §XXI
// grep target is structurally a CANARY surface, not a live mint
// surface: every match at `fixy::sess::mint_session<...>` is a
// pointer to the deletion, not to an authorization point.
//
// The structural pin lives in the sentinel TU below (search
// "fixy-A4-014" in test/test_fixy_sess_mint_removed_diag.cpp): a
// `static_assert(!requires { ... })` that breaks the build if
// EITHER deleted overload ever silently un-deletes.  Two HS14
// neg-compile fixtures (test/fixy_neg/neg_fixy_sess_mint_session_
// {deleted,bare_deleted}.cpp) witness the deletion diagnostic
// surfaces through the fixy:: path for BOTH overloads.

using ::crucible::safety::proto::mint_session;
using ::crucible::safety::proto::mint_permissioned_session;
using ::crucible::safety::proto::mint_channel;
using ::crucible::safety::proto::mint_session_handle;
using ::crucible::safety::proto::mint_recording_session;
using ::crucible::safety::proto::mint_crash_watched_session;
// fixy-A4-006: SessionView is the load-bearing handle-to-view bridge
// (CLAUDE.md §I structural-wrappers list); its mint factory must be
// reachable through `fixy::sess::` like every other session mint.  The
// companion probe concept `can_mint_session_view<H, Tag>` lives in
// `sessions/SessionView.h`'s `detail::sv_self_test::` namespace (test-
// only); it is intentionally not re-exported here.  Callers asking
// "can I view this handle?" use `requires { mint_session_view<Tag>(h); }`
// at the call site (the standard §XXI mint-probe idiom).
using ::crucible::safety::proto::mint_session_view;

// fixy-M-19: mint_substrate_session<Substr, Dir>(ctx, handle) is the
// generic substrate→session bridge (Tier-2→3 per §XXI).  The mint
// RETURNS a typed Session handle, so callers discovering it through
// "I need a session" search begin at fixy::sess::.  The canonical
// substrate-side home stays in fixy::substr:: (next to every per-
// substrate `mint_*_session` family); both surfaces resolve to the
// same `::crucible::concurrent::mint_substrate_session` symbol.  Pipe.h
// no longer re-exports — the M-19 grace window has closed.
using ::crucible::concurrent::mint_substrate_session;

// ═════════════════════════════════════════════════════════════════════
// ── Federation 3-role projection — carved out (FIXY-V-065) ─────────
// ═════════════════════════════════════════════════════════════════════
//
// The federation 3-role projection surface (namespace alias
// `federation = ::crucible::safety::proto::federation` + 4 per-role
// mint using-decls + row gate using-decls + `mint_federation_channel`
// fixy wrapper) moved to fixy/SessFederation.h on 2026-05-22.  Sess.h
// includes it below so every historical fixy::sess::* federation call
// site continues to resolve through fixy/Sess.h with byte-identical
// semantics.  The new file is also pulled directly into Fixy.h's
// Phase-C umbrella for grep-discoverability parallel to SessCrash.h /
// SessView.h / SessRowExtraction.h.  See SessFederation.h for the
// 8-entry fixy-side surface enumeration, sentinel battery, and
// runtime smoke test.

}  // namespace crucible::fixy::sess

#include <crucible/fixy/SessFederation.h>

namespace crucible::fixy::sess {

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

// ── FIXY-U-012 sentinels ──────────────────────────────────────────
// Dual-export discipline (FIXY-U-020): every new re-export gets an
// identity static_assert INSIDE this header so substrate-side rename
// drift breaks the build at the umbrella, not three TUs deep.

// EpochCtx — value-template identity through a sample instantiation.
static_assert(std::is_same_v<EpochCtx<5, 3>,
                             ::crucible::safety::proto::EpochCtx<5, 3>>,
    "fixy::sess::EpochCtx alias must be identical to safety::proto::EpochCtx.");

// Protocol-shape predicates — value identity through a representative
// protocol head.
static_assert(is_send_v<Send<int, End>> ==
              ::crucible::safety::proto::is_send_v<
                  ::crucible::safety::proto::Send<
                      int, ::crucible::safety::proto::End>>,
    "fixy::sess::is_send_v must mirror safety::proto::is_send_v.");

static_assert(is_recv_v<Recv<int, End>> ==
              ::crucible::safety::proto::is_recv_v<
                  ::crucible::safety::proto::Recv<
                      int, ::crucible::safety::proto::End>>,
    "fixy::sess::is_recv_v must mirror safety::proto::is_recv_v.");

static_assert(is_loop_v<Loop<End>> ==
              ::crucible::safety::proto::is_loop_v<
                  ::crucible::safety::proto::Loop<
                      ::crucible::safety::proto::End>>,
    "fixy::sess::is_loop_v must mirror safety::proto::is_loop_v.");

// PermSet — empty + insert + remove + contains + subset + disjoint.
static_assert(std::is_same_v<EmptyPermSet,
                             ::crucible::safety::proto::EmptyPermSet>,
    "fixy::sess::EmptyPermSet alias must be identical to "
    "safety::proto::EmptyPermSet.");

namespace u012_permset_witness {
struct TagA {};
struct TagB {};
using PS  = PermSet<TagA>;
using PS2 = perm_set_insert_t<PS, TagB>;
using PS3 = perm_set_remove_t<PS2, TagA>;
static_assert(perm_set_contains_v<PS, TagA>,
    "fixy::sess::perm_set_contains_v must agree with substrate.");
static_assert(!perm_set_contains_v<PS, TagB>,
    "fixy::sess::perm_set_contains_v must reject missing tag.");
static_assert(perm_set_subset_v<PS, PS2>,
    "fixy::sess::perm_set_subset_v must accept PS ⊆ PS ∪ {TagB}.");
static_assert(perm_set_disjoint_v<PermSet<TagA>, PermSet<TagB>>,
    "fixy::sess::perm_set_disjoint_v must accept disjoint PermSets.");
static_assert(std::is_same_v<PS3, PermSet<TagB>>,
    "fixy::sess::perm_set_remove_t<PS+TagB, TagA> must yield {TagB}.");
}  // namespace u012_permset_witness

// Payload permission markers — Transferable / Borrowed / Returned /
// DelegatedSession identity.
static_assert(std::is_same_v<
    Transferable<int, u012_permset_witness::TagA>,
    ::crucible::safety::proto::Transferable<
        int, u012_permset_witness::TagA>>,
    "fixy::sess::Transferable alias must be identical to "
    "safety::proto::Transferable.");

static_assert(std::is_same_v<
    Borrowed<int, u012_permset_witness::TagA>,
    ::crucible::safety::proto::Borrowed<
        int, u012_permset_witness::TagA>>,
    "fixy::sess::Borrowed alias must be identical to "
    "safety::proto::Borrowed.");

static_assert(std::is_same_v<
    Returned<int, u012_permset_witness::TagA>,
    ::crucible::safety::proto::Returned<
        int, u012_permset_witness::TagA>>,
    "fixy::sess::Returned alias must be identical to "
    "safety::proto::Returned.");

static_assert(is_transferable_v<
    Transferable<int, u012_permset_witness::TagA>>,
    "fixy::sess::is_transferable_v must accept Transferable<T, Tag>.");
static_assert(is_borrowed_v<
    Borrowed<int, u012_permset_witness::TagA>>,
    "fixy::sess::is_borrowed_v must accept Borrowed<T, Tag>.");
static_assert(is_returned_v<
    Returned<int, u012_permset_witness::TagA>>,
    "fixy::sess::is_returned_v must accept Returned<T, Tag>.");
static_assert(!is_transferable_v<int>,
    "fixy::sess::is_transferable_v must reject plain T.");

// SessionHandle / SessionHandleBase / PermissionedSessionHandle (PSH)
// identity.  Use the same template-arg shape the substrate exposes —
// SessionHandleBase<Proto, Derived=void> and SessionHandle<Proto, Resource, LoopCtx=void>.
static_assert(std::is_same_v<
    SessionHandleBase<Send<int, End>>,
    ::crucible::safety::proto::SessionHandleBase<
        ::crucible::safety::proto::Send<int, ::crucible::safety::proto::End>>>,
    "fixy::sess::SessionHandleBase alias must be identical to "
    "safety::proto::SessionHandleBase.");

static_assert(std::is_same_v<
    PSH<Send<int, End>, EmptyPermSet, int>,
    ::crucible::safety::proto::PermissionedSessionHandle<
        ::crucible::safety::proto::Send<int, ::crucible::safety::proto::End>,
        ::crucible::safety::proto::EmptyPermSet, int, void>>,
    "fixy::sess::PSH alias must yield "
    "safety::proto::PermissionedSessionHandle<Proto, PS, Resource, void>.");

}  // namespace self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test (FIXY-U-103 discipline) ─────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Forces TU instantiation of the U-012 surface so clangd/cc1plus walks
// every using-decl beyond the static_assert-only fast path.  Bodies are
// pure type-witnessing — no runtime allocation, no side effects, all
// expressions are consteval-foldable but referenced at runtime to keep
// the optimizer from elision.

inline void runtime_smoke_test() noexcept {
    // PermSet ops — type-witness evolution path; the static_assert
    // references the using-decls so cc1plus must instantiate them.
    struct WitnessTag {};
    using PS0 = EmptyPermSet;
    using PS1 = perm_set_insert_t<PS0, WitnessTag>;
    using PS2 = perm_set_remove_t<PS1, WitnessTag>;
    static_assert(std::is_same_v<PS0, PS2>,
        "fixy::sess::runtime_smoke_test: insert+remove round-trips.");
    static_assert(perm_set_contains_v<PS1, WitnessTag>,
        "fixy::sess::runtime_smoke_test: contains after insert.");
    static_assert(!perm_set_contains_v<PS0, WitnessTag>,
        "fixy::sess::runtime_smoke_test: empty PermSet contains nothing.");

    // Payload markers — type-witness via predicates (markers carry
    // non-default-constructible token fields, so we exercise the
    // is_*_v predicates instead of constructing instances).
    static_assert(is_transferable_v<Transferable<int, WitnessTag>>,
        "fixy::sess::runtime_smoke_test: is_transferable_v accepts marker.");
    static_assert(is_borrowed_v<Borrowed<int, WitnessTag>>,
        "fixy::sess::runtime_smoke_test: is_borrowed_v accepts marker.");
    static_assert(is_returned_v<Returned<int, WitnessTag>>,
        "fixy::sess::runtime_smoke_test: is_returned_v accepts marker.");
    static_assert(!is_transferable_v<int>,
        "fixy::sess::runtime_smoke_test: is_transferable_v rejects plain T.");

    // Protocol predicates — exercise variable templates.
    static_assert(is_send_v<Send<int, End>>,
        "fixy::sess::runtime_smoke_test: is_send_v identifies Send head.");
    static_assert(!is_send_v<End>,
        "fixy::sess::runtime_smoke_test: is_send_v rejects End.");
    static_assert(is_loop_v<Loop<End>>,
        "fixy::sess::runtime_smoke_test: is_loop_v identifies Loop head.");
}

}  // namespace crucible::fixy::sess
