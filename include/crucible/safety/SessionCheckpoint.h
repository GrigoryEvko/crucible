#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::safety::proto — CheckpointedSession combinator
//                            (SEPLOG-L2, task #362, Appendix D.2)
//
// CheckpointedSession<ProtoBase, ProtoRollback> is a FIRST-CLASS
// STRUCTURAL COMBINATOR marking a session position where the
// participant has captured a checkpoint and can, by their own
// decision, either proceed as ProtoBase (the normal, "commit" path)
// OR fall back to ProtoRollback (the abort/retry path).  The
// decision is LOCAL to the checkpoint-holder.
//
// ─── Motivation ────────────────────────────────────────────────────
//
// Several Crucible protocols have rollback semantics that aren't
// expressible as a plain Select (which advertises the choice to the
// peer on the wire):
//
//   * Speculative decoding — draft model produces candidate tokens;
//     target model verifies; on verify-reject, state rewinds and
//     redraft proceeds.  (CRUCIBLE.md §IV.29)
//   * Transaction (#101) — Begin/Op/Commit or Begin/Op/Rollback.
//     The Transaction pattern in SessionPatterns.h expresses this
//     via an explicit Select; CheckpointedSession expresses the
//     LOCAL-decision variant where the state-capture mechanism is
//     out-of-band.
//   * FLR recovery — running session preserved across hardware fault;
//     checkpoint loads at the point of recovery.
//
// The combinator does NOT ship runtime checkpoint/rollback state
// management (that's application-level, typically integrated with
// Cipher or a transaction-manager).  It ships the TYPE-LEVEL
// contract: the session either continues as ProtoBase or restarts
// as ProtoRollback, and both paths are individually well-typed.
//
// ─── Combinator algebra ────────────────────────────────────────────
//
//     dual(CheckpointedSession<P, R>)
//         = CheckpointedSession<dual(P), dual(R)>
//
//     compose(CheckpointedSession<P, R>, Q)
//         = CheckpointedSession<compose(P, Q), compose(R, Q)>
//
//     is_well_formed(CheckpointedSession<P, R>, LoopCtx)
//         = is_well_formed(P, LoopCtx) ∧ is_well_formed(R, LoopCtx)
//
//     is_subtype_sync(CheckpointedSession<P1, R1>,
//                      CheckpointedSession<P2, R2>)
//         = is_subtype_sync(P1, P2) ∧ is_subtype_sync(R1, R2)
//         (product subtyping — both branches refine independently)
//
// ─── Handle semantics ──────────────────────────────────────────────
//
// SessionHandle<CheckpointedSession<P, R>, Res, LoopCtx> exposes
// two terminal transitions:
//
//     handle.base()      -> SessionHandle<P, Res, LoopCtx>
//     handle.rollback()  -> SessionHandle<R, Res, LoopCtx>
//
// Both methods are `&&`-qualified (consume *this).  The user picks
// ONE of them based on their application-level checkpoint-decision
// logic.  After picking, the checkpoint capability is gone; the
// returned handle drives the chosen protocol normally.
//
// The framework does NOT automatically restore state on rollback.
// The application wires that in — e.g., Transaction.h captures a
// state snapshot before .base() and loads it before .rollback() if
// the transaction aborts.  The TYPE system guarantees that both
// paths produce well-typed handles; the RUNTIME semantics of
// "rollback" is application-defined.
//
// ─── Compose rule rationale ────────────────────────────────────────
//
// compose<CheckpointedSession<P, R>, Q> could reasonably extend
// only the base (P;Q but not R;Q), extend both (P;Q AND R;Q), or
// be undefined.  We pick "extend both" because:
//
//   1. It matches the intuition that Q is "what the participant
//      does AFTER the checkpointed transaction completes".  The
//      transaction completes via either branch; Q sequences after
//      both.
//   2. It's the only option that makes compose commute with dual:
//      dual(compose(C<P,R>, Q)) == compose(dual(C<P,R>), dual(Q))
//      holds iff both branches get Q.
//   3. Asymmetric extension (P;Q, R unchanged) would be useful for
//      "Q happens only on commit" semantics, but that's expressible
//      directly as CheckpointedSession<compose<P, Q>, R> without
//      needing a compose rule — users write it by hand.
//
// ─── References ────────────────────────────────────────────────────
//
//   Vieira-Parreaux-Wasowicz 2008 — transactional session types;
//     origin of rollback as a protocol-level concern.  Our version
//     is simpler (no coordinated distributed rollback); applications
//     can layer coordination on top.
//   session_types.md Appendix D.2 — Crucible's specification sketch.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/safety/Session.h>
#include <crucible/safety/SessionSubtype.h>

#include <type_traits>
#include <utility>

namespace crucible::safety::proto {

// ═════════════════════════════════════════════════════════════════════
// ── Combinator type ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Pure type marker — zero runtime footprint.  Nested aliases expose
// each branch for metaprogramming.

template <typename ProtoBase, typename ProtoRollback>
struct CheckpointedSession {
    using base     = ProtoBase;
    using rollback = ProtoRollback;
};

// ─── Shape traits ─────────────────────────────────────────────────

template <typename P>
struct is_checkpointed_session : std::false_type {};

template <typename B, typename R>
struct is_checkpointed_session<CheckpointedSession<B, R>> : std::true_type {};

template <typename P>
inline constexpr bool is_checkpointed_session_v =
    is_checkpointed_session<P>::value;

// ─── Branch extractors ────────────────────────────────────────────

template <typename P> struct checkpoint_base;
template <typename B, typename R>
struct checkpoint_base<CheckpointedSession<B, R>> { using type = B; };

template <typename P> struct checkpoint_rollback;
template <typename B, typename R>
struct checkpoint_rollback<CheckpointedSession<B, R>> { using type = R; };

template <typename P>
using checkpoint_base_t = typename checkpoint_base<P>::type;

template <typename P>
using checkpoint_rollback_t = typename checkpoint_rollback<P>::type;

// ═════════════════════════════════════════════════════════════════════
// ── dual_of<CheckpointedSession<P, R>> ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Both branches dualise independently.  Peer sees a mirror-shaped
// checkpointed session whose branches are the duals of ours.

template <typename B, typename R>
struct dual_of<CheckpointedSession<B, R>> {
    using type = CheckpointedSession<
        typename dual_of<B>::type,
        typename dual_of<R>::type>;
};

// ═════════════════════════════════════════════════════════════════════
// ── compose<CheckpointedSession<P, R>, Q> ──────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Q extends BOTH branches — once the checkpointed transaction ends
// (via either path), Q sequences after.  Rationale in file header.

template <typename B, typename R, typename Q>
struct compose<CheckpointedSession<B, R>, Q> {
    using type = CheckpointedSession<
        typename compose<B, Q>::type,
        typename compose<R, Q>::type>;
};

// ═════════════════════════════════════════════════════════════════════
// ── is_well_formed<CheckpointedSession<P, R>, LoopCtx> ─────────────
// ═════════════════════════════════════════════════════════════════════
//
// Both branches must be well-formed under the same LoopCtx.  The
// checkpointed session itself doesn't introduce a new Loop scope.

template <typename B, typename R, typename LoopCtx>
struct is_well_formed<CheckpointedSession<B, R>, LoopCtx>
    : std::bool_constant<
          is_well_formed<B, LoopCtx>::value &&
          is_well_formed<R, LoopCtx>::value
      > {};

// ═════════════════════════════════════════════════════════════════════
// ── is_subtype_sync: product subtyping ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// CheckpointedSession<P1, R1> ⩽ CheckpointedSession<P2, R2>
//     iff  P1 ⩽ P2  ∧  R1 ⩽ R2
//
// Each branch refines independently — matches the intuition that a
// narrower base + narrower rollback is a refinement of a wider pair.

template <typename B1, typename R1, typename B2, typename R2>
struct is_subtype_sync<CheckpointedSession<B1, R1>,
                        CheckpointedSession<B2, R2>>
    : std::bool_constant<
          is_subtype_sync<B1, B2>::value &&
          is_subtype_sync<R1, R2>::value
      > {};

// ═════════════════════════════════════════════════════════════════════
// ── SessionHandle<CheckpointedSession<P, R>, Res, LoopCtx> ─────────
// ═════════════════════════════════════════════════════════════════════
//
// Terminal handle exposing two &&-qualified branch transitions.
// User picks base() or rollback() based on application logic (e.g.,
// verify-reject triggers rollback; verify-accept triggers base).
// Neither transition is silently automatic — both require an
// explicit method call that consumes *this.

template <typename ProtoBase, typename ProtoRollback,
          typename Resource, typename LoopCtx>
class [[nodiscard]] SessionHandle<CheckpointedSession<ProtoBase, ProtoRollback>,
                                   Resource, LoopCtx>
    : public SessionHandleBase<CheckpointedSession<ProtoBase, ProtoRollback>>
{
    Resource resource_;

    template <typename P, typename R, typename L>
    friend class SessionHandle;

    template <typename P, typename R>
    friend constexpr auto make_session_handle(R r) noexcept;

    template <typename R, typename Res, typename L>
    friend constexpr auto detail::step_to_next(Res) noexcept;

public:
    using protocol         = CheckpointedSession<ProtoBase, ProtoRollback>;
    using base_protocol    = ProtoBase;
    using rollback_protocol = ProtoRollback;
    using resource_type    = Resource;
    using loop_ctx         = LoopCtx;

    constexpr explicit SessionHandle(Resource r)
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : resource_{std::move(r)} {}

    constexpr SessionHandle(SessionHandle&&) noexcept            = default;
    constexpr SessionHandle& operator=(SessionHandle&&) noexcept = default;
    ~SessionHandle()                                             = default;

    // Pick the BASE (normal, "commit") path.  Consumes *this and
    // returns a handle advanced to ProtoBase.  Loop/Continue
    // resolution applied by step_to_next.
    [[nodiscard]] constexpr auto base() &&
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
    {
        this->mark_consumed_();
        return detail::step_to_next<ProtoBase, Resource, LoopCtx>(
            std::move(resource_));
    }

    // Pick the ROLLBACK (abort/retry) path.  Consumes *this and
    // returns a handle advanced to ProtoRollback.  The application
    // is responsible for restoring any checkpointed state before
    // calling this — the framework does NOT automatically manage
    // runtime state; only the protocol typing.
    [[nodiscard]] constexpr auto rollback() &&
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
    {
        this->mark_consumed_();
        return detail::step_to_next<ProtoRollback, Resource, LoopCtx>(
            std::move(resource_));
    }

    // Diagnostic borrows — do NOT consume the handle.
    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return resource_; }
};

// ═════════════════════════════════════════════════════════════════════
// ── Ergonomic surface ──────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename P>
concept Checkpointed = is_checkpointed_session_v<P>;

// Consteval helper — one-line compile-time check that a protocol is
// CheckpointedSession with the specified branches.  Useful at
// boundary function signatures.
template <typename P, typename ExpectedBase, typename ExpectedRollback>
consteval void assert_checkpointed_matches() noexcept {
    static_assert(is_checkpointed_session_v<P>,
        "crucible::safety::proto::assert_checkpointed_matches: "
        "P is not a CheckpointedSession.");
    static_assert(std::is_same_v<checkpoint_base_t<P>, ExpectedBase>,
        "crucible::safety::proto::assert_checkpointed_matches: "
        "base branch does not match ExpectedBase.");
    static_assert(std::is_same_v<checkpoint_rollback_t<P>, ExpectedRollback>,
        "crucible::safety::proto::assert_checkpointed_matches: "
        "rollback branch does not match ExpectedRollback.");
}

// ═════════════════════════════════════════════════════════════════════
// ── Framework self-test static_asserts ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::checkpoint_self_test {

// ─── Fixture protocols ─────────────────────────────────────────────

struct Request  {};
struct Response {};
struct Error    {};

using CommitPath   = Send<Request, Recv<Response, End>>;
using RollbackPath = Send<Request, Recv<Error, End>>;

using CkptSession = CheckpointedSession<CommitPath, RollbackPath>;

// ─── Shape traits ─────────────────────────────────────────────────

static_assert( is_checkpointed_session_v<CkptSession>);
static_assert(!is_checkpointed_session_v<End>);
static_assert(!is_checkpointed_session_v<Send<int, End>>);
static_assert(!is_checkpointed_session_v<Select<End, End>>);

static_assert(std::is_same_v<checkpoint_base_t<CkptSession>,     CommitPath>);
static_assert(std::is_same_v<checkpoint_rollback_t<CkptSession>, RollbackPath>);

// is_head_v inherited from Session.h — CheckpointedSession is not Loop,
// so it's a valid head.  SessionHandle construction works.
static_assert(is_head_v<CkptSession>);

// ─── Duality ──────────────────────────────────────────────────────

// Both branches dualise.
static_assert(std::is_same_v<
    dual_of_t<CkptSession>,
    CheckpointedSession<dual_of_t<CommitPath>, dual_of_t<RollbackPath>>>);

// Involution.
static_assert(std::is_same_v<dual_of_t<dual_of_t<CkptSession>>, CkptSession>);

// ─── Composition ──────────────────────────────────────────────────

using After = Send<int, End>;

// compose extends BOTH branches.
static_assert(std::is_same_v<
    compose_t<CkptSession, After>,
    CheckpointedSession<
        compose_t<CommitPath,   After>,
        compose_t<RollbackPath, After>>>);

// Compose-identity: compose<Ckpt, End> == Ckpt.
static_assert(std::is_same_v<
    compose_t<CkptSession, End>,
    CkptSession>);

// Dual / compose commute:
//   dual(compose(C, Q)) == compose(dual(C), dual(Q))
// This is the load-bearing reason for the "extend both" compose rule.
static_assert(std::is_same_v<
    dual_of_t<compose_t<CkptSession, After>>,
    compose_t<dual_of_t<CkptSession>, dual_of_t<After>>>);

// ─── Well-formedness ──────────────────────────────────────────────

static_assert(is_well_formed_v<CkptSession>);

// Both branches must be well-formed.
using CkptWithFreeContinue = CheckpointedSession<Continue, End>;
static_assert(!is_well_formed_v<CkptWithFreeContinue>);

using CkptWithBadRollback = CheckpointedSession<End, Continue>;
static_assert(!is_well_formed_v<CkptWithBadRollback>);

// Inside a Loop, Continue in either branch is well-formed.
using CkptInsideLoop = Loop<CheckpointedSession<
    Send<int, Continue>,
    Send<int, End>>>;
static_assert(is_well_formed_v<CkptInsideLoop>);

// ─── Subtyping (product) ──────────────────────────────────────────

// Reflexivity — any ckpt is a subtype of itself.
static_assert(is_subtype_sync_v<CkptSession, CkptSession>);

// Narrowing in the base branch produces a subtype.
using NarrowerCommit = Send<Request, Recv<Response, End>>;  // same in this case
// Construct a genuinely-narrower base via Select narrowing.
struct MsgA {};
struct MsgB {};
using WiderSelectCkpt = CheckpointedSession<
    Select<Send<MsgA, End>, Send<MsgB, End>>,  // 2-branch Select
    End>;
using NarrowerSelectCkpt = CheckpointedSession<
    Select<Send<MsgA, End>>,                    // 1-branch Select
    End>;

// NarrowerSelect IS a subtype of WiderSelect (per Gay-Hole: fewer
// Select branches is a subtype).  Product subtyping lifts this.
static_assert(is_subtype_sync_v<NarrowerSelectCkpt, WiderSelectCkpt>);
static_assert(!is_subtype_sync_v<WiderSelectCkpt, NarrowerSelectCkpt>);

// Strict subtype (via is_strict_subtype_sync_v from SessionSubtype.h)
static_assert(is_strict_subtype_sync_v<NarrowerSelectCkpt, WiderSelectCkpt>);

// Mismatched shape is not a subtype.
static_assert(!is_subtype_sync_v<CkptSession, End>);
static_assert(!is_subtype_sync_v<End, CkptSession>);

// ─── Concept + assertion helpers ──────────────────────────────────

template <typename P>
    requires Checkpointed<P>
consteval bool requires_checkpointed() { return true; }
static_assert(requires_checkpointed<CkptSession>());

consteval bool check_assert_matches() {
    assert_checkpointed_matches<CkptSession, CommitPath, RollbackPath>();
    return true;
}
static_assert(check_assert_matches());

// ─── Nested checkpointing ─────────────────────────────────────────
//
// CheckpointedSession<CheckpointedSession<A, B>, C> — an outer
// checkpoint whose base is itself a checkpointed session.  Well-formed,
// dualises correctly, composes correctly.

using NestedCkpt = CheckpointedSession<
    CheckpointedSession<Send<int, End>, Send<bool, End>>,
    End>;

static_assert(is_well_formed_v<NestedCkpt>);
static_assert(std::is_same_v<
    dual_of_t<NestedCkpt>,
    CheckpointedSession<
        CheckpointedSession<Recv<int, End>, Recv<bool, End>>,
        End>>);

// Involution through nesting.
static_assert(std::is_same_v<dual_of_t<dual_of_t<NestedCkpt>>, NestedCkpt>);

}  // namespace detail::checkpoint_self_test

}  // namespace crucible::safety::proto
