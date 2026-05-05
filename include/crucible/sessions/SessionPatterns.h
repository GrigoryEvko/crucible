#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::safety::proto::pattern — session-type pattern library
//
// A pattern is a one-line TYPE ALIAS for a common communication shape.
// Every pattern here is layered on the L1 combinators (Send / Recv /
// Select / Offer / Loop / Continue / End) from Session.h and inherits
// their dual/compose/well-formedness machinery without needing its own
// specialisations.
//
// The value proposition:
//
//   * Self-documenting — the pattern name IS the protocol.  A reader
//     encountering RequestResponse_Client<Query, Result> knows what
//     wire behaviour it describes without peeling back the type.
//   * Zero-overhead — every pattern is a compile-time alias.  Dual,
//     compose, and handle dispatch all run at the combinator level.
//   * Composable — patterns compose via compose_t<P, Q>; patterns
//     refine via is_subtype_sync_v; patterns dualise via dual_of_t.
//   * Self-verifying — every pattern ships with static_assert checks
//     for dual involution, well-formedness, structural expansion, and
//     synchronous subtype refinement against documented aliases and
//     local branch-refinement witnesses.
//   * Crash-status explicit — baseline pattern aliases preserve their
//     original crash-oblivious wire shape.  The self-test harness marks
//     those aliases with PatternCrashSafety<..., CrashSafetyPending>
//     contracts for unreliable peers, and separately proves concrete
//     Crash<>-branch witnesses with SessionCrash.h's Offer walker.
//
// Subtype refinement note: this header does not expose public
// `*_Strong` / `*_Weak` pattern aliases yet.  The self-test block
// therefore verifies equivalence for aliases that are intentionally
// identical, double-dual subtype refinement for every named pattern,
// and strict local witnesses for the patterns whose structure admits
// true Gay-Hole refinement today: Select narrowing and Offer widening.
// Public named strong/weak variants remain future work once protocol
// evolution APIs need stable names for those witnesses.
//
// ─── Scope and non-goals ──────────────────────────────────────────
//
// This layer ships LOCAL (per-participant) session types.  A pattern
// like FanOut<N, Msg> describes ONE participant's view of an N-way
// fan-out — specifically the coordinator's view.  The full multiparty
// form — global type G with projection G ↾ p to each role's local
// type — lives in the (pending) SessionGlobal.h layer (task
// SEPLOG-H2g / session_types.md §II.4).  Every pattern whose natural
// shape is inherently multiparty is flagged in its doc with the
// caveat that only the local-view projection is shipped today.
//
// This is NOT a blocker for the patterns' usefulness:  every intra-
// process binary channel in CRUCIBLE.md §IV can be expressed with the
// local-view aliases here, and the multiparty forms that need L4 will
// drop in once that layer ships without breaking existing users of
// these patterns.
//
// ─── Pattern index ─────────────────────────────────────────────────
//
//   Request / response:
//       RequestResponseOnce_Client<Req, Resp>      single round-trip
//       RequestResponseOnce_Server<Req, Resp>      peer of above
//       RequestResponse_Client<Req, Resp>          looping forever
//       RequestResponse_Server<Req, Resp>          peer of above
//       RequestResponseLoop_Client<Req, Resp>      loop-with-explicit-close
//       RequestResponseLoop_Server<Req, Resp>      peer of above
//
//   Pipeline:
//       PipelineSource<Out>                        pure producer (no close)
//       PipelineSink<In>                           pure consumer (no close)
//       PipelineStage<In, Out>                     relay stage (recv,
//                                                  transform, send,
//                                                  repeat)
//
//   Transaction (Begin/Ops/Commit-or-Rollback, task #101):
//       Transaction_Client<Begin, Op, Commit, Ack, Rollback>
//       Transaction_Server<Begin, Op, Commit, Ack, Rollback>
//
//   Fan-out / fan-in:
//       FanOut<N, Msg>                             N sequential sends
//       FanIn<N, Msg>                              N sequential recvs
//       Broadcast<N, Msg>                          alias for FanOut
//       ScatterGather<N, Task, Result>             send N, recv N
//
//   MPMC primitive (§V of session_types.md):
//       MpmcProducer<T>                            loop-push-or-close
//       MpmcConsumer<T>                            loop-deliver-or-close
//
//   Two-phase commit (NBAC binary fragment, BSYZ22):
//       TwoPhaseCommit_Coord<Prepare, Vote, Commit, Abort>
//       TwoPhaseCommit_Follower<Prepare, Vote, Commit, Abort>
//
//   SWIM gossip probe:
//       SwimProbe_Client<Probe, Ack>               single probe/ack
//       SwimProbe_Server<Probe, Ack>               peer of above
//
//   Handshake:
//       Handshake_Client<Hello, Welcome, Reject>   send hello, offer
//                                                  welcome-or-reject
//       Handshake_Server<Hello, Welcome, Reject>   peer of above
//
// Every `*_Client` / `*_Server` pair satisfies duality:
//     dual_of_t<Pattern_Client<...>>  ==  Pattern_Server<...>
// This is enforced by a static_assert at the top of each pattern's
// self-test block.
//
// ─── References ───────────────────────────────────────────────────
//
//   Honda 1998 — binary session types; request-response, pipeline
//     stage, and 2PC fragments established here.
//   Honda-Yoshida-Carbone 2008 / JACM16 — multiparty session types;
//     scatter-gather and broadcast as multiparty primitives (local-
//     view here, multiparty later once SessionGlobal.h ships).
//   Barwell-Scalas-Yoshida-Zhou 2022 — synchronous crash-stop MPST;
//     TwoPhaseCommit_Coord/Follower here are the binary fragment of
//     BSYZ22's NBAC case study.
//   Nikolaev 2019 — Scalable Circular Queue; MpmcProducer/Consumer
//     are the session-type views of SCQ producer/consumer endpoints
//     (see session_types.md §V for the multiparty form).
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionSubtype.h>
#ifdef CRUCIBLE_SESSION_SELF_TESTS
#include <crucible/sessions/SessionCrash.h>
#endif

#include <cstddef>
#include <type_traits>

namespace crucible::safety::proto {
template <typename... Roles>
struct ReliableSet;
}  // namespace crucible::safety::proto

namespace crucible::safety::proto::pattern {

// ═════════════════════════════════════════════════════════════════════
// ── Crash-safety contract markers ─────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// The aliases below are intentionally compile-time-only markers.  They
// let a pattern declaration say whether the local protocol has been
// proved crash-aware under some reliability set R without changing the
// underlying protocol type or adding runtime checks.

struct CrashSafetyVerified {};
struct CrashSafetyPending {};
struct BaselinePatternNeedsCrashAwareVariant {};

template <typename Proto, typename R, typename Status,
          typename Reason = BaselinePatternNeedsCrashAwareVariant>
struct PatternCrashSafety {
    using protocol     = Proto;
    using reliable_set = R;
    using status       = Status;
    using reason       = Reason;

    static constexpr bool verified =
        std::is_same_v<Status, CrashSafetyVerified>;
    static constexpr bool pending =
        std::is_same_v<Status, CrashSafetyPending>;
};

template <typename Contract>
inline constexpr bool pattern_crash_safety_verified_v = Contract::verified;

template <typename Contract>
inline constexpr bool pattern_crash_safety_pending_v = Contract::pending;

// ═════════════════════════════════════════════════════════════════════
// ── Request / response ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// Single round-trip: client sends Req, receives Resp, terminates.
// Use for one-shot RPC-style calls (FLR reset request, Cipher
// cold-tier fetch, k8s operator reconcile).
template <typename Req, typename Resp>
using RequestResponseOnce_Client = Send<Req, Recv<Resp, End>>;

// Peer of RequestResponseOnce_Client.
template <typename Req, typename Resp>
using RequestResponseOnce_Server = Recv<Req, Send<Resp, End>>;

// Looping request-response, never-terminating: client repeatedly sends
// Req and awaits Resp.  Appropriate for long-lived serving loops where
// termination is out-of-band (connection teardown at the transport
// layer, peer death detected via SWIM).  Use the _Loop_ variant below
// when you need an in-band close.
template <typename Req, typename Resp>
using RequestResponse_Client = Loop<Send<Req, Recv<Resp, Continue>>>;

// Peer of RequestResponse_Client — also serves as the PipelineStage
// alias below (a relay stage IS a request-response server).
template <typename Req, typename Resp>
using RequestResponse_Server = Loop<Recv<Req, Send<Resp, Continue>>>;

// Loop with explicit close branch: in each iteration, the client
// chooses between "send another request" and "close the session".
// Gives graceful termination — protocol end is in-band and visible
// to both peers, not a transport-layer surprise.
template <typename Req, typename Resp>
using RequestResponseLoop_Client = Loop<Select<
    Send<Req, Recv<Resp, Continue>>,
    End>>;

// Peer of RequestResponseLoop_Client.
template <typename Req, typename Resp>
using RequestResponseLoop_Server = Loop<Offer<
    Recv<Req, Send<Resp, Continue>>,
    End>>;

// ═════════════════════════════════════════════════════════════════════
// ── Pipeline ───────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// Pure source: emit a stream of Outs forever.  Used at the HEAD of a
// pipeline (nothing received; only produced).  Example in Crucible:
// the Vessel dispatch thread feeding TraceRing is effectively a
// PipelineSource<TraceEntry> from the producer-side view.
template <typename Out>
using PipelineSource = Loop<Send<Out, Continue>>;

// Pure sink: receive a stream of Ins forever.  Used at the TAIL of a
// pipeline (nothing produced downstream; only consumed).  Example:
// the KernelCache publisher reading compiled-kernel deliveries.
template <typename In>
using PipelineSink = Loop<Recv<In, Continue>>;

// A middle-pipeline-stage relay: receive an In, send an Out, repeat.
// Structurally identical to RequestResponse_Server — the alias simply
// names the intent ("this is a pipeline stage, not a request handler").
//
// For a real multi-stage pipeline (Drain → Build → Transform → Compile
// from CRUCIBLE.md §IV.3), each stage has its own PipelineStage<In_i,
// Out_i> local type; the stages are COMPOSED at the multiparty level,
// not at the type level here.  The multiparty form arrives with
// SessionGlobal.h.
template <typename In, typename Out>
using PipelineStage = RequestResponse_Server<In, Out>;

// ═════════════════════════════════════════════════════════════════════
// ── Transaction (Begin / Ops / Commit-or-Rollback) ─────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Task #101's target shape for the Transaction.h refactor.  Protocol:
//
//   Client:                               Server:
//     1. Send Begin                         1. Recv Begin
//     2. Loop:                              2. Loop:
//        - Send Op, continue                   - Recv Op, continue
//        - Send Commit, recv Ack, end          - Recv Commit, send Ack, end
//        - Send Rollback, end                  - Recv Rollback, end
//
// Three-way client-driven Select: the CLIENT decides whether to issue
// another operation, commit (with server acknowledgement), or roll back
// (one-way).  The server-initiated-abort extension — server decides to
// abort due to isolation violation — would nest an Offer over Select
// on the Op branch; omitted here, add as Transaction_ServerAbort if
// needed.
//
// Positional branches:
//   index 0 = Op        (continue the transaction)
//   index 1 = Commit    (finalise with server ack)
//   index 2 = Rollback  (abandon, no ack)

template <typename Begin, typename Op, typename Commit, typename Ack, typename Rollback>
using Transaction_Client = Send<Begin, Loop<Select<
    Send<Op, Continue>,
    Send<Commit, Recv<Ack, End>>,
    Send<Rollback, End>>>>;

template <typename Begin, typename Op, typename Commit, typename Ack, typename Rollback>
using Transaction_Server = Recv<Begin, Loop<Offer<
    Recv<Op, Continue>,
    Recv<Commit, Send<Ack, End>>,
    Recv<Rollback, End>>>>;

// ═════════════════════════════════════════════════════════════════════
// ── Fan-out / fan-in ───────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail {

// fan_out_helper<N, Msg>::type  =  Send<Msg, Send<Msg, ..., End>>  (N sends)
template <std::size_t N, typename Msg>
struct fan_out_helper {
    using type = Send<Msg, typename fan_out_helper<N - 1, Msg>::type>;
};

template <typename Msg>
struct fan_out_helper<0, Msg> {
    using type = End;
};

// fan_in_helper<N, Msg>::type   =  Recv<Msg, Recv<Msg, ..., End>>  (N recvs)
template <std::size_t N, typename Msg>
struct fan_in_helper {
    using type = Recv<Msg, typename fan_in_helper<N - 1, Msg>::type>;
};

template <typename Msg>
struct fan_in_helper<0, Msg> {
    using type = End;
};

// ─── #375 SEPLOG-PERF-5: divide-and-conquer chain builders ────────
//
// The recursive `fan_{out,in}_helper` above has instantiation depth
// proportional to N — each `fan_out_helper<N>` requires
// `fan_out_helper<N - 1>` to be complete before the `Send<Msg, ...>`
// wrap can be evaluated.  GCC's default `-ftemplate-depth=900` caps
// the achievable N at roughly 1100; protocols that genuinely need
// `FanOut<2000, Msg>` (large scatter-gather, per-shard broadcast,
// MPST adapter codegen for 1000+ peers) hit a hard compile error.
//
// The accumulator-passing helpers below split N into halves at every
// step.  For `chain_send_with_tail<N, Msg, Tail>`:
//
//   chain_send_with_tail<N, Msg, Tail>
//     = chain_send_with_tail<N/2, Msg,
//         chain_send_with_tail<N - N/2, Msg, Tail>::type>::type
//
// Total work is still O(N) (must produce N nested wrappers), but
// instantiation DEPTH is O(log N).  At log₂(N) ≤ 900 the limit lifts
// from N ≈ 1100 to N ≈ 2^900 — effectively unlimited.  Cost on small
// N is unchanged: the base cases for N ∈ {0, 1} short-circuit
// immediately, and the divide-and-conquer fan-out at N ≤ 4 is
// equivalent in instantiation count to the linear form.
//
// Public-facing FanOut / FanIn aliases switch to these below.

template <std::size_t N, typename Msg, typename Tail>
struct chain_send_with_tail {
    static constexpr std::size_t Half = N / 2;
    static constexpr std::size_t Rest = N - Half;
    // Build the right portion (Rest copies of Send wrapped around
    // Tail), then build the left portion (Half copies) on top of
    // the right's result.
    using right = typename chain_send_with_tail<Rest, Msg, Tail>::type;
    using type  = typename chain_send_with_tail<Half, Msg, right>::type;
};

template <typename Msg, typename Tail>
struct chain_send_with_tail<0, Msg, Tail> { using type = Tail; };

template <typename Msg, typename Tail>
struct chain_send_with_tail<1, Msg, Tail> { using type = Send<Msg, Tail>; };

template <std::size_t N, typename Msg, typename Tail>
struct chain_recv_with_tail {
    static constexpr std::size_t Half = N / 2;
    static constexpr std::size_t Rest = N - Half;
    using right = typename chain_recv_with_tail<Rest, Msg, Tail>::type;
    using type  = typename chain_recv_with_tail<Half, Msg, right>::type;
};

template <typename Msg, typename Tail>
struct chain_recv_with_tail<0, Msg, Tail> { using type = Tail; };

template <typename Msg, typename Tail>
struct chain_recv_with_tail<1, Msg, Tail> { using type = Recv<Msg, Tail>; };

}  // namespace detail

// N sequential sends of Msg — coordinator's local view of a 1-to-N
// fan-out.  At the multiparty level this would project to each worker
// receiving a single Msg; we ship the coordinator's projection here
// and the worker receives via RequestResponseOnce_Server<Msg, ...> or
// similar per their own protocol obligation.
//
// Backed by the O(log N)-depth `chain_send_with_tail` divide-and-
// conquer helper (#375 SEPLOG-PERF-5).  Reachable N is bounded by
// 2^template-depth instead of template-depth, lifting the practical
// cap from ~1100 to effectively unlimited.
template <std::size_t N, typename Msg>
using FanOut = typename detail::chain_send_with_tail<N, Msg, End>::type;

// N sequential receives — collector's local view of an N-to-1 fan-in.
template <std::size_t N, typename Msg>
using FanIn = typename detail::chain_recv_with_tail<N, Msg, End>::type;

// Broadcast is syntactically identical to FanOut.  The alias names
// the intent: "I'm sending the same message to each of N peers" vs
// "I'm sending N distinct messages in some order".  No structural
// difference at the type level — a runtime broadcast implementation
// may elide wire duplicates (see ContentAddressed<T>, task #361).
template <std::size_t N, typename Msg>
using Broadcast = FanOut<N, Msg>;

// Scatter-gather: send N Tasks, then receive N Results.  This is the
// COORDINATOR's view; each worker sees RequestResponseOnce_Server<Task,
// Result> individually.  The coordinator doesn't constrain which Task
// maps to which Result at the type level — that invariant is the
// runtime's obligation (usually index-preserving).
template <std::size_t N, typename Task, typename Result>
using ScatterGather = compose_t<FanOut<N, Task>, FanIn<N, Result>>;

// ═════════════════════════════════════════════════════════════════════
// ── MPMC primitive (§V of session_types.md) ─────────────────────────
// ═════════════════════════════════════════════════════════════════════

// Producer's local view of a multi-producer / multi-consumer channel.
// In each iteration, the producer chooses between pushing a value or
// closing the channel.  The dual is MpmcConsumer<T>.
//
// Positional branches (no labels): branch 0 = push, branch 1 = close.
// The MPMC protocol's precise-async subtyping (§V.4 of session_types.md)
// admits producer pipelining — multiple producers can push without
// waiting for consumer delivery — under the SISO decomposition of
// ⩽_a (task SEPLOG-I6).
template <typename T>
using MpmcProducer = Loop<Select<
    Send<T, Continue>,
    End>>;

// Consumer's local view — accepts either a delivery or a close signal.
template <typename T>
using MpmcConsumer = Loop<Offer<
    Recv<T, Continue>,
    End>>;

// ═════════════════════════════════════════════════════════════════════
// ── Two-phase commit (NBAC binary fragment, BSYZ22) ────────────────
// ═════════════════════════════════════════════════════════════════════

// Coordinator side of a Non-Blocking Atomic Commit (2PC) protocol.
// Sequence:
//   1. Send Prepare to follower
//   2. Recv Vote back
//   3. Select: Send Commit (if vote yes and all peers ack) OR
//              Send Abort  (if any vote no or timeout)
//
// BSYZ22 §6 gives the multiparty form with crash-stop extensions;
// the binary fragment here is sufficient for the coordinator's pair-
// wise interaction with each follower.  This baseline alias is the
// crash-oblivious wire shape; crash-aware 2PC wraps the Vote/decision
// points in Sender<>-annotated Offer branches carrying Crash<Peer>.
template <typename Prepare, typename Vote, typename Commit, typename Abort>
using TwoPhaseCommit_Coord =
    Send<Prepare,
    Recv<Vote,
    Select<
        Send<Commit, End>,
        Send<Abort,  End>>>>;

// Follower side — dual of coordinator.  Written out literally (rather
// than as dual_of_t<...>) so the shape is readable at a glance and
// so the alias is stable even if dual_of_t's implementation changes.
template <typename Prepare, typename Vote, typename Commit, typename Abort>
using TwoPhaseCommit_Follower =
    Recv<Prepare,
    Send<Vote,
    Offer<
        Recv<Commit, End>,
        Recv<Abort,  End>>>>;

// ═════════════════════════════════════════════════════════════════════
// ── SWIM gossip probe ──────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// Client side of a single SWIM probe: send Probe, receive Ack.
// Structurally identical to RequestResponseOnce_Client; named for
// protocol-catalogue recognition.  CRUCIBLE.md §IV.16 uses this at
// every gossip step between two Keepers.
template <typename Probe, typename Ack>
using SwimProbe_Client = RequestResponseOnce_Client<Probe, Ack>;

// Server side.
template <typename Probe, typename Ack>
using SwimProbe_Server = RequestResponseOnce_Server<Probe, Ack>;

// ═════════════════════════════════════════════════════════════════════
// ── Handshake ──────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// Client sends Hello, then the peer offers to respond with either
// Welcome (accept) or Reject (refuse).  After either branch the
// session ends.  For a handshake-then-run-protocol shape, compose
// with the subsequent protocol via compose_t.
template <typename Hello, typename Welcome, typename Reject>
using Handshake_Client = Send<Hello, Offer<
    Recv<Welcome, End>,
    Recv<Reject,  End>>>;

// Server side — receives Hello, then selects between welcoming or
// rejecting.
template <typename Hello, typename Welcome, typename Reject>
using Handshake_Server = Recv<Hello, Select<
    Send<Welcome, End>,
    Send<Reject,  End>>>;

}  // namespace crucible::safety::proto::pattern

// ═════════════════════════════════════════════════════════════════════
// ── Framework self-test static_asserts ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Verify every pattern's structural expansion, duality, involution,
// and well-formedness.  Runs at header-inclusion time; regressions in
// Session.h's dual_of / compose / is_well_formed or in this header's
// aliases fail compilation at the first TU that pulls us in.

#ifdef CRUCIBLE_SESSION_SELF_TESTS
namespace crucible::safety::proto::pattern::detail::pattern_self_test {

// Fixture types — distinct empty structs so the type system can tell
// them apart in positional-branch tests.
struct Req     {};
struct Resp    {};
struct Task    {};
struct Result  {};
struct Prepare {};
struct Vote    {};
struct Commit  {};
struct Abort   {};
struct Job     {};
struct Probe   {};
struct Ack     {};
struct Hello   {};
struct Welcome {};
struct Reject  {};
struct Cancel  {};
struct Retry   {};

struct ClientRole      {};
struct ServerRole      {};
struct SourceRole      {};
struct SinkRole        {};
struct StageRole       {};
struct CoordinatorRole {};
struct CollectorRole   {};
struct ProducerRole    {};
struct ConsumerRole    {};
struct FollowerRole    {};

// ─── RequestResponse family ────────────────────────────────────────

// Once-variants: single round trip.
static_assert(std::is_same_v<
    RequestResponseOnce_Client<Req, Resp>,
    Send<Req, Recv<Resp, End>>>);
static_assert(std::is_same_v<
    RequestResponseOnce_Server<Req, Resp>,
    Recv<Req, Send<Resp, End>>>);
static_assert(std::is_same_v<
    dual_of_t<RequestResponseOnce_Client<Req, Resp>>,
    RequestResponseOnce_Server<Req, Resp>>);
static_assert(std::is_same_v<
    dual_of_t<RequestResponseOnce_Server<Req, Resp>>,
    RequestResponseOnce_Client<Req, Resp>>);

// Looping variants: forever.
static_assert(std::is_same_v<
    RequestResponse_Client<Req, Resp>,
    Loop<Send<Req, Recv<Resp, Continue>>>>);
static_assert(std::is_same_v<
    RequestResponse_Server<Req, Resp>,
    Loop<Recv<Req, Send<Resp, Continue>>>>);
static_assert(std::is_same_v<
    dual_of_t<RequestResponse_Client<Req, Resp>>,
    RequestResponse_Server<Req, Resp>>);

// Loop-with-close variants.
static_assert(std::is_same_v<
    RequestResponseLoop_Client<Req, Resp>,
    Loop<Select<Send<Req, Recv<Resp, Continue>>, End>>>);
static_assert(std::is_same_v<
    RequestResponseLoop_Server<Req, Resp>,
    Loop<Offer<Recv<Req, Send<Resp, Continue>>, End>>>);
static_assert(std::is_same_v<
    dual_of_t<RequestResponseLoop_Client<Req, Resp>>,
    RequestResponseLoop_Server<Req, Resp>>);

// Well-formedness — every variant passes.
static_assert(is_well_formed_v<RequestResponseOnce_Client<Req, Resp>>);
static_assert(is_well_formed_v<RequestResponseOnce_Server<Req, Resp>>);
static_assert(is_well_formed_v<RequestResponse_Client<Req, Resp>>);
static_assert(is_well_formed_v<RequestResponse_Server<Req, Resp>>);
static_assert(is_well_formed_v<RequestResponseLoop_Client<Req, Resp>>);
static_assert(is_well_formed_v<RequestResponseLoop_Server<Req, Resp>>);

// Involution under dual for every *_Client pattern.
static_assert(std::is_same_v<
    dual_of_t<dual_of_t<RequestResponseOnce_Client<Req, Resp>>>,
    RequestResponseOnce_Client<Req, Resp>>);
static_assert(std::is_same_v<
    dual_of_t<dual_of_t<RequestResponse_Client<Req, Resp>>>,
    RequestResponse_Client<Req, Resp>>);
static_assert(std::is_same_v<
    dual_of_t<dual_of_t<RequestResponseLoop_Client<Req, Resp>>>,
    RequestResponseLoop_Client<Req, Resp>>);

// ─── PipelineSource / PipelineSink / PipelineStage ────────────────

// Source: forever-loop of Send.
static_assert(std::is_same_v<
    PipelineSource<Job>,
    Loop<Send<Job, Continue>>>);
static_assert(std::is_same_v<
    PipelineSink<Job>,
    Loop<Recv<Job, Continue>>>);

// Source's dual is Sink, and vice versa (both directions).
static_assert(std::is_same_v<dual_of_t<PipelineSource<Job>>, PipelineSink<Job>>);
static_assert(std::is_same_v<dual_of_t<PipelineSink<Job>>,   PipelineSource<Job>>);

// Involution.
static_assert(std::is_same_v<
    dual_of_t<dual_of_t<PipelineSource<Job>>>,
    PipelineSource<Job>>);

// Well-formed: Continue is inside Loop.
static_assert(is_well_formed_v<PipelineSource<Job>>);
static_assert(is_well_formed_v<PipelineSink<Job>>);

// PipelineStage IS RequestResponse_Server structurally — the alias
// names the intent.
static_assert(std::is_same_v<
    PipelineStage<Req, Resp>,
    RequestResponse_Server<Req, Resp>>);

// Dual of a pipeline stage is the "source" that drives it.
static_assert(std::is_same_v<
    dual_of_t<PipelineStage<Req, Resp>>,
    RequestResponse_Client<Req, Resp>>);
// And the reverse direction.
static_assert(std::is_same_v<
    dual_of_t<RequestResponse_Client<Req, Resp>>,
    PipelineStage<Req, Resp>>);

static_assert(is_well_formed_v<PipelineStage<Req, Resp>>);

// ─── Transaction ──────────────────────────────────────────────────

using TxClient = Transaction_Client<Prepare, Req, Commit, Ack, Abort>;
using TxServer = Transaction_Server<Prepare, Req, Commit, Ack, Abort>;

// Structural expansion — matches the doc-comment sequence exactly.
static_assert(std::is_same_v<
    TxClient,
    Send<Prepare, Loop<Select<
        Send<Req, Continue>,
        Send<Commit, Recv<Ack, End>>,
        Send<Abort, End>>>>>);
static_assert(std::is_same_v<
    TxServer,
    Recv<Prepare, Loop<Offer<
        Recv<Req, Continue>,
        Recv<Commit, Send<Ack, End>>,
        Recv<Abort, End>>>>>);

// Bidirectional duality — catches asymmetric dual bugs in either
// Session.h or in our hand-written dual pair.
static_assert(std::is_same_v<dual_of_t<TxClient>, TxServer>);
static_assert(std::is_same_v<dual_of_t<TxServer>, TxClient>);

// Involution.
static_assert(std::is_same_v<dual_of_t<dual_of_t<TxClient>>, TxClient>);

// Well-formedness: Continue (in the Op branch) is guarded by the
// Loop; all End-terminated branches are WF trivially.
static_assert(is_well_formed_v<TxClient>);
static_assert(is_well_formed_v<TxServer>);

// ─── FanOut / FanIn / Broadcast ────────────────────────────────────

// Degenerate: 0 sends = End.
static_assert(std::is_same_v<FanOut<0, Job>, End>);
static_assert(std::is_same_v<FanIn<0, Job>, End>);

// Single send/recv.
static_assert(std::is_same_v<FanOut<1, Job>, Send<Job, End>>);
static_assert(std::is_same_v<FanIn<1, Job>,  Recv<Job, End>>);

// Three-element expansion.
static_assert(std::is_same_v<
    FanOut<3, Job>,
    Send<Job, Send<Job, Send<Job, End>>>>);
static_assert(std::is_same_v<
    FanIn<3, Job>,
    Recv<Job, Recv<Job, Recv<Job, End>>>>);

// Duality mirror:  dual(FanOut<N, Msg>) = FanIn<N, Msg>  for any N.
static_assert(std::is_same_v<dual_of_t<FanOut<0, Job>>, FanIn<0, Job>>);
static_assert(std::is_same_v<dual_of_t<FanOut<1, Job>>, FanIn<1, Job>>);
static_assert(std::is_same_v<dual_of_t<FanOut<3, Job>>, FanIn<3, Job>>);
static_assert(std::is_same_v<dual_of_t<FanIn<5, Job>>,  FanOut<5, Job>>);

// Involution.
static_assert(std::is_same_v<
    dual_of_t<dual_of_t<FanOut<7, Job>>>,
    FanOut<7, Job>>);

// Broadcast == FanOut at the type level.
static_assert(std::is_same_v<Broadcast<0, Job>, FanOut<0, Job>>);
static_assert(std::is_same_v<Broadcast<4, Job>, FanOut<4, Job>>);

// Well-formedness across sizes.
static_assert(is_well_formed_v<FanOut<0, Job>>);
static_assert(is_well_formed_v<FanOut<1, Job>>);
static_assert(is_well_formed_v<FanOut<32, Job>>);
static_assert(is_well_formed_v<FanIn<32, Job>>);

// #375 SEPLOG-PERF-5: divide-and-conquer chain builders make N values
// far above the previous ~1100 template-depth cap reachable.  These
// asserts witness the lifted limit — under the original linear
// `fan_{out,in}_helper` recursion, each of these would fire
// "template instantiation depth exceeds maximum of 900" on GCC 16.
//
// Two-element-at-a-time expansion checks the recursion's correctness
// at boundaries where Half == Rest (even N) and Half + 1 == Rest
// (odd N).
static_assert(std::is_same_v<
    FanOut<2, Job>,
    Send<Job, Send<Job, End>>>);
static_assert(std::is_same_v<
    FanOut<4, Job>,
    Send<Job, Send<Job, Send<Job, Send<Job, End>>>>>);
static_assert(std::is_same_v<
    FanOut<5, Job>,
    Send<Job, Send<Job, Send<Job, Send<Job, Send<Job, End>>>>>>);

// Large-N witnesses: instantiating these forces O(log₂ N) chain-
// construction depth = 11 (for 2048) ≪ 900-default template-depth
// limit.  Under the previous linear `fan_{out,in}_helper` recursion
// the chain construction itself fired "template instantiation depth
// exceeds maximum of 900" at GCC-default settings around N ≈ 1100;
// the divide-and-conquer helpers lift that ceiling to N ≈ 2^900.
//
// The witness uses a non-recursive HEAD pattern-match (instead of
// `is_well_formed_v`, which walks the entire chain and would itself
// hit the depth limit).  The chain's existence + matching head
// shape proves the construction succeeded; tail correctness is
// established by the small-N exact-match asserts above.
namespace large_n_witness {
template <typename T> struct head_is_send : std::false_type {};
template <typename M, typename K>
struct head_is_send<Send<M, K>> : std::true_type {};

template <typename T> struct head_is_recv : std::false_type {};
template <typename M, typename K>
struct head_is_recv<Recv<M, K>> : std::true_type {};
}  // namespace large_n_witness

static_assert( large_n_witness::head_is_send<FanOut<2048, Job>>::value);
static_assert( large_n_witness::head_is_recv<FanIn <2048, Job>>::value);
static_assert( large_n_witness::head_is_send<FanOut<8192, Job>>::value);
static_assert( large_n_witness::head_is_recv<FanIn <8192, Job>>::value);

// Note: a `dual_of_t<FanOut<2048, Job>>` witness would be natural
// here but would itself hit the template-depth limit — `dual_of` is
// still linearly recursive (Session.h:268).  The small-N duality
// asserts above (`dual_of_t<FanOut<3, Job>>` / `<FanOut<7, Job>>`)
// prove the duality rule; lifting `dual_of` to divide-and-conquer
// is a separate follow-up if any real use case materialises.

// ─── ScatterGather ─────────────────────────────────────────────────

// N=2: send Task Task, then recv Result Result.
static_assert(std::is_same_v<
    ScatterGather<2, Task, Result>,
    Send<Task, Send<Task,
         Recv<Result, Recv<Result, End>>>>>);

// N=0: degenerate — empty compose = End.
static_assert(std::is_same_v<ScatterGather<0, Task, Result>, End>);

// N=1: single round trip — structurally equal to RequestResponseOnce.
static_assert(std::is_same_v<
    ScatterGather<1, Task, Result>,
    RequestResponseOnce_Client<Task, Result>>);

// Duality: scatter-gather coordinator's dual is gather-scatter follower.
static_assert(std::is_same_v<
    dual_of_t<ScatterGather<2, Task, Result>>,
    Recv<Task, Recv<Task,
         Send<Result, Send<Result, End>>>>>);

// Involution.
static_assert(std::is_same_v<
    dual_of_t<dual_of_t<ScatterGather<4, Task, Result>>>,
    ScatterGather<4, Task, Result>>);

static_assert(is_well_formed_v<ScatterGather<8, Task, Result>>);

// ─── MpmcProducer / MpmcConsumer ───────────────────────────────────

static_assert(std::is_same_v<
    MpmcProducer<Job>,
    Loop<Select<Send<Job, Continue>, End>>>);
static_assert(std::is_same_v<
    MpmcConsumer<Job>,
    Loop<Offer<Recv<Job, Continue>, End>>>);

// Duality.
static_assert(std::is_same_v<dual_of_t<MpmcProducer<Job>>, MpmcConsumer<Job>>);
static_assert(std::is_same_v<dual_of_t<MpmcConsumer<Job>>, MpmcProducer<Job>>);

// Involution.
static_assert(std::is_same_v<
    dual_of_t<dual_of_t<MpmcProducer<Job>>>,
    MpmcProducer<Job>>);

// Well-formed (Continue is guarded by Loop).
static_assert(is_well_formed_v<MpmcProducer<Job>>);
static_assert(is_well_formed_v<MpmcConsumer<Job>>);

// ─── TwoPhaseCommit ────────────────────────────────────────────────

using ConcreteCoord    = TwoPhaseCommit_Coord<Prepare, Vote, Commit, Abort>;
using ConcreteFollower = TwoPhaseCommit_Follower<Prepare, Vote, Commit, Abort>;

// Structural expansion.
static_assert(std::is_same_v<
    ConcreteCoord,
    Send<Prepare,
    Recv<Vote,
    Select<
        Send<Commit, End>,
        Send<Abort,  End>>>>>);
static_assert(std::is_same_v<
    ConcreteFollower,
    Recv<Prepare,
    Send<Vote,
    Offer<
        Recv<Commit, End>,
        Recv<Abort,  End>>>>>);

// Duality — critical: the written-out TwoPhaseCommit_Follower IS
// precisely dual(TwoPhaseCommit_Coord).  If dual_of_t changes, this
// catches the divergence.
static_assert(std::is_same_v<dual_of_t<ConcreteCoord>,    ConcreteFollower>);
static_assert(std::is_same_v<dual_of_t<ConcreteFollower>, ConcreteCoord>);

// Involution.
static_assert(std::is_same_v<dual_of_t<dual_of_t<ConcreteCoord>>, ConcreteCoord>);

// Well-formedness.
static_assert(is_well_formed_v<ConcreteCoord>);
static_assert(is_well_formed_v<ConcreteFollower>);

// ─── SwimProbe ─────────────────────────────────────────────────────

// SwimProbe is intentionally identical to RequestResponseOnce —
// structurally equal at the type level.
static_assert(std::is_same_v<
    SwimProbe_Client<Probe, Ack>,
    RequestResponseOnce_Client<Probe, Ack>>);
static_assert(std::is_same_v<
    SwimProbe_Server<Probe, Ack>,
    RequestResponseOnce_Server<Probe, Ack>>);

// Duality.
static_assert(std::is_same_v<
    dual_of_t<SwimProbe_Client<Probe, Ack>>,
    SwimProbe_Server<Probe, Ack>>);

static_assert(is_well_formed_v<SwimProbe_Client<Probe, Ack>>);
static_assert(is_well_formed_v<SwimProbe_Server<Probe, Ack>>);

// ─── Handshake ─────────────────────────────────────────────────────

// Structural expansion.
static_assert(std::is_same_v<
    Handshake_Client<Hello, Welcome, Reject>,
    Send<Hello, Offer<
        Recv<Welcome, End>,
        Recv<Reject,  End>>>>);
static_assert(std::is_same_v<
    Handshake_Server<Hello, Welcome, Reject>,
    Recv<Hello, Select<
        Send<Welcome, End>,
        Send<Reject,  End>>>>);

// Duality.
static_assert(std::is_same_v<
    dual_of_t<Handshake_Client<Hello, Welcome, Reject>>,
    Handshake_Server<Hello, Welcome, Reject>>);
static_assert(std::is_same_v<
    dual_of_t<Handshake_Server<Hello, Welcome, Reject>>,
    Handshake_Client<Hello, Welcome, Reject>>);

// Involution.
static_assert(std::is_same_v<
    dual_of_t<dual_of_t<Handshake_Client<Hello, Welcome, Reject>>>,
    Handshake_Client<Hello, Welcome, Reject>>);

static_assert(is_well_formed_v<Handshake_Client<Hello, Welcome, Reject>>);
static_assert(is_well_formed_v<Handshake_Server<Hello, Welcome, Reject>>);

// ─── Subtype refinement coverage (GAPS-054) ───────────────────────
//
// Public named strong/weak aliases do not exist yet.  These assertions
// pin the subtype surface that is meaningful for today's pattern set:
// every named pattern refines itself and its double dual; structural
// aliases are bidirectionally equivalent; and branch-bearing patterns
// have local strict witnesses for Select narrowing / Offer widening.

template <typename P>
inline constexpr bool refines_self_and_double_dual_v =
    is_subtype_sync_v<P, P> &&
    is_subtype_sync_v<P, dual_of_t<dual_of_t<P>>>;

// Every public named pattern refines its own double dual.  This is the
// subtype-strengthened form of the dual-involution checks above.
static_assert(refines_self_and_double_dual_v<
    RequestResponseOnce_Client<Req, Resp>>);
static_assert(refines_self_and_double_dual_v<
    RequestResponseOnce_Server<Req, Resp>>);
static_assert(refines_self_and_double_dual_v<
    RequestResponse_Client<Req, Resp>>);
static_assert(refines_self_and_double_dual_v<
    RequestResponse_Server<Req, Resp>>);
static_assert(refines_self_and_double_dual_v<
    RequestResponseLoop_Client<Req, Resp>>);
static_assert(refines_self_and_double_dual_v<
    RequestResponseLoop_Server<Req, Resp>>);

static_assert(refines_self_and_double_dual_v<PipelineSource<Job>>);
static_assert(refines_self_and_double_dual_v<PipelineSink<Job>>);
static_assert(refines_self_and_double_dual_v<PipelineStage<Req, Resp>>);

static_assert(refines_self_and_double_dual_v<TxClient>);
static_assert(refines_self_and_double_dual_v<TxServer>);

static_assert(refines_self_and_double_dual_v<FanOut<3, Job>>);
static_assert(refines_self_and_double_dual_v<FanIn<3, Job>>);
static_assert(refines_self_and_double_dual_v<Broadcast<3, Job>>);
static_assert(refines_self_and_double_dual_v<
    ScatterGather<2, Task, Result>>);

static_assert(refines_self_and_double_dual_v<MpmcProducer<Job>>);
static_assert(refines_self_and_double_dual_v<MpmcConsumer<Job>>);

static_assert(refines_self_and_double_dual_v<ConcreteCoord>);
static_assert(refines_self_and_double_dual_v<ConcreteFollower>);

static_assert(refines_self_and_double_dual_v<
    SwimProbe_Client<Probe, Ack>>);
static_assert(refines_self_and_double_dual_v<
    SwimProbe_Server<Probe, Ack>>);

static_assert(refines_self_and_double_dual_v<
    Handshake_Client<Hello, Welcome, Reject>>);
static_assert(refines_self_and_double_dual_v<
    Handshake_Server<Hello, Welcome, Reject>>);

// Alias families are not merely syntactically equal; they are pinned
// as bidirectional subtype equivalents.
static_assert(equivalent_sync_v<
    PipelineStage<Req, Resp>,
    RequestResponse_Server<Req, Resp>>);
static_assert(equivalent_sync_v<Broadcast<4, Job>, FanOut<4, Job>>);
static_assert(equivalent_sync_v<
    ScatterGather<1, Task, Result>,
    RequestResponseOnce_Client<Task, Result>>);
static_assert(equivalent_sync_v<
    SwimProbe_Client<Probe, Ack>,
    RequestResponseOnce_Client<Probe, Ack>>);
static_assert(equivalent_sync_v<
    SwimProbe_Server<Probe, Ack>,
    RequestResponseOnce_Server<Probe, Ack>>);

// The close-free and in-band-close request/response variants are
// intentionally distinct shapes, not a subtype ladder.
static_assert(!is_subtype_sync_v<
    RequestResponse_Client<Req, Resp>,
    RequestResponseLoop_Client<Req, Resp>>);
static_assert(!is_subtype_sync_v<
    RequestResponseLoop_Client<Req, Resp>,
    RequestResponse_Client<Req, Resp>>);
static_assert(!is_subtype_sync_v<
    RequestResponse_Server<Req, Resp>,
    RequestResponseLoop_Server<Req, Resp>>);
static_assert(!is_subtype_sync_v<
    RequestResponseLoop_Server<Req, Resp>,
    RequestResponse_Server<Req, Resp>>);

// RequestResponseLoop: client-side Select narrowing and server-side
// Offer widening are the proper strict refinement directions.
using RRLoopClientSendOnly = Loop<Select<
    Send<Req, Recv<Resp, Continue>>>>;
static_assert(is_strict_subtype_sync_v<
    RRLoopClientSendOnly,
    RequestResponseLoop_Client<Req, Resp>>);
static_assert(!is_subtype_sync_v<
    RequestResponseLoop_Client<Req, Resp>,
    RRLoopClientSendOnly>);

using RRLoopServerWithRetry = Loop<Offer<
    Recv<Req, Send<Resp, Continue>>,
    End,
    Recv<Retry, End>>>;
static_assert(is_strict_subtype_sync_v<
    RRLoopServerWithRetry,
    RequestResponseLoop_Server<Req, Resp>>);
static_assert(!is_subtype_sync_v<
    RequestResponseLoop_Server<Req, Resp>,
    RRLoopServerWithRetry>);

// Transaction: a client may narrow choices; a server may handle more
// choices while preserving the documented first three branch positions.
using TxClientOpsOnly = Send<Prepare, Loop<Select<
    Send<Req, Continue>>>>;
static_assert(is_strict_subtype_sync_v<TxClientOpsOnly, TxClient>);
static_assert(!is_subtype_sync_v<TxClient, TxClientOpsOnly>);

using TxServerWithCancel = Recv<Prepare, Loop<Offer<
    Recv<Req, Continue>,
    Recv<Commit, Send<Ack, End>>,
    Recv<Abort, End>,
    Recv<Cancel, End>>>>;
static_assert(is_strict_subtype_sync_v<TxServerWithCancel, TxServer>);
static_assert(!is_subtype_sync_v<TxServer, TxServerWithCancel>);

// MPMC: producer push-only is a Select narrowing; consumer extra close
// handling is an Offer widening.
using MpmcProducerPushOnly = Loop<Select<Send<Job, Continue>>>;
static_assert(is_strict_subtype_sync_v<
    MpmcProducerPushOnly,
    MpmcProducer<Job>>);
static_assert(!is_subtype_sync_v<
    MpmcProducer<Job>,
    MpmcProducerPushOnly>);

using MpmcConsumerWithCancel = Loop<Offer<
    Recv<Job, Continue>,
    End,
    Recv<Cancel, End>>>;
static_assert(is_strict_subtype_sync_v<
    MpmcConsumerWithCancel,
    MpmcConsumer<Job>>);
static_assert(!is_subtype_sync_v<
    MpmcConsumer<Job>,
    MpmcConsumerWithCancel>);

// Two-phase commit: coordinator narrows the final decision; follower
// accepts a compatible future extension branch.
using CoordCommitOnly = Send<Prepare, Recv<Vote, Select<
    Send<Commit, End>>>>;
static_assert(is_strict_subtype_sync_v<CoordCommitOnly, ConcreteCoord>);
static_assert(!is_subtype_sync_v<ConcreteCoord, CoordCommitOnly>);

using FollowerWithRetry = Recv<Prepare, Send<Vote, Offer<
    Recv<Commit, End>,
    Recv<Abort, End>,
    Recv<Retry, End>>>>;
static_assert(is_strict_subtype_sync_v<FollowerWithRetry, ConcreteFollower>);
static_assert(!is_subtype_sync_v<ConcreteFollower, FollowerWithRetry>);

// Handshake: the client may handle an extra offered outcome; the
// server may narrow its selected outcome.
using HandshakeClientWithRetry = Send<Hello, Offer<
    Recv<Welcome, End>,
    Recv<Reject, End>,
    Recv<Retry, End>>>;
static_assert(is_strict_subtype_sync_v<
    HandshakeClientWithRetry,
    Handshake_Client<Hello, Welcome, Reject>>);
static_assert(!is_subtype_sync_v<
    Handshake_Client<Hello, Welcome, Reject>,
    HandshakeClientWithRetry>);

using HandshakeServerWelcomeOnly = Recv<Hello, Select<
    Send<Welcome, End>>>;
static_assert(is_strict_subtype_sync_v<
    HandshakeServerWelcomeOnly,
    Handshake_Server<Hello, Welcome, Reject>>);
static_assert(!is_subtype_sync_v<
    Handshake_Server<Hello, Welcome, Reject>,
    HandshakeServerWelcomeOnly>);

// ─── Crash-safety contracts (GAPS-055) ────────────────────────────
//
// Existing public aliases stay wire-compatible and therefore remain
// crash-oblivious.  We mark that status explicitly for unreliable-peer
// deployments, then prove representative crash-aware witnesses using
// Sender<>-annotated Offer branches from SessionCrash.h.

template <typename Proto, typename SelfRole>
using PendingCrashContract = PatternCrashSafety<
    Proto,
    ReliableSet<SelfRole>,
    CrashSafetyPending,
    BaselinePatternNeedsCrashAwareVariant>;

template <typename Proto, typename SelfRole>
using VerifiedCrashContract = PatternCrashSafety<
    Proto,
    ReliableSet<SelfRole>,
    CrashSafetyVerified>;

template <typename Contract>
consteval bool pending_contract_ok() {
    return pattern_crash_safety_pending_v<Contract> &&
           !pattern_crash_safety_verified_v<Contract>;
}

template <typename Contract>
consteval bool verified_contract_ok() {
    return pattern_crash_safety_verified_v<Contract> &&
           !pattern_crash_safety_pending_v<Contract>;
}

// Baseline pattern contracts.  Reliability set R is the local role
// assumed reliable for this endpoint; the peer side is not assumed
// reliable and needs an explicit Crash<Peer> branch in a crash-aware
// variant.
static_assert(pending_contract_ok<PendingCrashContract<
    RequestResponseOnce_Client<Req, Resp>, ClientRole>>());
static_assert(pending_contract_ok<PendingCrashContract<
    RequestResponseOnce_Server<Req, Resp>, ServerRole>>());
static_assert(pending_contract_ok<PendingCrashContract<
    RequestResponse_Client<Req, Resp>, ClientRole>>());
static_assert(pending_contract_ok<PendingCrashContract<
    RequestResponse_Server<Req, Resp>, ServerRole>>());
static_assert(pending_contract_ok<PendingCrashContract<
    RequestResponseLoop_Client<Req, Resp>, ClientRole>>());
static_assert(pending_contract_ok<PendingCrashContract<
    RequestResponseLoop_Server<Req, Resp>, ServerRole>>());

static_assert(pending_contract_ok<PendingCrashContract<
    PipelineSource<Job>, SourceRole>>());
static_assert(pending_contract_ok<PendingCrashContract<
    PipelineSink<Job>, SinkRole>>());
static_assert(pending_contract_ok<PendingCrashContract<
    PipelineStage<Req, Resp>, StageRole>>());

static_assert(pending_contract_ok<PendingCrashContract<TxClient, ClientRole>>());
static_assert(pending_contract_ok<PendingCrashContract<TxServer, ServerRole>>());

static_assert(pending_contract_ok<PendingCrashContract<
    FanOut<3, Job>, CoordinatorRole>>());
static_assert(pending_contract_ok<PendingCrashContract<
    FanIn<3, Job>, CollectorRole>>());
static_assert(pending_contract_ok<PendingCrashContract<
    Broadcast<3, Job>, CoordinatorRole>>());
static_assert(pending_contract_ok<PendingCrashContract<
    ScatterGather<2, Task, Result>, CoordinatorRole>>());

static_assert(pending_contract_ok<PendingCrashContract<
    MpmcProducer<Job>, ProducerRole>>());
static_assert(pending_contract_ok<PendingCrashContract<
    MpmcConsumer<Job>, ConsumerRole>>());

static_assert(pending_contract_ok<PendingCrashContract<
    ConcreteCoord, CoordinatorRole>>());
static_assert(pending_contract_ok<PendingCrashContract<
    ConcreteFollower, FollowerRole>>());

static_assert(pending_contract_ok<PendingCrashContract<
    SwimProbe_Client<Probe, Ack>, ClientRole>>());
static_assert(pending_contract_ok<PendingCrashContract<
    SwimProbe_Server<Probe, Ack>, ServerRole>>());

static_assert(pending_contract_ok<PendingCrashContract<
    Handshake_Client<Hello, Welcome, Reject>, ClientRole>>());
static_assert(pending_contract_ok<PendingCrashContract<
    Handshake_Server<Hello, Welcome, Reject>, ServerRole>>());

// The Offer walker catches the currently-unprotected branch protocols.
// Protocols without Offer nodes are still marked pending above: the
// local type lacks sender-role context, so an Offer-only predicate has
// no sound way to infer "this Recv came from an unreliable peer."
static_assert(!every_offer_has_crash_branch_for_peer_v<
    RequestResponseLoop_Server<Req, Resp>, ClientRole>);
static_assert(!every_offer_has_crash_branch_for_peer_v<TxServer, ClientRole>);
static_assert(!every_offer_has_crash_branch_for_peer_v<
    MpmcConsumer<Job>, ProducerRole>);
static_assert(!every_offer_has_crash_branch_for_peer_v<
    ConcreteFollower, CoordinatorRole>);
static_assert(!every_offer_has_crash_branch_for_peer_v<
    Handshake_Client<Hello, Welcome, Reject>, ServerRole>);

using CrashAwareOnceClient = Send<Req, Offer<Sender<ServerRole>,
    Recv<Resp, End>,
    Recv<Crash<ServerRole>, End>>>;
static_assert(every_offer_has_crash_branch_for_peer_v<
    CrashAwareOnceClient, ServerRole>);
static_assert(verified_contract_ok<VerifiedCrashContract<
    CrashAwareOnceClient, ClientRole>>());

using CrashAwareLoopServer = Loop<Offer<Sender<ClientRole>,
    Recv<Req, Send<Resp, Continue>>,
    End,
    Recv<Crash<ClientRole>, End>>>;
static_assert(every_offer_has_crash_branch_for_peer_v<
    CrashAwareLoopServer, ClientRole>);
static_assert(verified_contract_ok<VerifiedCrashContract<
    CrashAwareLoopServer, ServerRole>>());

using CrashAwareTxServer = Recv<Prepare, Loop<Offer<Sender<ClientRole>,
    Recv<Req, Continue>,
    Recv<Commit, Send<Ack, End>>,
    Recv<Abort, End>,
    Recv<Crash<ClientRole>, End>>>>;
static_assert(every_offer_has_crash_branch_for_peer_v<
    CrashAwareTxServer, ClientRole>);
static_assert(verified_contract_ok<VerifiedCrashContract<
    CrashAwareTxServer, ServerRole>>());

using CrashAwareMpmcConsumer = Loop<Offer<Sender<ProducerRole>,
    Recv<Job, Continue>,
    End,
    Recv<Crash<ProducerRole>, End>>>;
static_assert(every_offer_has_crash_branch_for_peer_v<
    CrashAwareMpmcConsumer, ProducerRole>);
static_assert(verified_contract_ok<VerifiedCrashContract<
    CrashAwareMpmcConsumer, ConsumerRole>>());

using CrashAwareCoord = Send<Prepare, Offer<Sender<FollowerRole>,
    Recv<Vote, Select<Send<Commit, End>, Send<Abort, End>>>,
    Recv<Crash<FollowerRole>, End>>>;
static_assert(every_offer_has_crash_branch_for_peer_v<
    CrashAwareCoord, FollowerRole>);
static_assert(verified_contract_ok<VerifiedCrashContract<
    CrashAwareCoord, CoordinatorRole>>());

using CrashAwareFollower = Recv<Prepare, Send<Vote, Offer<Sender<CoordinatorRole>,
    Recv<Commit, End>,
    Recv<Abort, End>,
    Recv<Crash<CoordinatorRole>, End>>>>;
static_assert(every_offer_has_crash_branch_for_peer_v<
    CrashAwareFollower, CoordinatorRole>);
static_assert(verified_contract_ok<VerifiedCrashContract<
    CrashAwareFollower, FollowerRole>>());

using CrashAwareHandshakeClient = Send<Hello, Offer<Sender<ServerRole>,
    Recv<Welcome, End>,
    Recv<Reject, End>,
    Recv<Crash<ServerRole>, End>>>;
static_assert(every_offer_has_crash_branch_for_peer_v<
    CrashAwareHandshakeClient, ServerRole>);
static_assert(verified_contract_ok<VerifiedCrashContract<
    CrashAwareHandshakeClient, ClientRole>>());

// ─── Cross-pattern composition ────────────────────────────────────
//
// Patterns compose via compose_t<P, Q>.  Semantics: replace EVERY End
// in P with Q.  For a Handshake, this means BOTH the welcome AND
// reject branches lead into Q — not just the welcome branch.  That's
// the right shape when the "reject" is retry-able (client tries again
// on the same channel); when rejection should terminate the session,
// hand-write the protocol with an explicit End in the reject arm
// rather than composing.
//
// For the compose test, we pick a request-response-pair composed with
// a trailing close-signal pattern — all branches genuinely continue
// to the same continuation, so compose's every-End-replacement is the
// correct behaviour:

using HandshakeThenLoop_Client = compose_t<
    Handshake_Client<Hello, Welcome, Reject>,
    RequestResponse_Client<Req, Resp>>;

using HandshakeThenLoop_Server = compose_t<
    Handshake_Server<Hello, Welcome, Reject>,
    RequestResponse_Server<Req, Resp>>;

static_assert(is_well_formed_v<HandshakeThenLoop_Client>);
static_assert(is_well_formed_v<HandshakeThenLoop_Server>);

// Duality distributes over compose — compose(dual(A), dual(B)) equals
// dual(compose(A, B)).  This IS the compose/dual commutativity lemma
// and the test catches regressions in either Session.h's compose or
// dual_of implementation.
static_assert(std::is_same_v<
    dual_of_t<HandshakeThenLoop_Client>,
    HandshakeThenLoop_Server>);

// Involution still holds through composition.
static_assert(std::is_same_v<
    dual_of_t<dual_of_t<HandshakeThenLoop_Client>>,
    HandshakeThenLoop_Client>);

// ─── Compose identity: End is the identity element for compose ────
//
// compose<P, End> = P for any P.  If Session.h's compose rules
// regress on the End base case (e.g., stop terminating recursion at
// End), these tests catch it before any pattern using compose_t is
// silently wrong.

static_assert(std::is_same_v<
    compose_t<RequestResponseOnce_Client<Req, Resp>, End>,
    RequestResponseOnce_Client<Req, Resp>>);
static_assert(std::is_same_v<
    compose_t<FanOut<5, Job>, End>,
    FanOut<5, Job>>);
static_assert(std::is_same_v<
    compose_t<FanIn<3, Job>, End>,
    FanIn<3, Job>>);
static_assert(std::is_same_v<
    compose_t<Handshake_Client<Hello, Welcome, Reject>, End>,
    Handshake_Client<Hello, Welcome, Reject>>);
static_assert(std::is_same_v<
    compose_t<ConcreteCoord, End>,
    ConcreteCoord>);

// Compose identity through Loop + Continue: Continue MUST stay Continue
// under compose (not replace-with-Q).  If this regresses, every Loop
// protocol composing with a sub-protocol would be silently incorrect.
static_assert(std::is_same_v<
    compose_t<RequestResponse_Client<Req, Resp>, End>,
    RequestResponse_Client<Req, Resp>>);
static_assert(std::is_same_v<
    compose_t<MpmcProducer<Job>, End>,
    MpmcProducer<Job>>);
static_assert(std::is_same_v<
    compose_t<TxClient, End>,
    TxClient>);

// ─── Deep template recursion ──────────────────────────────────────
//
// Verify the helpers handle "large" N without template-depth issues.
// GCC 16's default template-instantiation depth is 900; we stay well
// inside.  If a future refactor accidentally makes the recursion
// non-tail, compile times jump sharply and these tests flag it.

static_assert(is_well_formed_v<FanOut<64, Job>>);
static_assert(is_well_formed_v<FanIn<64, Job>>);
static_assert(is_well_formed_v<ScatterGather<32, Task, Result>>);
static_assert(std::is_same_v<
    dual_of_t<FanOut<64, Job>>,
    FanIn<64, Job>>);

}  // namespace crucible::safety::proto::pattern::detail::pattern_self_test
#endif  // CRUCIBLE_SESSION_SELF_TESTS
