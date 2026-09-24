#pragma once

// A session position where the participant holds a checkpoint and can
// go on along the base protocol or fall back to the rollback protocol.
//
// The distinction from Select is that this decision is local.  A Select
// announces the chosen branch to the peer on the wire.  A checkpoint
// decision does not, so the mechanism that captured the state and the
// mechanism that decides sit outside the protocol entirely.
//
// Only the type-level contract lives here.  Both branches are
// individually well-typed, and nothing in the framework captures or
// restores the state that gives rollback its meaning.  An application
// that wants a rollback to undo anything wires that in itself, before
// it takes the rollback branch.
//
// Composition extends both branches rather than the base alone.  The
// checkpointed stretch is over once either branch is taken, so whatever
// follows follows both.  It is also the only choice under which
// composition commutes with duality, since dualising distributes over
// the branches.  Extending the base alone would express "this happens
// only on commit", but that already has a spelling: put the composition
// inside the base branch by hand.

#include <crucible/Platform.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionEventLog.h>
#include <crucible/sessions/SessionSubtype.h>

#include <type_traits>
#include <utility>

namespace crucible::safety::proto {

template <typename ProtoBase, typename ProtoRollback>
struct CheckpointedSession {
    using base = ProtoBase;
    using rollback = ProtoRollback;
};

template <typename P>
struct is_checkpointed_session : std::false_type {};

template <typename B, typename R>
struct is_checkpointed_session<CheckpointedSession<B, R>> : std::true_type {};

template <typename P>
inline constexpr bool is_checkpointed_session_v = is_checkpointed_session<P>::value;

template <typename P>
struct checkpoint_base;
template <typename B, typename R>
struct checkpoint_base<CheckpointedSession<B, R>> {
    using type = B;
};

template <typename P>
struct checkpoint_rollback;
template <typename B, typename R>
struct checkpoint_rollback<CheckpointedSession<B, R>> {
    using type = R;
};

template <typename P>
using checkpoint_base_t = typename checkpoint_base<P>::type;

template <typename P>
using checkpoint_rollback_t = typename checkpoint_rollback<P>::type;

// The peer sees a mirror-shaped checkpoint whose branches are the duals
// of this side's branches.

template <typename B, typename R>
struct dual_of<CheckpointedSession<B, R>> {
    using type = CheckpointedSession<typename dual_of<B>::type, typename dual_of<R>::type>;
};

// Dualising distributes over the branches, so the round trip returns
// the original protocol only when both branches survive it.

template <typename B, typename R>
struct is_dual_involutive<CheckpointedSession<B, R>>
    : std::bool_constant<is_dual_involutive<B>::value && is_dual_involutive<R>::value> {};

// Both arms are reachable, so a choice with no branches in either one
// is a defect: whichever arm is taken would have nothing to pick and
// the peer nothing to signal.

template <typename B, typename R>
struct is_empty_choice<CheckpointedSession<B, R>>
    : std::bool_constant<is_empty_choice<B>::value || is_empty_choice<R>::value> {};

template <typename B, typename R, typename Q>
struct compose<CheckpointedSession<B, R>, Q> {
    using type = CheckpointedSession<typename compose<B, Q>::type, typename compose<R, Q>::type>;
};

// A checkpoint opens no loop scope of its own, so the enclosing loop
// context reaches both branches unchanged.  A loop-closing step inside
// a branch is well-formed only when the surrounding context supplies
// the loop.

template <typename B, typename R, typename LoopCtx>
struct is_well_formed<CheckpointedSession<B, R>, LoopCtx>
    : std::bool_constant<is_well_formed<B, LoopCtx>::value && is_well_formed<R, LoopCtx>::value> {};

// Each branch refines on its own, so a narrower base paired with a
// narrower rollback refines the wider pair.

template <typename B1, typename R1, typename B2, typename R2>
struct is_subtype_sync_structural<CheckpointedSession<B1, R1>, CheckpointedSession<B2, R2>>
    : std::bool_constant<is_subtype_sync_structural<B1, B2>::value && is_subtype_sync_structural<R1, R2>::value> {};

namespace detail::subtype {

template <typename B1, typename R1, typename B2, typename R2>
struct protocol_grade_satisfies<CheckpointedSession<B1, R1>, CheckpointedSession<B2, R2>>
    : std::bool_constant<protocol_grade_satisfies<B1, B2>::value && protocol_grade_satisfies<R1, R2>::value> {};

}  // namespace detail::subtype

// The owner still has to pick a branch, so the position counts as
// terminal only when both branches are.  If either one carries protocol
// work the handle still owes its peer, dropping the handle is
// abandonment.

template <typename B, typename R>
struct is_terminal_state<CheckpointedSession<B, R>>
    : std::bool_constant<is_terminal_state<B>::value && is_terminal_state<R>::value> {};

// Both branches run at some point, so an unhandled peer crash in either
// one is a defect.

namespace detail::crash {

template <typename B, typename R, typename PeerTag>
struct all_offers_have_crash_branch<CheckpointedSession<B, R>, PeerTag>
    : std::bool_constant<all_offers_have_crash_branch<B, PeerTag>::value
                         && all_offers_have_crash_branch<R, PeerTag>::value> {};

}  // namespace detail::crash

template <typename ProtoBase, typename ProtoRollback, typename Resource, typename LoopCtx>
class [[nodiscard]] SessionHandle<CheckpointedSession<ProtoBase, ProtoRollback>, Resource, LoopCtx>
    : public SessionHandleBase<CheckpointedSession<ProtoBase, ProtoRollback>,
                               SessionHandle<CheckpointedSession<ProtoBase, ProtoRollback>, Resource, LoopCtx>> {
    Resource resource_;

    template <typename P, typename R, typename L>
    friend class SessionHandle;

    template <typename FProto, typename FRes, typename FLoop>
    friend constexpr auto
        detail::make_session_handle(FRes, std::source_location) noexcept(std::is_nothrow_move_constructible_v<FRes>)
            -> SessionHandle<FProto, FRes, FLoop>;

    constexpr explicit SessionHandle(Resource r, std::source_location loc = std::source_location::current()) noexcept(
        std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<CheckpointedSession<ProtoBase, ProtoRollback>,
                            SessionHandle<CheckpointedSession<ProtoBase, ProtoRollback>, Resource, LoopCtx>>{loc},
          resource_{std::move(r)} {}

public:
    using protocol = CheckpointedSession<ProtoBase, ProtoRollback>;
    using base_protocol = ProtoBase;
    using rollback_protocol = ProtoRollback;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;

    constexpr SessionHandle(SessionHandle&&) noexcept = default;
    constexpr SessionHandle& operator=(SessionHandle&&) noexcept = default;
    ~SessionHandle() = default;

    [[nodiscard]] constexpr auto base() && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        this->mark_consumed_();
        return detail::step_to_next<ProtoBase, Resource, LoopCtx>(std::move(resource_));
    }

    // Restoring the checkpointed state is the caller's job and must
    // happen before this call.  Only the protocol typing is handled
    // here.
    [[nodiscard]] constexpr auto rollback() && noexcept(std::is_nothrow_move_constructible_v<Resource>) {
        this->mark_consumed_();
        return detail::step_to_next<ProtoRollback, Resource, LoopCtx>(std::move(resource_));
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return resource_; }
};

template <typename P>
concept Checkpointed = is_checkpointed_session_v<P>;

template <typename P, typename ExpectedBase, typename ExpectedRollback>
consteval void assert_checkpointed_matches() noexcept {
    static_assert(is_checkpointed_session_v<P>, "crucible::session::diagnostic [ProtocolViolation_State]: "
                                                "assert_checkpointed_matches: P is not a CheckpointedSession.");
    static_assert(std::is_same_v<checkpoint_base_t<P>, ExpectedBase>,
                  "crucible::session::diagnostic [ProtocolViolation_State]: "
                  "assert_checkpointed_matches: base branch does not match "
                  "ExpectedBase.");
    static_assert(std::is_same_v<checkpoint_rollback_t<P>, ExpectedRollback>,
                  "crucible::session::diagnostic [ProtocolViolation_State]: "
                  "assert_checkpointed_matches: rollback branch does not match "
                  "ExpectedRollback.");
}

#ifdef CRUCIBLE_SESSION_SELF_TESTS
namespace detail::checkpoint_self_test {

struct Request {};
struct Response {};
struct Error {};

using CommitPath = Send<Request, Recv<Response, End>>;
using RollbackPath = Send<Request, Recv<Error, End>>;

using CkptSession = CheckpointedSession<CommitPath, RollbackPath>;

static_assert(is_checkpointed_session_v<CkptSession>);
static_assert(!is_checkpointed_session_v<End>);
static_assert(!is_checkpointed_session_v<Send<int, End>>);
static_assert(!is_checkpointed_session_v<Select<End, End>>);

static_assert(std::is_same_v<checkpoint_base_t<CkptSession>, CommitPath>);
static_assert(std::is_same_v<checkpoint_rollback_t<CkptSession>, RollbackPath>);

static_assert(is_head_v<CkptSession>);

static_assert(
    std::is_same_v<dual_of_t<CkptSession>, CheckpointedSession<dual_of_t<CommitPath>, dual_of_t<RollbackPath>>>);
static_assert(std::is_same_v<dual_of_t<dual_of_t<CkptSession>>, CkptSession>);

using After = Send<int, End>;

static_assert(std::is_same_v<compose_t<CkptSession, After>,
                             CheckpointedSession<compose_t<CommitPath, After>, compose_t<RollbackPath, After>>>);

static_assert(std::is_same_v<compose_t<CkptSession, End>, CkptSession>);

// Composition commutes with duality, which is the reason composition
// extends both branches.
static_assert(
    std::is_same_v<dual_of_t<compose_t<CkptSession, After>>, compose_t<dual_of_t<CkptSession>, dual_of_t<After>>>);

static_assert(is_well_formed_v<CkptSession>);

using CkptWithFreeContinue = CheckpointedSession<Continue, End>;
static_assert(!is_well_formed_v<CkptWithFreeContinue>);

using CkptWithBadRollback = CheckpointedSession<End, Continue>;
static_assert(!is_well_formed_v<CkptWithBadRollback>);

// A loop-closing step inside a branch is well-formed once the
// surrounding loop supplies the scope.
using CkptInsideLoop = Loop<CheckpointedSession<Send<int, Continue>, Send<int, End>>>;
static_assert(is_well_formed_v<CkptInsideLoop>);

static_assert(is_terminal_state_v<CheckpointedSession<End, End>>);

// A crashed branch is terminal too, at any crash tier and under a
// vendor pin.
static_assert(is_terminal_state_v<CheckpointedSession<Stop, End>>);
static_assert(is_terminal_state_v<CheckpointedSession<End, Stop>>);
static_assert(is_terminal_state_v<CheckpointedSession<Stop, Stop>>);
static_assert(is_terminal_state_v<CheckpointedSession<Stop_g<CrashClass::Abort>, Stop_g<CrashClass::Throw>>>);
static_assert(is_terminal_state_v<CheckpointedSession<VendorPinned<VendorBackend::NV, End>, End>>);
static_assert(is_terminal_state_v<CheckpointedSession<End, VendorPinned<VendorBackend::AMD, Stop>>>);

// One branch still owing the peer an operation is enough to make the
// position non-terminal, and an unmade choice counts as owed work.
static_assert(!is_terminal_state_v<CheckpointedSession<Send<int, End>, End>>);
static_assert(!is_terminal_state_v<CheckpointedSession<End, Recv<int, End>>>);
static_assert(!is_terminal_state_v<CheckpointedSession<Send<int, End>, Recv<int, End>>>);
static_assert(!is_terminal_state_v<CheckpointedSession<Select<Send<int, End>>, End>>);
static_assert(!is_terminal_state_v<CheckpointedSession<End, Offer<Recv<int, End>>>>);

// A loop whose body is terminal can never come back round, so a
// twin-terminal checkpoint is rejected as a loop body.
static_assert(!is_well_formed_v<Loop<CheckpointedSession<End, End>>>);

static_assert(is_subtype_sync_v<CkptSession, CkptSession>);

using NarrowerCommit = Send<Request, Recv<Response, End>>;
struct MsgA {};
struct MsgB {};
using WiderSelectCkpt = CheckpointedSession<Select<Send<MsgA, End>, Send<MsgB, End>>, End>;
using NarrowerSelectCkpt = CheckpointedSession<Select<Send<MsgA, End>>, End>;

// A Select that offers fewer branches is a subtype of one that offers
// more, and the branch-wise rule lifts that to the checkpoint.
static_assert(is_subtype_sync_v<NarrowerSelectCkpt, WiderSelectCkpt>);
static_assert(!is_subtype_sync_v<WiderSelectCkpt, NarrowerSelectCkpt>);

static_assert(is_strict_subtype_sync_v<NarrowerSelectCkpt, WiderSelectCkpt>);

static_assert(!is_subtype_sync_v<CkptSession, End>);
static_assert(!is_subtype_sync_v<End, CkptSession>);

template <typename P>
    requires Checkpointed<P>
consteval bool requires_checkpointed() {
    return true;
}
static_assert(requires_checkpointed<CkptSession>());

consteval bool check_assert_matches() {
    assert_checkpointed_matches<CkptSession, CommitPath, RollbackPath>();
    return true;
}
static_assert(check_assert_matches());

using NestedCkpt = CheckpointedSession<CheckpointedSession<Send<int, End>, Send<bool, End>>, End>;

static_assert(is_well_formed_v<NestedCkpt>);
static_assert(std::is_same_v<dual_of_t<NestedCkpt>,
                             CheckpointedSession<CheckpointedSession<Recv<int, End>, Recv<bool, End>>, End>>);

static_assert(std::is_same_v<dual_of_t<dual_of_t<NestedCkpt>>, NestedCkpt>);

// Either branch may be the one that runs, so a branch that does not
// survive the dual round trip costs the whole checkpoint its involution.
namespace fixy_a2_003_is_dual_involutive_checkpointed {
struct RoleA {};
using InvolutiveBoth = CheckpointedSession<Send<int, End>, Recv<int, End>>;
static_assert(is_dual_involutive_v<InvolutiveBoth>);

using NonInvBase = CheckpointedSession<Offer<Sender<RoleA>, Recv<int, End>>, End>;
static_assert(!is_dual_involutive_v<NonInvBase>);

using NonInvRecovery = CheckpointedSession<End, Offer<Sender<RoleA>, Recv<int, End>>>;
static_assert(!is_dual_involutive_v<NonInvRecovery>);

using NestedCkptInvolutive = CheckpointedSession<NestedCkpt, End>;
static_assert(is_dual_involutive_v<NestedCkptInvolutive>);
}  // namespace fixy_a2_003_is_dual_involutive_checkpointed

// A defect in either branch is a defect in the checkpoint, at any depth,
// because either branch may be the one that runs.
namespace fixy_a2_004_is_empty_choice_checkpointed {
using HealthyCkpt = CheckpointedSession<Send<int, End>, Recv<int, End>>;
static_assert(!is_empty_choice_v<HealthyCkpt>);

using EmptySelectInBase = CheckpointedSession<Select<>, End>;
static_assert(is_empty_choice_v<EmptySelectInBase>);

using EmptyOfferInRecovery = CheckpointedSession<End, Offer<>>;
static_assert(is_empty_choice_v<EmptyOfferInRecovery>);

using BuriedEmpty = CheckpointedSession<Send<int, Select<>>, End>;
static_assert(is_empty_choice_v<BuriedEmpty>);

using LoopEmptyRecovery = CheckpointedSession<End, Loop<Offer<>>>;
static_assert(is_empty_choice_v<LoopEmptyRecovery>);

using NestedEmpty = CheckpointedSession<CheckpointedSession<Select<>, End>, End>;
static_assert(is_empty_choice_v<NestedEmpty>);
}  // namespace fixy_a2_004_is_empty_choice_checkpointed

}  // namespace detail::checkpoint_self_test
#endif  // CRUCIBLE_SESSION_SELF_TESTS

}  // namespace crucible::safety::proto
