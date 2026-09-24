#pragma once

// Every alias here is a local, per-participant session type.  FanOut<N, Msg>
// is one participant's view of an N-way fan-out, not the fan-out itself: the
// global type and its projection onto each role belong to a separate layer.  A
// pattern whose natural shape is multiparty ships only its local projection.

#include <crucible/Platform.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionSubtype.h>
#ifdef CRUCIBLE_SESSION_SELF_TESTS
#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionDelegate.h>
#endif

#include <cstddef>
#include <type_traits>

namespace crucible::safety::proto {
template <typename... Roles>
struct ReliableSet;
}  // namespace crucible::safety::proto

namespace crucible::safety::proto::pattern {

struct CrashSafetyVerified {};
struct CrashSafetyPending {};
struct BaselinePatternNeedsCrashAwareVariant {};
struct DelegateCompatible {};
struct DelegateCompatibilityPending {};
struct PatternHasNoDelegateBoundaryConstraints {};

template <typename Proto, typename R, typename Status, typename Reason = BaselinePatternNeedsCrashAwareVariant>
struct PatternCrashSafety {
    using protocol = Proto;
    using reliable_set = R;
    using status = Status;
    using reason = Reason;

    static constexpr bool verified = std::is_same_v<Status, CrashSafetyVerified>;
    static constexpr bool pending = std::is_same_v<Status, CrashSafetyPending>;
};

template <typename Contract>
inline constexpr bool pattern_crash_safety_verified_v = Contract::verified;

template <typename Contract>
inline constexpr bool pattern_crash_safety_pending_v = Contract::pending;

template <typename Proto, typename Status, typename Reason = PatternHasNoDelegateBoundaryConstraints>
struct PatternDelegateCompatibility {
    using protocol = Proto;
    using status = Status;
    using reason = Reason;

    static constexpr bool compatible = std::is_same_v<Status, DelegateCompatible>;
    static constexpr bool pending = std::is_same_v<Status, DelegateCompatibilityPending>;
};

template <typename Contract>
inline constexpr bool pattern_delegate_compatible_v = Contract::compatible;

template <typename Contract>
inline constexpr bool pattern_delegate_pending_v = Contract::pending;

template <typename Req, typename Resp>
using RequestResponseOnce_Client = Send<Req, Recv<Resp, End>>;

template <typename Req, typename Resp>
using RequestResponseOnce_Server = Recv<Req, Send<Resp, End>>;

// This loop never terminates in-band.  Its close is out of band, at the
// transport or through peer-death detection.  The close-carrying variant below
// is the one to use when both peers must see the end of the session.
template <typename Req, typename Resp>
using RequestResponse_Client = Loop<Send<Req, Recv<Resp, Continue>>>;

template <typename Req, typename Resp>
using RequestResponse_Server = Loop<Recv<Req, Send<Resp, Continue>>>;

template <typename Req, typename Resp>
using RequestResponseLoop_Client = Loop<Select<Send<Req, Recv<Resp, Continue>>, End>>;

template <typename Req, typename Resp>
using RequestResponseLoop_Server = Loop<Offer<Recv<Req, Send<Resp, Continue>>, End>>;

template <typename Out>
using PipelineSource = Loop<Send<Out, Continue>>;

template <typename In>
using PipelineSink = Loop<Recv<In, Continue>>;

// A multi-stage pipeline gives each stage its own local type.  The stages
// compose at the multiparty level, not by nesting these aliases.
template <typename In, typename Out>
using PipelineStage = RequestResponse_Server<In, Out>;

// The transaction is client-driven throughout: the client picks whether to
// issue another operation, commit, or roll back.  A server-initiated abort
// would nest an Offer over the Select on the operation branch, and is not part
// of this shape.

template <typename Begin, typename Op, typename Commit, typename Ack, typename Rollback>
using Transaction_Client =
    Send<Begin, Loop<Select<Send<Op, Continue>, Send<Commit, Recv<Ack, End>>, Send<Rollback, End>>>>;

template <typename Begin, typename Op, typename Commit, typename Ack, typename Rollback>
using Transaction_Server =
    Recv<Begin, Loop<Offer<Recv<Op, Continue>, Recv<Commit, Send<Ack, End>>, Recv<Rollback, End>>>>;

namespace detail {

template <std::size_t N, typename Msg>
struct fan_out_helper {
    using type = Send<Msg, typename fan_out_helper<N - 1, Msg>::type>;
};

template <typename Msg>
struct fan_out_helper<0, Msg> {
    using type = End;
};

template <std::size_t N, typename Msg>
struct fan_in_helper {
    using type = Recv<Msg, typename fan_in_helper<N - 1, Msg>::type>;
};

template <typename Msg>
struct fan_in_helper<0, Msg> {
    using type = End;
};

// These builders split N in half at every step and pass the tail down as an
// accumulator.  The simpler linear recursion needs one instantiation level per
// element, which puts a hard ceiling on N at the compiler's template-depth
// limit.  Halving makes the depth logarithmic in N, so that ceiling stops
// binding.  Total instantiation count is unchanged, and the base cases for the
// two smallest N keep small chains as cheap as the linear form.

template <std::size_t N, typename Msg, typename Tail>
struct chain_send_with_tail {
    static constexpr std::size_t Half = N / 2;
    static constexpr std::size_t Rest = N - Half;
    using right = typename chain_send_with_tail<Rest, Msg, Tail>::type;
    using type = typename chain_send_with_tail<Half, Msg, right>::type;
};

template <typename Msg, typename Tail>
struct chain_send_with_tail<0, Msg, Tail> {
    using type = Tail;
};

template <typename Msg, typename Tail>
struct chain_send_with_tail<1, Msg, Tail> {
    using type = Send<Msg, Tail>;
};

template <std::size_t N, typename Msg, typename Tail>
struct chain_recv_with_tail {
    static constexpr std::size_t Half = N / 2;
    static constexpr std::size_t Rest = N - Half;
    using right = typename chain_recv_with_tail<Rest, Msg, Tail>::type;
    using type = typename chain_recv_with_tail<Half, Msg, right>::type;
};

template <typename Msg, typename Tail>
struct chain_recv_with_tail<0, Msg, Tail> {
    using type = Tail;
};

template <typename Msg, typename Tail>
struct chain_recv_with_tail<1, Msg, Tail> {
    using type = Recv<Msg, Tail>;
};

}  // namespace detail

template <std::size_t N, typename Msg>
using FanOut = typename detail::chain_send_with_tail<N, Msg, End>::type;

template <std::size_t N, typename Msg>
using FanIn = typename detail::chain_recv_with_tail<N, Msg, End>::type;

template <std::size_t N, typename Msg>
using Broadcast = FanOut<N, Msg>;

// Which task a given result answers is not fixed by the type.  Keeping the two
// sequences aligned is the runtime's obligation.
template <std::size_t N, typename Task, typename Result>
using ScatterGather = compose_t<FanOut<N, Task>, FanIn<N, Result>>;

template <typename T>
using MpmcProducer = Loop<Select<Send<T, Continue>, End>>;

template <typename T>
using MpmcConsumer = Loop<Offer<Recv<T, Continue>, End>>;

// This is the crash-oblivious wire shape.  A crash-aware two-phase commit
// wraps the vote and decision points in Sender-annotated Offer branches that
// carry a crash payload.
template <typename Prepare, typename Vote, typename Commit, typename Abort>
using TwoPhaseCommit_Coord = Send<Prepare, Recv<Vote, Select<Send<Commit, End>, Send<Abort, End>>>>;

// The follower is written out rather than spelled as the dual of the
// coordinator, so that the alias stays fixed even if the dual operator's
// implementation moves.
template <typename Prepare, typename Vote, typename Commit, typename Abort>
using TwoPhaseCommit_Follower = Recv<Prepare, Send<Vote, Offer<Recv<Commit, End>, Recv<Abort, End>>>>;

template <typename Probe, typename Ack>
using SwimProbe_Client = RequestResponseOnce_Client<Probe, Ack>;

template <typename Probe, typename Ack>
using SwimProbe_Server = RequestResponseOnce_Server<Probe, Ack>;

template <typename Hello, typename Welcome, typename Reject>
using Handshake_Client = Send<Hello, Offer<Recv<Welcome, End>, Recv<Reject, End>>>;

template <typename Hello, typename Welcome, typename Reject>
using Handshake_Server = Recv<Hello, Select<Send<Welcome, End>, Send<Reject, End>>>;

}  // namespace crucible::safety::proto::pattern

#ifdef CRUCIBLE_SESSION_SELF_TESTS
#if !defined(CRUCIBLE_SESSION_PATTERN_SELF_TESTS_BASIC) && !defined(CRUCIBLE_SESSION_PATTERN_SELF_TESTS_FAN)        \
    && !defined(CRUCIBLE_SESSION_PATTERN_SELF_TESTS_SUBTYPE) && !defined(CRUCIBLE_SESSION_PATTERN_SELF_TESTS_CRASH) \
    && !defined(CRUCIBLE_SESSION_PATTERN_SELF_TESTS_DELEGATE) && !defined(CRUCIBLE_SESSION_PATTERN_SELF_TESTS_COMPOSE)
#define CRUCIBLE_SESSION_PATTERN_SELF_TESTS_BASIC 1
#define CRUCIBLE_SESSION_PATTERN_SELF_TESTS_FAN 1
#define CRUCIBLE_SESSION_PATTERN_SELF_TESTS_SUBTYPE 1
#define CRUCIBLE_SESSION_PATTERN_SELF_TESTS_CRASH 1
#define CRUCIBLE_SESSION_PATTERN_SELF_TESTS_DELEGATE 1
#define CRUCIBLE_SESSION_PATTERN_SELF_TESTS_COMPOSE 1
#endif
namespace crucible::safety::proto::pattern::detail::pattern_self_test {

struct Req {};
struct Resp {};
struct Task {};
struct Result {};
struct Prepare {};
struct Vote {};
struct Commit {};
struct Abort {};
struct Job {};
struct Probe {};
struct Ack {};
struct Hello {};
struct Welcome {};
struct Reject {};
struct Cancel {};
struct Retry {};

struct ClientRole {};
struct ServerRole {};
struct SourceRole {};
struct SinkRole {};
struct StageRole {};
struct CoordinatorRole {};
struct CollectorRole {};
struct ProducerRole {};
struct ConsumerRole {};
struct FollowerRole {};
struct DelegatedRecipientRole {};

using TxClient = Transaction_Client<Prepare, Req, Commit, Ack, Abort>;
using TxServer = Transaction_Server<Prepare, Req, Commit, Ack, Abort>;
using ConcreteCoord = TwoPhaseCommit_Coord<Prepare, Vote, Commit, Abort>;
using ConcreteFollower = TwoPhaseCommit_Follower<Prepare, Vote, Commit, Abort>;

#ifdef CRUCIBLE_SESSION_PATTERN_SELF_TESTS_BASIC
static_assert(std::is_same_v<RequestResponseOnce_Client<Req, Resp>, Send<Req, Recv<Resp, End>>>);
static_assert(std::is_same_v<RequestResponseOnce_Server<Req, Resp>, Recv<Req, Send<Resp, End>>>);
static_assert(std::is_same_v<dual_of_t<RequestResponseOnce_Client<Req, Resp>>, RequestResponseOnce_Server<Req, Resp>>);
static_assert(std::is_same_v<dual_of_t<RequestResponseOnce_Server<Req, Resp>>, RequestResponseOnce_Client<Req, Resp>>);

static_assert(std::is_same_v<RequestResponse_Client<Req, Resp>, Loop<Send<Req, Recv<Resp, Continue>>>>);
static_assert(std::is_same_v<RequestResponse_Server<Req, Resp>, Loop<Recv<Req, Send<Resp, Continue>>>>);
static_assert(std::is_same_v<dual_of_t<RequestResponse_Client<Req, Resp>>, RequestResponse_Server<Req, Resp>>);

static_assert(
    std::is_same_v<RequestResponseLoop_Client<Req, Resp>, Loop<Select<Send<Req, Recv<Resp, Continue>>, End>>>);
static_assert(std::is_same_v<RequestResponseLoop_Server<Req, Resp>, Loop<Offer<Recv<Req, Send<Resp, Continue>>, End>>>);
static_assert(std::is_same_v<dual_of_t<RequestResponseLoop_Client<Req, Resp>>, RequestResponseLoop_Server<Req, Resp>>);

static_assert(is_well_formed_v<RequestResponseOnce_Client<Req, Resp>>);
static_assert(is_well_formed_v<RequestResponseOnce_Server<Req, Resp>>);
static_assert(is_well_formed_v<RequestResponse_Client<Req, Resp>>);
static_assert(is_well_formed_v<RequestResponse_Server<Req, Resp>>);
static_assert(is_well_formed_v<RequestResponseLoop_Client<Req, Resp>>);
static_assert(is_well_formed_v<RequestResponseLoop_Server<Req, Resp>>);

static_assert(
    std::is_same_v<dual_of_t<dual_of_t<RequestResponseOnce_Client<Req, Resp>>>, RequestResponseOnce_Client<Req, Resp>>);
static_assert(
    std::is_same_v<dual_of_t<dual_of_t<RequestResponse_Client<Req, Resp>>>, RequestResponse_Client<Req, Resp>>);
static_assert(
    std::is_same_v<dual_of_t<dual_of_t<RequestResponseLoop_Client<Req, Resp>>>, RequestResponseLoop_Client<Req, Resp>>);

static_assert(std::is_same_v<PipelineSource<Job>, Loop<Send<Job, Continue>>>);
static_assert(std::is_same_v<PipelineSink<Job>, Loop<Recv<Job, Continue>>>);

static_assert(std::is_same_v<dual_of_t<PipelineSource<Job>>, PipelineSink<Job>>);
static_assert(std::is_same_v<dual_of_t<PipelineSink<Job>>, PipelineSource<Job>>);

static_assert(std::is_same_v<dual_of_t<dual_of_t<PipelineSource<Job>>>, PipelineSource<Job>>);

static_assert(is_well_formed_v<PipelineSource<Job>>);
static_assert(is_well_formed_v<PipelineSink<Job>>);

static_assert(std::is_same_v<PipelineStage<Req, Resp>, RequestResponse_Server<Req, Resp>>);

static_assert(std::is_same_v<dual_of_t<PipelineStage<Req, Resp>>, RequestResponse_Client<Req, Resp>>);
static_assert(std::is_same_v<dual_of_t<RequestResponse_Client<Req, Resp>>, PipelineStage<Req, Resp>>);

static_assert(is_well_formed_v<PipelineStage<Req, Resp>>);

static_assert(
    std::is_same_v<TxClient,
                   Send<Prepare, Loop<Select<Send<Req, Continue>, Send<Commit, Recv<Ack, End>>, Send<Abort, End>>>>>);
static_assert(
    std::is_same_v<TxServer,
                   Recv<Prepare, Loop<Offer<Recv<Req, Continue>, Recv<Commit, Send<Ack, End>>, Recv<Abort, End>>>>>);

static_assert(std::is_same_v<dual_of_t<TxClient>, TxServer>);
static_assert(std::is_same_v<dual_of_t<TxServer>, TxClient>);

static_assert(std::is_same_v<dual_of_t<dual_of_t<TxClient>>, TxClient>);

static_assert(is_well_formed_v<TxClient>);
static_assert(is_well_formed_v<TxServer>);
#endif  // CRUCIBLE_SESSION_PATTERN_SELF_TESTS_BASIC

#ifdef CRUCIBLE_SESSION_PATTERN_SELF_TESTS_FAN
static_assert(std::is_same_v<FanOut<0, Job>, End>);
static_assert(std::is_same_v<FanIn<0, Job>, End>);

static_assert(std::is_same_v<FanOut<1, Job>, Send<Job, End>>);
static_assert(std::is_same_v<FanIn<1, Job>, Recv<Job, End>>);

static_assert(std::is_same_v<FanOut<3, Job>, Send<Job, Send<Job, Send<Job, End>>>>);
static_assert(std::is_same_v<FanIn<3, Job>, Recv<Job, Recv<Job, Recv<Job, End>>>>);

static_assert(std::is_same_v<dual_of_t<FanOut<0, Job>>, FanIn<0, Job>>);
static_assert(std::is_same_v<dual_of_t<FanOut<1, Job>>, FanIn<1, Job>>);
static_assert(std::is_same_v<dual_of_t<FanOut<3, Job>>, FanIn<3, Job>>);
static_assert(std::is_same_v<dual_of_t<FanIn<5, Job>>, FanOut<5, Job>>);

static_assert(std::is_same_v<dual_of_t<dual_of_t<FanOut<7, Job>>>, FanOut<7, Job>>);

static_assert(std::is_same_v<Broadcast<0, Job>, FanOut<0, Job>>);
static_assert(std::is_same_v<Broadcast<4, Job>, FanOut<4, Job>>);

static_assert(is_well_formed_v<FanOut<0, Job>>);
static_assert(is_well_formed_v<FanOut<1, Job>>);
static_assert(is_well_formed_v<FanOut<32, Job>>);
static_assert(is_well_formed_v<FanIn<32, Job>>);

// These sizes exercise the halving at both boundaries: the two parts are equal
// for even N and differ by one for odd N.
static_assert(std::is_same_v<FanOut<2, Job>, Send<Job, Send<Job, End>>>);
static_assert(std::is_same_v<FanOut<4, Job>, Send<Job, Send<Job, Send<Job, Send<Job, End>>>>>);
static_assert(std::is_same_v<FanOut<5, Job>, Send<Job, Send<Job, Send<Job, Send<Job, Send<Job, End>>>>>>);

// The large-N witness matches only the head of the chain.  A well-formedness
// check would walk the whole chain and hit the template-depth limit itself.
// That the chain exists with the right head proves the construction ran.  The
// exact-match assertions above establish the tail.
namespace large_n_witness {
template <typename T>
struct head_is_send : std::false_type {};
template <typename M, typename K>
struct head_is_send<Send<M, K>> : std::true_type {};

template <typename T>
struct head_is_recv : std::false_type {};
template <typename M, typename K>
struct head_is_recv<Recv<M, K>> : std::true_type {};
}  // namespace large_n_witness

static_assert(large_n_witness::head_is_send<FanOut<2048, Job>>::value);
static_assert(large_n_witness::head_is_recv<FanIn<2048, Job>>::value);

// No large-N duality witness is possible here.  The dual operator is still
// linearly recursive, so it hits the depth limit on a chain this long.  The
// small-N duality assertions above carry the rule.

static_assert(std::is_same_v<ScatterGather<2, Task, Result>, Send<Task, Send<Task, Recv<Result, Recv<Result, End>>>>>);

static_assert(std::is_same_v<ScatterGather<0, Task, Result>, End>);

static_assert(std::is_same_v<ScatterGather<1, Task, Result>, RequestResponseOnce_Client<Task, Result>>);

static_assert(
    std::is_same_v<dual_of_t<ScatterGather<2, Task, Result>>, Recv<Task, Recv<Task, Send<Result, Send<Result, End>>>>>);

static_assert(std::is_same_v<dual_of_t<dual_of_t<ScatterGather<4, Task, Result>>>, ScatterGather<4, Task, Result>>);

static_assert(is_well_formed_v<ScatterGather<8, Task, Result>>);
#endif  // CRUCIBLE_SESSION_PATTERN_SELF_TESTS_FAN

#ifdef CRUCIBLE_SESSION_PATTERN_SELF_TESTS_BASIC
static_assert(std::is_same_v<MpmcProducer<Job>, Loop<Select<Send<Job, Continue>, End>>>);
static_assert(std::is_same_v<MpmcConsumer<Job>, Loop<Offer<Recv<Job, Continue>, End>>>);

static_assert(std::is_same_v<dual_of_t<MpmcProducer<Job>>, MpmcConsumer<Job>>);
static_assert(std::is_same_v<dual_of_t<MpmcConsumer<Job>>, MpmcProducer<Job>>);

static_assert(std::is_same_v<dual_of_t<dual_of_t<MpmcProducer<Job>>>, MpmcProducer<Job>>);

static_assert(is_well_formed_v<MpmcProducer<Job>>);
static_assert(is_well_formed_v<MpmcConsumer<Job>>);

static_assert(std::is_same_v<ConcreteCoord, Send<Prepare, Recv<Vote, Select<Send<Commit, End>, Send<Abort, End>>>>>);
static_assert(std::is_same_v<ConcreteFollower, Recv<Prepare, Send<Vote, Offer<Recv<Commit, End>, Recv<Abort, End>>>>>);

// The hand-written follower has to stay exactly the dual of the coordinator.
// These two catch a divergence if either side is edited alone.
static_assert(std::is_same_v<dual_of_t<ConcreteCoord>, ConcreteFollower>);
static_assert(std::is_same_v<dual_of_t<ConcreteFollower>, ConcreteCoord>);

static_assert(std::is_same_v<dual_of_t<dual_of_t<ConcreteCoord>>, ConcreteCoord>);

static_assert(is_well_formed_v<ConcreteCoord>);
static_assert(is_well_formed_v<ConcreteFollower>);

static_assert(std::is_same_v<SwimProbe_Client<Probe, Ack>, RequestResponseOnce_Client<Probe, Ack>>);
static_assert(std::is_same_v<SwimProbe_Server<Probe, Ack>, RequestResponseOnce_Server<Probe, Ack>>);

static_assert(std::is_same_v<dual_of_t<SwimProbe_Client<Probe, Ack>>, SwimProbe_Server<Probe, Ack>>);

static_assert(is_well_formed_v<SwimProbe_Client<Probe, Ack>>);
static_assert(is_well_formed_v<SwimProbe_Server<Probe, Ack>>);

static_assert(std::is_same_v<Handshake_Client<Hello, Welcome, Reject>,
                             Send<Hello, Offer<Recv<Welcome, End>, Recv<Reject, End>>>>);
static_assert(std::is_same_v<Handshake_Server<Hello, Welcome, Reject>,
                             Recv<Hello, Select<Send<Welcome, End>, Send<Reject, End>>>>);

static_assert(
    std::is_same_v<dual_of_t<Handshake_Client<Hello, Welcome, Reject>>, Handshake_Server<Hello, Welcome, Reject>>);
static_assert(
    std::is_same_v<dual_of_t<Handshake_Server<Hello, Welcome, Reject>>, Handshake_Client<Hello, Welcome, Reject>>);

static_assert(std::is_same_v<dual_of_t<dual_of_t<Handshake_Client<Hello, Welcome, Reject>>>,
                             Handshake_Client<Hello, Welcome, Reject>>);

static_assert(is_well_formed_v<Handshake_Client<Hello, Welcome, Reject>>);
static_assert(is_well_formed_v<Handshake_Server<Hello, Welcome, Reject>>);
#endif  // CRUCIBLE_SESSION_PATTERN_SELF_TESTS_BASIC

#ifdef CRUCIBLE_SESSION_PATTERN_SELF_TESTS_SUBTYPE
// The question "does P refine its double dual?" only means something when the
// dual operator is involutive on P.  It is not involutive everywhere: an Offer
// carrying a Sender annotation loses that annotation on the round trip, so the
// double dual is a structurally different protocol and a yes or no answer says
// nothing about the subtype lattice.  The involution check is a conjunct here
// so that such a shape reports false outright instead of returning whatever
// the structural comparison happens to produce.
template <typename P>
inline constexpr bool refines_self_and_double_dual_v =
    is_dual_involutive_v<P> && is_subtype_sync_v<P, P> && is_subtype_sync_v<P, dual_of_t<dual_of_t<P>>>;

static_assert(refines_self_and_double_dual_v<RequestResponseOnce_Client<Req, Resp>>);
static_assert(refines_self_and_double_dual_v<RequestResponseOnce_Server<Req, Resp>>);
static_assert(refines_self_and_double_dual_v<RequestResponse_Client<Req, Resp>>);
static_assert(refines_self_and_double_dual_v<RequestResponse_Server<Req, Resp>>);
static_assert(refines_self_and_double_dual_v<RequestResponseLoop_Client<Req, Resp>>);
static_assert(refines_self_and_double_dual_v<RequestResponseLoop_Server<Req, Resp>>);

static_assert(refines_self_and_double_dual_v<PipelineSource<Job>>);
static_assert(refines_self_and_double_dual_v<PipelineSink<Job>>);
static_assert(refines_self_and_double_dual_v<PipelineStage<Req, Resp>>);

static_assert(refines_self_and_double_dual_v<TxClient>);
static_assert(refines_self_and_double_dual_v<TxServer>);

static_assert(refines_self_and_double_dual_v<FanOut<3, Job>>);
static_assert(refines_self_and_double_dual_v<FanIn<3, Job>>);
static_assert(refines_self_and_double_dual_v<Broadcast<3, Job>>);
static_assert(refines_self_and_double_dual_v<ScatterGather<2, Task, Result>>);

static_assert(refines_self_and_double_dual_v<MpmcProducer<Job>>);
static_assert(refines_self_and_double_dual_v<MpmcConsumer<Job>>);

static_assert(refines_self_and_double_dual_v<ConcreteCoord>);
static_assert(refines_self_and_double_dual_v<ConcreteFollower>);

static_assert(refines_self_and_double_dual_v<SwimProbe_Client<Probe, Ack>>);
static_assert(refines_self_and_double_dual_v<SwimProbe_Server<Probe, Ack>>);

static_assert(refines_self_and_double_dual_v<Handshake_Client<Hello, Welcome, Reject>>);
static_assert(refines_self_and_double_dual_v<Handshake_Server<Hello, Welcome, Reject>>);

static_assert(equivalent_sync_v<PipelineStage<Req, Resp>, RequestResponse_Server<Req, Resp>>);
static_assert(equivalent_sync_v<Broadcast<4, Job>, FanOut<4, Job>>);
static_assert(equivalent_sync_v<ScatterGather<1, Task, Result>, RequestResponseOnce_Client<Task, Result>>);
static_assert(equivalent_sync_v<SwimProbe_Client<Probe, Ack>, RequestResponseOnce_Client<Probe, Ack>>);
static_assert(equivalent_sync_v<SwimProbe_Server<Probe, Ack>, RequestResponseOnce_Server<Probe, Ack>>);

// The close-free and in-band-close variants are distinct shapes by design, not
// two rungs of a subtype ladder.  Neither substitutes for the other.
static_assert(!is_subtype_sync_v<RequestResponse_Client<Req, Resp>, RequestResponseLoop_Client<Req, Resp>>);
static_assert(!is_subtype_sync_v<RequestResponseLoop_Client<Req, Resp>, RequestResponse_Client<Req, Resp>>);
static_assert(!is_subtype_sync_v<RequestResponse_Server<Req, Resp>, RequestResponseLoop_Server<Req, Resp>>);
static_assert(!is_subtype_sync_v<RequestResponseLoop_Server<Req, Resp>, RequestResponse_Server<Req, Resp>>);

// Refinement runs in one direction on each side.  A chooser may narrow its
// Select, because the peer already handles every branch it drops.  A responder
// may widen its Offer, because the extra branches are simply never picked.
// The reverse of either direction is unsound, which the negative assertions
// through the rest of this block pin down.
using RRLoopClientSendOnly = Loop<Select<Send<Req, Recv<Resp, Continue>>>>;
static_assert(is_strict_subtype_sync_v<RRLoopClientSendOnly, RequestResponseLoop_Client<Req, Resp>>);
static_assert(!is_subtype_sync_v<RequestResponseLoop_Client<Req, Resp>, RRLoopClientSendOnly>);

using RRLoopServerWithRetry = Loop<Offer<Recv<Req, Send<Resp, Continue>>, End, Recv<Retry, End>>>;
static_assert(is_strict_subtype_sync_v<RRLoopServerWithRetry, RequestResponseLoop_Server<Req, Resp>>);
static_assert(!is_subtype_sync_v<RequestResponseLoop_Server<Req, Resp>, RRLoopServerWithRetry>);

using TxClientOpsOnly = Send<Prepare, Loop<Select<Send<Req, Continue>>>>;
static_assert(is_strict_subtype_sync_v<TxClientOpsOnly, TxClient>);
static_assert(!is_subtype_sync_v<TxClient, TxClientOpsOnly>);

using TxServerWithCancel =
    Recv<Prepare, Loop<Offer<Recv<Req, Continue>, Recv<Commit, Send<Ack, End>>, Recv<Abort, End>, Recv<Cancel, End>>>>;
static_assert(is_strict_subtype_sync_v<TxServerWithCancel, TxServer>);
static_assert(!is_subtype_sync_v<TxServer, TxServerWithCancel>);

using MpmcProducerPushOnly = Loop<Select<Send<Job, Continue>>>;
static_assert(is_strict_subtype_sync_v<MpmcProducerPushOnly, MpmcProducer<Job>>);
static_assert(!is_subtype_sync_v<MpmcProducer<Job>, MpmcProducerPushOnly>);

using MpmcConsumerWithCancel = Loop<Offer<Recv<Job, Continue>, End, Recv<Cancel, End>>>;
static_assert(is_strict_subtype_sync_v<MpmcConsumerWithCancel, MpmcConsumer<Job>>);
static_assert(!is_subtype_sync_v<MpmcConsumer<Job>, MpmcConsumerWithCancel>);

using CoordCommitOnly = Send<Prepare, Recv<Vote, Select<Send<Commit, End>>>>;
static_assert(is_strict_subtype_sync_v<CoordCommitOnly, ConcreteCoord>);
static_assert(!is_subtype_sync_v<ConcreteCoord, CoordCommitOnly>);

using FollowerWithRetry = Recv<Prepare, Send<Vote, Offer<Recv<Commit, End>, Recv<Abort, End>, Recv<Retry, End>>>>;
static_assert(is_strict_subtype_sync_v<FollowerWithRetry, ConcreteFollower>);
static_assert(!is_subtype_sync_v<ConcreteFollower, FollowerWithRetry>);

using HandshakeClientWithRetry = Send<Hello, Offer<Recv<Welcome, End>, Recv<Reject, End>, Recv<Retry, End>>>;
static_assert(is_strict_subtype_sync_v<HandshakeClientWithRetry, Handshake_Client<Hello, Welcome, Reject>>);
static_assert(!is_subtype_sync_v<Handshake_Client<Hello, Welcome, Reject>, HandshakeClientWithRetry>);

using HandshakeServerWelcomeOnly = Recv<Hello, Select<Send<Welcome, End>>>;
static_assert(is_strict_subtype_sync_v<HandshakeServerWelcomeOnly, Handshake_Server<Hello, Welcome, Reject>>);
static_assert(!is_subtype_sync_v<Handshake_Server<Hello, Welcome, Reject>, HandshakeServerWelcomeOnly>);
#endif  // CRUCIBLE_SESSION_PATTERN_SELF_TESTS_SUBTYPE

#ifdef CRUCIBLE_SESSION_PATTERN_SELF_TESTS_CRASH
// The public aliases keep their wire shape and so stay crash-oblivious.  That
// status is marked here rather than fixed, and crash-aware witnesses are built
// separately below.

template <typename Proto, typename SelfRole>
using PendingCrashContract =
    PatternCrashSafety<Proto, ReliableSet<SelfRole>, CrashSafetyPending, BaselinePatternNeedsCrashAwareVariant>;

template <typename Proto, typename SelfRole>
using VerifiedCrashContract = PatternCrashSafety<Proto, ReliableSet<SelfRole>, CrashSafetyVerified>;

template <typename Contract>
consteval bool pending_contract_ok() {
    return pattern_crash_safety_pending_v<Contract> && !pattern_crash_safety_verified_v<Contract>;
}

template <typename Contract>
consteval bool verified_contract_ok() {
    return pattern_crash_safety_verified_v<Contract> && !pattern_crash_safety_pending_v<Contract>;
}

// The reliability set names the one role assumed not to crash: the local
// endpoint.  The peer carries no such assumption, so a crash-aware variant of
// any of these needs an explicit crash branch for it.
static_assert(pending_contract_ok<PendingCrashContract<RequestResponseOnce_Client<Req, Resp>, ClientRole>>());
static_assert(pending_contract_ok<PendingCrashContract<RequestResponseOnce_Server<Req, Resp>, ServerRole>>());
static_assert(pending_contract_ok<PendingCrashContract<RequestResponse_Client<Req, Resp>, ClientRole>>());
static_assert(pending_contract_ok<PendingCrashContract<RequestResponse_Server<Req, Resp>, ServerRole>>());
static_assert(pending_contract_ok<PendingCrashContract<RequestResponseLoop_Client<Req, Resp>, ClientRole>>());
static_assert(pending_contract_ok<PendingCrashContract<RequestResponseLoop_Server<Req, Resp>, ServerRole>>());

static_assert(pending_contract_ok<PendingCrashContract<PipelineSource<Job>, SourceRole>>());
static_assert(pending_contract_ok<PendingCrashContract<PipelineSink<Job>, SinkRole>>());
static_assert(pending_contract_ok<PendingCrashContract<PipelineStage<Req, Resp>, StageRole>>());

static_assert(pending_contract_ok<PendingCrashContract<TxClient, ClientRole>>());
static_assert(pending_contract_ok<PendingCrashContract<TxServer, ServerRole>>());

static_assert(pending_contract_ok<PendingCrashContract<FanOut<3, Job>, CoordinatorRole>>());
static_assert(pending_contract_ok<PendingCrashContract<FanIn<3, Job>, CollectorRole>>());
static_assert(pending_contract_ok<PendingCrashContract<Broadcast<3, Job>, CoordinatorRole>>());
static_assert(pending_contract_ok<PendingCrashContract<ScatterGather<2, Task, Result>, CoordinatorRole>>());

static_assert(pending_contract_ok<PendingCrashContract<MpmcProducer<Job>, ProducerRole>>());
static_assert(pending_contract_ok<PendingCrashContract<MpmcConsumer<Job>, ConsumerRole>>());

static_assert(pending_contract_ok<PendingCrashContract<ConcreteCoord, CoordinatorRole>>());
static_assert(pending_contract_ok<PendingCrashContract<ConcreteFollower, FollowerRole>>());

static_assert(pending_contract_ok<PendingCrashContract<SwimProbe_Client<Probe, Ack>, ClientRole>>());
static_assert(pending_contract_ok<PendingCrashContract<SwimProbe_Server<Probe, Ack>, ServerRole>>());

static_assert(pending_contract_ok<PendingCrashContract<Handshake_Client<Hello, Welcome, Reject>, ClientRole>>());
static_assert(pending_contract_ok<PendingCrashContract<Handshake_Server<Hello, Welcome, Reject>, ServerRole>>());

// The walker only inspects Offer nodes, so a protocol with none of them cannot
// be judged by it and stays marked pending above.  A bare Recv carries no
// sender-role context, so nothing in the local type says whether it came from
// a peer that may crash.
static_assert(!every_offer_has_crash_branch_for_peer_v<RequestResponseLoop_Server<Req, Resp>, ClientRole>);
static_assert(!every_offer_has_crash_branch_for_peer_v<TxServer, ClientRole>);
static_assert(!every_offer_has_crash_branch_for_peer_v<MpmcConsumer<Job>, ProducerRole>);
static_assert(!every_offer_has_crash_branch_for_peer_v<ConcreteFollower, CoordinatorRole>);
static_assert(!every_offer_has_crash_branch_for_peer_v<Handshake_Client<Hello, Welcome, Reject>, ServerRole>);

using CrashAwareOnceClient = Send<Req, Offer<Sender<ServerRole>, Recv<Resp, End>, Recv<Crash<ServerRole>, End>>>;
static_assert(every_offer_has_crash_branch_for_peer_v<CrashAwareOnceClient, ServerRole>);
static_assert(verified_contract_ok<VerifiedCrashContract<CrashAwareOnceClient, ClientRole>>());

using CrashAwareLoopServer =
    Loop<Offer<Sender<ClientRole>, Recv<Req, Send<Resp, Continue>>, End, Recv<Crash<ClientRole>, End>>>;
static_assert(every_offer_has_crash_branch_for_peer_v<CrashAwareLoopServer, ClientRole>);
static_assert(verified_contract_ok<VerifiedCrashContract<CrashAwareLoopServer, ServerRole>>());

using CrashAwareTxServer =
    Recv<Prepare, Loop<Offer<Sender<ClientRole>, Recv<Req, Continue>, Recv<Commit, Send<Ack, End>>, Recv<Abort, End>,
                             Recv<Crash<ClientRole>, End>>>>;
static_assert(every_offer_has_crash_branch_for_peer_v<CrashAwareTxServer, ClientRole>);
static_assert(verified_contract_ok<VerifiedCrashContract<CrashAwareTxServer, ServerRole>>());

using CrashAwareMpmcConsumer =
    Loop<Offer<Sender<ProducerRole>, Recv<Job, Continue>, End, Recv<Crash<ProducerRole>, End>>>;
static_assert(every_offer_has_crash_branch_for_peer_v<CrashAwareMpmcConsumer, ProducerRole>);
static_assert(verified_contract_ok<VerifiedCrashContract<CrashAwareMpmcConsumer, ConsumerRole>>());

using CrashAwareCoord =
    Send<Prepare, Offer<Sender<FollowerRole>, Recv<Vote, Select<Send<Commit, End>, Send<Abort, End>>>,
                        Recv<Crash<FollowerRole>, End>>>;
static_assert(every_offer_has_crash_branch_for_peer_v<CrashAwareCoord, FollowerRole>);
static_assert(verified_contract_ok<VerifiedCrashContract<CrashAwareCoord, CoordinatorRole>>());

using CrashAwareFollower = Recv<
    Prepare,
    Send<Vote, Offer<Sender<CoordinatorRole>, Recv<Commit, End>, Recv<Abort, End>, Recv<Crash<CoordinatorRole>, End>>>>;
static_assert(every_offer_has_crash_branch_for_peer_v<CrashAwareFollower, CoordinatorRole>);
static_assert(verified_contract_ok<VerifiedCrashContract<CrashAwareFollower, FollowerRole>>());

using CrashAwareHandshakeClient =
    Send<Hello, Offer<Sender<ServerRole>, Recv<Welcome, End>, Recv<Reject, End>, Recv<Crash<ServerRole>, End>>>;
static_assert(every_offer_has_crash_branch_for_peer_v<CrashAwareHandshakeClient, ServerRole>);
static_assert(verified_contract_ok<VerifiedCrashContract<CrashAwareHandshakeClient, ClientRole>>());
#endif  // CRUCIBLE_SESSION_PATTERN_SELF_TESTS_CRASH

#ifdef CRUCIBLE_SESSION_PATTERN_SELF_TESTS_DELEGATE
// None of these patterns carries linear authority beyond its own handle, so
// every one of them is delegate-compatible.  Permission-bearing delegated
// payloads are a separate concern and are not exercised here.

using DelegateRecoveryK = Offer<Sender<DelegatedRecipientRole>, Recv<Crash<DelegatedRecipientRole>, End>>;

template <typename Proto>
using CompatibleDelegateContract = PatternDelegateCompatibility<Proto, DelegateCompatible>;

template <typename Proto>
consteval bool delegate_compatible_pattern_ok() {
    using Contract = CompatibleDelegateContract<Proto>;
    static_assert(pattern_delegate_compatible_v<Contract>);
    static_assert(!pattern_delegate_pending_v<Contract>);
    static_assert(is_delegate_compatible_v<Proto>);
    static_assert(can_delegate_v<Proto, DelegatedRecipientRole>);
    static_assert(DelegatesTo<Delegate<Proto, End>, Proto>);
    static_assert(AcceptsFrom<Accept<Proto, End>, Proto>);

    using Propagation = delegated_crash_propagation_t<Proto, DelegatedRecipientRole, DelegateRecoveryK>;
    static_assert(!std::is_same_v<Propagation, IllFormed>);
    assert_delegated_crash_propagates<Proto, DelegatedRecipientRole, DelegateRecoveryK>();
    return true;
}

static_assert(delegate_compatible_pattern_ok<RequestResponseOnce_Client<Req, Resp>>());
static_assert(delegate_compatible_pattern_ok<RequestResponseOnce_Server<Req, Resp>>());
static_assert(delegate_compatible_pattern_ok<RequestResponse_Client<Req, Resp>>());
static_assert(delegate_compatible_pattern_ok<RequestResponse_Server<Req, Resp>>());
static_assert(delegate_compatible_pattern_ok<RequestResponseLoop_Client<Req, Resp>>());
static_assert(delegate_compatible_pattern_ok<RequestResponseLoop_Server<Req, Resp>>());

static_assert(delegate_compatible_pattern_ok<PipelineSource<Job>>());
static_assert(delegate_compatible_pattern_ok<PipelineSink<Job>>());
static_assert(delegate_compatible_pattern_ok<PipelineStage<Req, Resp>>());

static_assert(delegate_compatible_pattern_ok<TxClient>());
static_assert(delegate_compatible_pattern_ok<TxServer>());

static_assert(delegate_compatible_pattern_ok<FanOut<3, Job>>());
static_assert(delegate_compatible_pattern_ok<FanIn<3, Job>>());
static_assert(delegate_compatible_pattern_ok<Broadcast<3, Job>>());
static_assert(delegate_compatible_pattern_ok<ScatterGather<2, Task, Result>>());

static_assert(delegate_compatible_pattern_ok<MpmcProducer<Job>>());
static_assert(delegate_compatible_pattern_ok<MpmcConsumer<Job>>());

static_assert(delegate_compatible_pattern_ok<ConcreteCoord>());
static_assert(delegate_compatible_pattern_ok<ConcreteFollower>());

static_assert(delegate_compatible_pattern_ok<SwimProbe_Client<Probe, Ack>>());
static_assert(delegate_compatible_pattern_ok<SwimProbe_Server<Probe, Ack>>());

static_assert(delegate_compatible_pattern_ok<Handshake_Client<Hello, Welcome, Reject>>());
static_assert(delegate_compatible_pattern_ok<Handshake_Server<Hello, Welcome, Reject>>());

using DelegateRequestResponseServer = Delegate<RequestResponse_Server<Req, Resp>, Recv<Result, End>>;
using AcceptRequestResponseServer = dual_of_t<DelegateRequestResponseServer>;

static_assert(DelegatesTo<DelegateRequestResponseServer, RequestResponse_Server<Req, Resp>>);
static_assert(AcceptsFrom<AcceptRequestResponseServer, RequestResponse_Server<Req, Resp>>);
static_assert(is_well_formed_v<DelegateRequestResponseServer>);
static_assert(is_well_formed_v<AcceptRequestResponseServer>);

using DelegateThreePartyProjection = Delegate<ScatterGather<2, Task, Result>, Delegate<FanOut<3, Job>, End>>;
using AcceptThreePartyProjection = dual_of_t<DelegateThreePartyProjection>;

static_assert(DelegatesTo<DelegateThreePartyProjection, ScatterGather<2, Task, Result>>);
static_assert(AcceptsFrom<AcceptThreePartyProjection, ScatterGather<2, Task, Result>>);
static_assert(is_well_formed_v<DelegateThreePartyProjection>);
static_assert(is_well_formed_v<AcceptThreePartyProjection>);
#endif  // CRUCIBLE_SESSION_PATTERN_SELF_TESTS_DELEGATE

#ifdef CRUCIBLE_SESSION_PATTERN_SELF_TESTS_COMPOSE
// Composition replaces every End in the left protocol, not just the last one.
// Composing a handshake therefore routes the reject branch into the follow-on
// protocol as well as the welcome branch.  That is right only when a rejection
// is meant to be retried on the same channel.  A protocol where rejection ends
// the session has to be written out with an explicit End in that arm instead
// of composed.  The pair below is chosen because all of its branches do
// continue into the same continuation.

using HandshakeThenLoop_Client = compose_t<Handshake_Client<Hello, Welcome, Reject>, RequestResponse_Client<Req, Resp>>;

using HandshakeThenLoop_Server = compose_t<Handshake_Server<Hello, Welcome, Reject>, RequestResponse_Server<Req, Resp>>;

static_assert(is_well_formed_v<HandshakeThenLoop_Client>);
static_assert(is_well_formed_v<HandshakeThenLoop_Server>);

// Dualizing a composition gives the composition of the duals.
static_assert(std::is_same_v<dual_of_t<HandshakeThenLoop_Client>, HandshakeThenLoop_Server>);

static_assert(std::is_same_v<dual_of_t<dual_of_t<HandshakeThenLoop_Client>>, HandshakeThenLoop_Client>);

static_assert(
    std::is_same_v<compose_t<RequestResponseOnce_Client<Req, Resp>, End>, RequestResponseOnce_Client<Req, Resp>>);
static_assert(std::is_same_v<compose_t<FanOut<5, Job>, End>, FanOut<5, Job>>);
static_assert(std::is_same_v<compose_t<FanIn<3, Job>, End>, FanIn<3, Job>>);
static_assert(
    std::is_same_v<compose_t<Handshake_Client<Hello, Welcome, Reject>, End>, Handshake_Client<Hello, Welcome, Reject>>);
static_assert(std::is_same_v<compose_t<ConcreteCoord, End>, ConcreteCoord>);

// Continue is left alone by composition.  Replacing it the way End is replaced
// would silently rewrite every looping protocol that gets composed.
static_assert(std::is_same_v<compose_t<RequestResponse_Client<Req, Resp>, End>, RequestResponse_Client<Req, Resp>>);
static_assert(std::is_same_v<compose_t<MpmcProducer<Job>, End>, MpmcProducer<Job>>);
static_assert(std::is_same_v<compose_t<TxClient, End>, TxClient>);

static_assert(is_well_formed_v<FanOut<64, Job>>);
static_assert(is_well_formed_v<FanIn<64, Job>>);
static_assert(is_well_formed_v<ScatterGather<32, Task, Result>>);
static_assert(std::is_same_v<dual_of_t<FanOut<64, Job>>, FanIn<64, Job>>);
#endif  // CRUCIBLE_SESSION_PATTERN_SELF_TESTS_COMPOSE

}  // namespace crucible::safety::proto::pattern::detail::pattern_self_test
#endif  // CRUCIBLE_SESSION_SELF_TESTS
