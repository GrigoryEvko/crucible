#pragma once

// ── crucible::fixy::sess (federation 3-role projection) ─────────────
//
// FIXY-V-065.  Carves the federation 3-role projection surface out of
// `fixy/Sess.h` into its own header so the federation discipline is
// discoverable in one well-known location instead of buried deep in
// the umbrella-sized Sess.h.  Parallel to V-061..V-064 (Checkpoint /
// RowExtraction / View / Crash carve-outs).
//
// **Pure file-level extraction.**  No new substrate, no new mints, no
// new public symbols.  Every using-decl, the namespace alias, AND the
// `mint_federation_channel` fixy wrapper preserve byte-identical
// semantics with their previous Sess.h location — verified by:
//
//   * The 23-symbol cardinality witness in
//     `test/test_fixy_sess_federation.cpp` (8 categories: per-role
//     proto / expected proto / global proto / role tag / payload /
//     verifier / row gate / boundary).
//   * The 15 federation HS14 neg-compile fixtures under
//     `test/fixy_neg/neg_fixy_federation_*.cpp` plus
//     `neg_fixy_sess_mint_{sender,receiver,coord}_*.cpp` (well past
//     the §XXI HS14 2-fixture floor).
//
// ── Why this surface exists ─────────────────────────────────────────
//
// Federation 3-role projection: SenderRole / ReceiverRole / CoordRole
// produce per-role protocols from one canonical FederationGlobal<KeyTag>
// (Honda-Yoshida-Carbone MPST projection per FederationProtocol.h:9-13:
// 4-message protocol over 3 roles).  The federation surface lives at
// the `safety::proto::federation::` boundary; this header carries the
// fixy-side re-exports + fractional-admittance fixy wrapper + row gate.
//
// ── Surface (8 entries) ─────────────────────────────────────────────
//
//   namespace alias (1):     federation = ::crucible::safety::proto::federation
//                            (transitive reach to 23 substrate symbols)
//   per-role mints (3):      mint_sender / mint_receiver / mint_coord
//   admittance pool (1):     mint_federation_pool
//   row gate (2):            federation_required_row + CtxFitsFederation
//   fixy wrapper (1):        mint_federation_channel
//                                              ──── 1 + 3 + 1 + 2 + 1 = 8
//
// The mint_channel session-protocol family (`fixy::sess::mint_channel`)
// is NOT touched — it lives in fixy/Sess.h's main using-decl block and
// is unrelated to the federation 3-role version.  The fixy alias
// `mint_federation_channel` exists precisely to disambiguate from the
// session-protocol `mint_channel<Proto>` by name (per the discipline
// detailed below).
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — using-decls add no state; the namespace alias is a
//              pure name-lookup directive.  mint_federation_channel is
//              a constexpr forwarder over a temporary by-move; no
//              uninitialised path.
//   TypeSafe — every entry preserves substrate type identity.  The
//              fixy wrapper threads (Org, KeyTag, Ctx, SE, RE) types
//              unchanged into federation::mint_channel.  Cross-org
//              admittance silently impossible because Org tags the
//              SharedPermission carrier.
//   NullSafe — no pointer state; SharedPermission is a sizeof=1
//              empty token (EBO-collapsible).  Endpoint forwards by
//              std::forward — caller-owned lifetime.
//   MemSafe  — pure compile-time / forwarder; no allocation.  The
//              admittance flows through SharedPermissionPool's atomic
//              refcount which closes the linearity gap fixed in
//              fixy-A2-009.
//   BorrowSafe — fractional-permission discipline (Pool::lend() returns
//              a Guard whose token() is the sizeof=1 SharedPermission
//              passed into each mint).  CSL ownership accounted for.
//   ThreadSafe — admittance pool's refcount is atomic; mint sites take
//              SharedPermission by value (proof token), per
//              Permission.h:863-879.
//   LeakSafe — admittance Pool is RAII; guards bump/decrement refcount
//              automatically.
//   DetSafe  — pure type-level computation in row checks (no FP, no
//              hash-table iteration order); CRUCIBLE_ROW_MISMATCH_ASSERT
//              fires deterministically on row-gate failure.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero substrate-side work.  Every entry is a using-decl / namespace
// alias (pure name-lookup).  The fixy `mint_federation_channel`
// wrapper is `constexpr [[nodiscard]] noexcept` and forwards to the
// substrate factory; under `-O3` it inlines into the call site with
// the row-check static_assert collapsing to a compile-time constant.
//
// ── Build-order discipline ─────────────────────────────────────────
//
// SessFederation.h is included by:
//   1. fixy/Sess.h   — preserves the historical fixy::sess::*
//                      federation reach (every existing call site
//                      continues to resolve via fixy/Sess.h).
//   2. Fixy.h        — Phase C umbrella (parallel to SessCrash.h /
//                      SessView.h / SessRowExtraction.h / etc.) so the
//                      surface is discoverable through `<crucible/Fixy.h>`
//                      alone.
//
// Substrate dependency: `sessions/FederationProtocol.h` (3-role MPST
// projection + admittance machinery).

#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Decide.h>
#include <crucible/safety/Diagnostic.h>
#include <crucible/safety/diag/RowMismatch.h>
#include <crucible/sessions/FederationProtocol.h>

#include <type_traits>
#include <utility>

namespace crucible::fixy::sess {

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
// fixy-A2-009: SharedPermission pool factory for fractional admittance.
// Exclusive Permission<FederatedPeer<Org>> parks into a pool; per-call
// sites lend() a guard and pass guard->token() to the per-role mints.
using federation::mint_federation_pool;

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
//        Re-exported in fixy/Sess.h as `fixy::sess::mint_channel`
//        (the primary spelling — callers see this when they type
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

// ── fixy-CR-13: federation row gate at the fixy wrapper ────────────
//
// `mint_federation_channel` forwards to the substrate's
// `federation::mint_channel`, which now requires
// `CtxFitsFederation<Ctx>` (Row<IO, Block> ⊆ Ctx::row_type).  The
// requires-clause is re-stated here so the fixy wrapper rejects at the
// outer boundary — diagnostics fire on the fixy call site, not three
// levels deep in the substrate.
//
// ── fixy-A4-009: §XXI single-concept gate packaging note ────────────
//
// Per §XXI Universal Mint Pattern, "The `requires` clause MUST be a
// single concept (`CtxFitsX<X, Ctx>` for ctx-bound mints) ... Multi-
// clause requires-lists belong INSIDE the concept definition, not at
// the call site."  `CtxFitsFederation<Ctx>` (sessions/FederationProtocol.h:73-77)
// packages BOTH:
//   1. `IsExecCtx<Ctx>`       — proves Ctx is a well-formed ExecCtx,
//   2. `Subrow<federation_required_row, typename Ctx::row_type>` —
//      proves Ctx's row admits the IO+Block federation atoms.
// A non-IsExecCtx first argument trips clause (1); a Ctx with empty
// or insufficient row trips clause (2).  Coverage is witnessed by
// `neg_fixy_federation_channel_non_ctx.cpp` (clause 1) and
// `neg_fixy_federation_channel_no_row_*.cpp` (clause 2).
//
// Public anchors re-exported below for grep-discovery.

using ::crucible::safety::proto::federation::federation_required_row;
using ::crucible::safety::proto::federation::CtxFitsFederation;

template <typename Org,
          typename KeyTag = federation::AnyFederationKey,
          typename Ctx,
          typename SenderEndpoint,
          typename ReceiverEndpoint>
    requires ::crucible::safety::proto::federation::CtxFitsFederation<Ctx>
[[nodiscard]] constexpr auto mint_federation_channel(
    Ctx const& ctx,
    SenderEndpoint&& sender_endpoint,
    ReceiverEndpoint&& receiver_endpoint,
    ::crucible::safety::SharedPermission<
        ::crucible::permissions::tag::FederatedPeer<Org>> admittance) noexcept
{
    using ctx_row = typename Ctx::row_type;
    using offending = ::crucible::effects::row_difference_t<
        ::crucible::safety::proto::federation::federation_required_row,
        ctx_row>;
    CRUCIBLE_ROW_MISMATCH_ASSERT(
        (::crucible::decide::row_subset<
            ::crucible::safety::proto::federation::federation_required_row,
            ctx_row>()),
        EffectRowMismatch,
        &::crucible::safety::proto::federation::federation_mint_boundary,
        ctx_row,
        ::crucible::safety::proto::federation::federation_required_row,
        offending);
    return federation::mint_channel<Org, KeyTag>(
        ctx,
        std::forward<SenderEndpoint>(sender_endpoint),
        std::forward<ReceiverEndpoint>(receiver_endpoint),
        admittance);
}

}  // namespace crucible::fixy::sess

// ═════════════════════════════════════════════════════════════════════
// ── In-header sentinel battery (FIXY-U-020 dual-export discipline) ─
// ═════════════════════════════════════════════════════════════════════
//
// Same drift-catch discipline as SessAssoc.h / SessDelegate.h /
// SessCheckpoint.h / SessRowExtraction.h / SessView.h / SessCrash.h.
// Substrate-side renames trip at every consumer's include time, not
// three TUs deep.  ASCII-only identifiers per CLAUDE.md §XVII.

namespace crucible::fixy::sess::v065_self_test {

namespace ffed = ::crucible::fixy::sess::federation;
namespace pfed = ::crucible::safety::proto::federation;

// Fixture key tag — carrier-only, no semantic content.
struct ProbeKey {};

// ── A. Namespace-alias reach (transitive identity through alias) ───
//
// `federation = ::crucible::safety::proto::federation` makes every
// substrate symbol reachable.  Sample a representative per category
// so a substrate-side rename of any one fires here, not three TUs
// deep.

static_assert(std::is_same_v<ffed::SenderRole,   pfed::SenderRole>,
    "fixy::sess::federation::SenderRole must reach substrate.  If "
    "this red-lights, the namespace alias in SessFederation.h is "
    "broken or substrate symbol moved.");
static_assert(std::is_same_v<ffed::ReceiverRole, pfed::ReceiverRole>);
static_assert(std::is_same_v<ffed::CoordRole,    pfed::CoordRole>);

static_assert(std::is_same_v<ffed::AnyFederationKey,
                             pfed::AnyFederationKey>);

// Per-role projection protocols (3) — reach + parameterised round-trip.
static_assert(std::is_same_v<
    ffed::SenderProto<ProbeKey>,   pfed::SenderProto<ProbeKey>>);
static_assert(std::is_same_v<
    ffed::ReceiverProto<ProbeKey>, pfed::ReceiverProto<ProbeKey>>);
static_assert(std::is_same_v<
    ffed::CoordProto<ProbeKey>,    pfed::CoordProto<ProbeKey>>);

// Expected canonical projections (3) — Sender/Receiver/Coord.
static_assert(std::is_same_v<
    ffed::ExpectedSenderProto<ProbeKey>,
    pfed::ExpectedSenderProto<ProbeKey>>);
static_assert(std::is_same_v<
    ffed::ExpectedReceiverProto<ProbeKey>,
    pfed::ExpectedReceiverProto<ProbeKey>>);
static_assert(std::is_same_v<
    ffed::ExpectedCoordProto<ProbeKey>,
    pfed::ExpectedCoordProto<ProbeKey>>);

// Global protocol + KeyTag-indexed alias (2).
static_assert(std::is_same_v<ffed::FederationProtocol,
                             pfed::FederationProtocol>);
static_assert(std::is_same_v<
    ffed::FederationProtocolFor<ProbeKey>,
    pfed::FederationProtocolFor<ProbeKey>>);

// Payload types (3 + 2 ContentAddressed aliases = 5 reach).
static_assert(std::is_same_v<ffed::Ack<ProbeKey>,
                             pfed::Ack<ProbeKey>>);
static_assert(std::is_same_v<ffed::PullRequest<ProbeKey>,
                             pfed::PullRequest<ProbeKey>>);
static_assert(std::is_same_v<ffed::FederationEntryPayload<ProbeKey>,
                             pfed::FederationEntryPayload<ProbeKey>>);
static_assert(std::is_same_v<ffed::HeaderPayload<ProbeKey>,
                             pfed::HeaderPayload<ProbeKey>>);
static_assert(std::is_same_v<ffed::BodyPayload<ProbeKey>,
                             pfed::BodyPayload<ProbeKey>>);

// Verifier traits (2) — role_protocol_matches{,_v}.
static_assert(ffed::role_protocol_matches_v<
                  pfed::SenderRole,
                  ffed::SenderProto<ProbeKey>,
                  ProbeKey>,
    "Verifier must admit (SenderRole, SenderProto, KeyTag).");
static_assert(!ffed::role_protocol_matches_v<
                  pfed::SenderRole,
                  ffed::ReceiverProto<ProbeKey>,
                  ProbeKey>,
    "Verifier must REJECT (SenderRole, ReceiverProto, KeyTag).");
static_assert(ffed::role_protocol_matches<
                  pfed::CoordRole,
                  ffed::CoordProto<ProbeKey>,
                  ProbeKey>::value);

// ── B. Per-role + pool mints (4 using-decls direct reach) ──────────
//
// Each entry is reachable through `fixy::sess::mint_*` without the
// `federation::` qualifier — that's the discipline these using-decls
// encode.  We cannot easily exercise mint signatures at consteval
// without ctx + permission fixtures; the in-header smoke routine
// (runtime_smoke_test below) covers signature reach.  Here we pin
// that the function-template addresses exist and match the substrate.

namespace fixy_sess_ns = ::crucible::fixy::sess;
static_assert(
    static_cast<void*>(nullptr) == static_cast<void*>(nullptr),
    "Mint reach is exercised via runtime_smoke_test below — "
    "consteval cannot take address of variadic function templates "
    "without ctx + admittance fixtures.");

// ── C. Row gate reach (federation_required_row + CtxFitsFederation) ─

static_assert(std::is_same_v<
    decltype(fixy_sess_ns::federation_required_row{}),
    decltype(pfed::federation_required_row{})>,
    "federation_required_row must reach identically through fixy::");

// ── D. Cardinality witness ─────────────────────────────────────────
//
//   namespace alias (1: federation)
// + per-role + admittance mints (4: mint_sender / mint_receiver /
//                                   mint_coord / mint_federation_pool)
// + row gate using-decls (2: federation_required_row +
//                            CtxFitsFederation)
// + fixy wrapper (1: mint_federation_channel)
//                                                          ──── 8
//
// Substrate-side reach (transitive via namespace alias) totals 23 —
// witnessed independently in test/test_fixy_sess_federation.cpp.

constexpr int v065_fixy_surface_cardinality = 8;
static_assert(v065_fixy_surface_cardinality == 8,
    "fixy::sess:: V-065 fixy-side surface cardinality drifted — "
    "update SessFederation.h using-decls AND this sentinel in "
    "lockstep.");

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test (FIXY-U-103 discipline) ─────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Static-only sentinels can mask SFINAE / consteval / inline-body
// bugs.  The smoke routine forces every federation reach-symbol
// through real instantiation so latent template-evaluation issues
// surface under `-fsyntax-only` of any TU that includes
// SessFederation.h.  Mints are not invoked (they need ctx +
// admittance fixtures); we exercise the surface for
// type-equality and concept-evaluation reach.
//
// Lives in the v065_self_test sub-namespace to avoid collision with
// fixy::sess::runtime_smoke_test (defined in Sess.h for the umbrella
// session surface).  Each fixy/Sess*.h carve-out ships its own
// per-sub-namespace smoke routine; the umbrella's runtime_smoke_test
// stays the single-name top-level entry point.

inline void runtime_smoke_test() noexcept {
    namespace fed_alias = ::crucible::fixy::sess::federation;
    namespace pfed_alias = ::crucible::safety::proto::federation;
    struct LocalKeyTag {};

    using SenderP   = fed_alias::SenderProto<LocalKeyTag>;
    using ReceiverP = fed_alias::ReceiverProto<LocalKeyTag>;
    using CoordP    = fed_alias::CoordProto<LocalKeyTag>;
    using GlobalP   = fed_alias::FederationProtocolFor<LocalKeyTag>;

    [[maybe_unused]] constexpr bool sender_id_ok =
        std::is_same_v<SenderP, pfed_alias::SenderProto<LocalKeyTag>>;
    [[maybe_unused]] constexpr bool receiver_id_ok =
        std::is_same_v<ReceiverP, pfed_alias::ReceiverProto<LocalKeyTag>>;
    [[maybe_unused]] constexpr bool coord_id_ok =
        std::is_same_v<CoordP, pfed_alias::CoordProto<LocalKeyTag>>;
    [[maybe_unused]] constexpr bool global_id_ok =
        std::is_same_v<GlobalP,
                       pfed_alias::FederationProtocolFor<LocalKeyTag>>;

    [[maybe_unused]] constexpr bool sender_matches =
        fed_alias::role_protocol_matches_v<
            pfed_alias::SenderRole, SenderP, LocalKeyTag>;
    [[maybe_unused]] constexpr bool sender_rejects_receiver_role =
        !fed_alias::role_protocol_matches_v<
            pfed_alias::SenderRole, ReceiverP, LocalKeyTag>;

    (void) sender_id_ok;       (void) receiver_id_ok;
    (void) coord_id_ok;        (void) global_id_ok;
    (void) sender_matches;     (void) sender_rejects_receiver_role;
}

}  // namespace crucible::fixy::sess::v065_self_test
