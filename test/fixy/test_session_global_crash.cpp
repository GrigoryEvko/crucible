// Global types with crash-stop failures, checked against the examples of
// Barwell, Hou, Yoshida and Zhou, "Crash-Stop Failures in Asynchronous
// Multiparty Session Types" (LMCS 21:2, 2025).
//
// The compile-time half checks the crash label and the crash annotation
// of fixy/session/Global.h, role removal (Definition 4.10), crash-stop
// projection (Definition 4.3) and the liveness predicate of Theorem
// 4.31.  The runtime half projects Example 3.2 of the paper, takes the
// binary view of the two projections, and runs them through the crash
// transport of fixy/session/CrashTransport.h under two schedules.

#include <fixy/session/CrashTransport.h>
#include <fixy/session/Global.h>
#include <fixy/session/Liveness.h>
#include <fixy/session/Projection.h>

#include <cstdint>
#include <cstdio>
#include <deque>
#include <optional>
#include <type_traits>
#include <utility>

namespace s = fixy::session;
namespace g = fixy::session::global;

namespace {

// ── The Simpler Logging protocol, equation (2.1) ────────────────────

struct L {};  // the logger
struct I {};  // the interface
struct C {};  // the client
struct Trigger {};
struct Read {};
struct Report {};
struct Fatal {};
struct Log {};

using Logging = g::Msg<L, I, Trigger, void,
                       g::Comm<C, I,
                               g::Branch<Read, void,
                                         g::Msg<I, L, Read, void, g::Msg<L, I, Report, Log, g::Msg<I, C, Report, Log, g::End>>>>,
                               g::Branch<g::CrashLabel, void, g::Msg<I, L, Fatal, void, g::End>>>>;
using LoggerAndInterface = s::ReliableSet<L, I>;

static_assert(g::is_global_well_formed_v<Logging>);
static_assert(g::is_balanced_plus_v<Logging>);
static_assert(std::is_same_v<g::crashed_roles_t<Logging>, g::Roles<>>);

// The projections of section 2, with the crash label kept last.  Each
// choice that the receiver can see a crash of stays a Select on the
// sending side, so the label reaches the receiver.
using ProjC = s::project_crash_t<Logging, C, LoggerAndInterface>;
using ProjL = s::project_crash_t<Logging, L, LoggerAndInterface>;
using ProjI = s::project_crash_t<Logging, I, LoggerAndInterface>;
static_assert(std::is_same_v<ProjC::local, s::Select<s::Send<s::PeerMsg<I, Read, void>, s::Recv<s::PeerMsg<I, Report, Log>, s::End>>>>);
static_assert(std::is_same_v<ProjL::local,
                             s::Send<s::PeerMsg<I, Trigger, void>,
                                     s::Offer<s::Sender<I>,
                                              s::Recv<s::PeerMsg<I, Read, void>, s::Send<s::PeerMsg<I, Report, Log>, s::End>>,
                                              s::Recv<s::PeerMsg<I, Fatal, void>, s::End>>>>);
static_assert(std::is_same_v<
              ProjI::local,
              s::Recv<s::PeerMsg<L, Trigger, void>,
                      s::Offer<s::Sender<C>,
                               s::Recv<s::PeerMsg<C, Read, void>,
                                       s::Send<s::PeerMsg<L, Read, void>,
                                               s::Recv<s::PeerMsg<L, Report, Log>, s::Send<s::PeerMsg<C, Report, Log>, s::End>>>>,
                               s::Recv<s::PeerMsg<C, g::CrashLabel, void>, s::Send<s::PeerMsg<L, Fatal, void>, s::End>>>>>);
static_assert(ProjC::queue::size == 0 && ProjL::queue::size == 0 && ProjI::queue::size == 0);
static_assert(s::crash_live_by_construction_v<Logging, LoggerAndInterface>);

// With no reliable role, the trigger from L needs a crash branch.
static_assert(std::is_same_v<s::project_crash_t<Logging, I, s::NoReliableRoles>,
                             s::NotProjectable<s::projection_failure::MissingCrashBranch>>);
static_assert(!s::crash_live_by_construction_v<Logging, s::NoReliableRoles>);
// With C reliable too, the crash branch of C can never run.
static_assert(std::is_same_v<s::project_crash_t<Logging, I, s::ReliableSet<L, I, C>>,
                             s::NotProjectable<s::projection_failure::CrashBranchFromReliableSender>>);
// project_t counts every role reliable, and the liveness of the
// asynchronous paper has no crash, so neither accepts the crash branch.
static_assert(std::is_same_v<s::project_t<Logging, I>, s::NotProjectable<s::projection_failure::CrashBranchFromReliableSender>>);
static_assert(!s::is_live_by_construction_v<Logging>);
// The crash-free protocol G0 of section 2 projects in both theories.
using Logging0 = g::Msg<L, I, Trigger, void,
                        g::Msg<C, I, Read, void, g::Msg<I, L, Read, void, g::Msg<L, I, Report, Log, g::Msg<I, C, Report, Log, g::End>>>>>;
static_assert(s::is_live_by_construction_v<Logging0>);
static_assert(s::crash_live_by_construction_v<Logging0, s::ReliableSet<L, I, C>>);
static_assert(!s::crash_live_by_construction_v<Logging0, LoggerAndInterface>);

// ── Role removal, Example 4.11 ──────────────────────────────────────

using LoggingWithoutC = g::remove_role_t<Logging, C>;
static_assert(std::is_same_v<LoggingWithoutC,
                             g::Msg<L, I, Trigger, void,
                                    g::EnRoute<g::Crashed<C>, I, g::CrashLabel, void, g::Msg<I, L, Fatal, void, g::End>>>>);
static_assert(g::is_global_well_formed_v<LoggingWithoutC>);
static_assert(g::is_balanced_plus_v<LoggingWithoutC>);
static_assert(g::is_well_annotated_v<LoggingWithoutC, g::Roles<L, I>>);
static_assert(!g::is_well_annotated_v<LoggingWithoutC, g::Roles<C>>);
static_assert(std::is_same_v<g::crashed_roles_t<LoggingWithoutC>, g::Roles<C>>);
static_assert(std::is_same_v<g::roles_t<LoggingWithoutC>, g::Roles<L, I, C>>);
static_assert(std::is_same_v<g::active_roles_t<LoggingWithoutC>, g::Roles<L, I>>);
// The crash pseudo-message is not in a queue.
static_assert(std::is_same_v<g::sending_roles_t<LoggingWithoutC>, g::Roles<>>);
static_assert(g::en_route_count_v<C, I, LoggingWithoutC> == 0);
// L sends the trigger with no crash branch, so its removal is undefined.
static_assert(std::is_same_v<g::remove_role_t<Logging, L>, g::RemovalUndefined>);
// A role that has crashed cannot crash again.
static_assert(std::is_same_v<g::remove_role_t<LoggingWithoutC, C>, g::RemovalUndefined>);
// The projections of the state after the crash.
static_assert(std::is_same_v<s::project_crash_t<LoggingWithoutC, C, LoggerAndInterface>,
                             s::NotProjectable<s::projection_failure::ProjectionOntoCrashedRole>>);
static_assert(std::is_same_v<s::project_crash_t<LoggingWithoutC, I, LoggerAndInterface>,
                             s::NotProjectable<s::projection_failure::EnRouteFromUnreliableSender>>);
static_assert(std::is_same_v<s::project_crash_t<LoggingWithoutC, L, LoggerAndInterface>::local,
                             s::Send<s::PeerMsg<I, Trigger, void>, s::Recv<s::PeerMsg<I, Fatal, void>, s::End>>>);
// The theorem covers a design-time type only.
static_assert(!s::crash_live_by_construction_v<LoggingWithoutC, LoggerAndInterface>);

// ── Remark 4.13: a receiver that crashed ────────────────────────────

struct P {};
struct Q {};
struct M {};
using Remark = g::Comm<P, Q, g::Branch<M, void, g::End>, g::Branch<g::CrashLabel, void, g::End>>;
using RemarkWithoutQ = g::remove_role_t<Remark, Q>;
static_assert(std::is_same_v<RemarkWithoutQ, g::Comm<P, g::Crashed<Q>, g::Branch<M, void, g::End>, g::Branch<g::CrashLabel, void, g::End>>>);
static_assert(std::is_same_v<g::active_roles_t<RemarkWithoutQ>, g::Roles<P>>);
static_assert(std::is_same_v<g::crashed_roles_t<RemarkWithoutQ>, g::Roles<Q>>);
// p still sends, into a lost queue.
static_assert(std::is_same_v<s::project_crash_t<RemarkWithoutQ, P, s::NoReliableRoles>::local,
                             s::Select<s::Send<s::PeerMsg<Q, M, void>, s::End>>>);
// Removal of both roles leaves the crash branch.
static_assert(std::is_same_v<g::remove_role_t<RemarkWithoutQ, P>, g::End>);

// ── Well-formedness of the crash label and the annotation ───────────

static_assert(g::global_fault_v<g::Msg<P, Q, g::CrashLabel, void, g::End>> == g::GlobalFault::CrashOnlyChoice);
static_assert(g::global_fault_v<g::Comm<P, Q, g::Branch<M, int, g::End>, g::Branch<g::CrashLabel, int, g::End>>>
              == g::GlobalFault::CrashLabelPayload);
static_assert(g::global_fault_v<g::Msg<g::Crashed<P>, Q, M, int, g::End>> == g::GlobalFault::MisplacedCrashAnnotation);
static_assert(g::global_fault_v<g::Msg<P, g::Crashed<g::Crashed<Q>>, M, int, g::End>> == g::GlobalFault::MisplacedCrashAnnotation);
static_assert(g::global_fault_v<g::EnRoute<P, g::Crashed<Q>, M, int, g::End>> == g::GlobalFault::MisplacedCrashAnnotation);
static_assert(g::global_fault_v<g::EnRoute<P, Q, g::CrashLabel, void, g::End>> == g::GlobalFault::MisplacedCrashAnnotation);
static_assert(g::global_fault_v<g::Comm<P, g::Crashed<Q>>> == g::GlobalFault::EmptyChoice);
static_assert(g::global_fault_v<g::Msg<P, g::Crashed<P>, M, int, g::End>> == g::GlobalFault::SelfCommunication);
// A role that crashed at one position and acts at another.
static_assert(!g::is_well_annotated_v<g::Comm<P, g::Crashed<Q>, g::Branch<M, int, g::Msg<Q, P, M, int, g::End>>>, g::Roles<>>);

// ── A missing crash branch cannot hide ──────────────────────────────

struct Y {};
// Inside a loop: the second transmission of the body has no crash branch.
using HiddenInLoop =
    g::Rec<g::Comm<P, Q, g::Branch<M, int, g::Msg<P, Q, Y, int, g::Var>>, g::Branch<g::CrashLabel, void, g::End>>>;
static_assert(g::is_balanced_plus_v<HiddenInLoop>);
static_assert(std::is_same_v<s::project_crash_t<HiddenInLoop, Q, s::NoReliableRoles>,
                             s::NotProjectable<s::projection_failure::MissingCrashBranch>>);
static_assert(!s::crash_live_by_construction_v<HiddenInLoop, s::NoReliableRoles>);
// Behind a third role: C merges two receptions from I, and the one in
// the crash branch has no crash branch of its own.
using HiddenBehindMerge =
    g::Comm<C, I, g::Branch<Read, void, g::Comm<I, L, g::Branch<Read, void, g::End>, g::Branch<g::CrashLabel, void, g::End>>>,
            g::Branch<g::CrashLabel, void, g::Msg<I, L, Fatal, void, g::End>>>;
static_assert(std::is_same_v<s::project_crash_t<HiddenBehindMerge, L, s::NoReliableRoles>,
                             s::NotProjectable<s::projection_failure::MissingCrashBranch>>);
static_assert(s::projects_crash_v<HiddenBehindMerge, L, s::ReliableSet<I>> == false);
using MergedWithCrash =
    g::Comm<C, I, g::Branch<Read, void, g::Comm<I, L, g::Branch<Read, void, g::End>, g::Branch<g::CrashLabel, void, g::End>>>,
            g::Branch<g::CrashLabel, void, g::Comm<I, L, g::Branch<Fatal, void, g::End>, g::Branch<g::CrashLabel, void, g::End>>>>;
// L merges the two receptions into one choice with one crash branch, last.
static_assert(std::is_same_v<s::project_crash_t<MergedWithCrash, L, s::NoReliableRoles>::local,
                             s::Offer<s::Sender<I>, s::Recv<s::PeerMsg<I, Read, void>, s::End>, s::Recv<s::PeerMsg<I, Fatal, void>, s::End>,
                                      s::Recv<s::PeerMsg<I, g::CrashLabel, void>, s::End>>>);
static_assert(s::crash_live_by_construction_v<MergedWithCrash, s::NoReliableRoles>);

// A crash detected in each iteration.  After p crashes, q takes the
// crash branch and loops back to detect the crash again.  The paper
// admits this: each detection is a step, so the path is live.
using DetectEachTime = g::Rec<g::Comm<P, Q, g::Branch<M, int, g::Var>, g::Branch<g::CrashLabel, void, g::Var>>>;
static_assert(s::crash_live_by_construction_v<DetectEachTime, s::NoReliableRoles>);
static_assert(std::is_same_v<g::remove_role_t<DetectEachTime, P>,
                             g::Rec<g::EnRoute<g::Crashed<P>, Q, g::CrashLabel, void, g::Var>>>);
static_assert(g::is_balanced_v<g::remove_role_t<DetectEachTime, P>>);

// ── Example 3.2, from the global type to the runtime ────────────────

struct Text {};
struct Answer {};
using Example = g::Comm<P, Q, g::Branch<M, int, g::Comm<Q, P, g::Branch<Answer, int, g::End>, g::Branch<g::CrashLabel, void, g::End>>>,
                        g::Branch<g::CrashLabel, void, g::End>>;
static_assert(s::crash_live_by_construction_v<Example, s::NoReliableRoles>);

using BinaryP = s::strip_peers_t<s::project_crash_t<Example, P, s::NoReliableRoles>::local>;
using BinaryQ = s::strip_peers_t<s::project_crash_t<Example, Q, s::NoReliableRoles>::local>;
static_assert(std::is_same_v<BinaryP, s::Select<s::Send<s::Labelled<M, int>,
                                                        s::Offer<s::Recv<s::Labelled<Answer, int>, s::End>, s::Recv<s::Crash<Q>, s::End>>>>>);
static_assert(std::is_same_v<BinaryQ, s::Offer<s::Recv<s::Labelled<M, int>, s::Select<s::Send<s::Labelled<Answer, int>, s::End>>>,
                                               s::Recv<s::Crash<P>, s::End>>>);
// The two binary views are crash duals, and the crash transport admits
// each of them.
static_assert(s::is_crash_dual_v<BinaryP, BinaryQ>);
static_assert(s::CrashSessionAdmissible<BinaryP, P, Q, s::NoReliableRoles>);
static_assert(s::CrashSessionAdmissible<BinaryQ, Q, P, s::NoReliableRoles>);

struct Mailbox {
    std::deque<std::uint64_t> slots;
};

struct Port {
    Mailbox* in = nullptr;
    Mailbox* out = nullptr;
};

constexpr auto push_label = [](Port& port, std::size_t label) noexcept { port.out->slots.push_back(label); };
constexpr auto push_value = [](Port& port, auto&& message) noexcept { port.out->slots.push_back(1); (void)message; };
template <typename T>
constexpr auto pop_value = [](Port& port) noexcept {
    port.in->slots.pop_front();
    return T{};
};
constexpr auto poll_label = [](Port& port) noexcept -> std::optional<std::size_t> {
    if (port.in->slots.empty()) return std::nullopt;
    const std::uint64_t slot = port.in->slots.front();
    port.in->slots.pop_front();
    return static_cast<std::size_t>(slot);
};

int fail(const char* what) {
    std::fprintf(stderr, "test_session_global_crash: %s\n", what);
    return 1;
}

// p sends and crashes.  q receives the queued message, and its reply to
// the crashed p is lost.
int run_sender_crashes_after_send() {
    Mailbox to_p;
    Mailbox to_q;
    s::PeerCrashCell cell_p;
    s::PeerCrashCell cell_q;
    auto p = s::mint_crash_session<BinaryP, P, Q>(Port{&to_p, &to_q}, cell_q);
    auto q = s::mint_crash_session<BinaryQ, Q, P>(Port{&to_q, &to_p}, cell_p);

    auto p_sent = std::move(p).select<0>(push_label);
    auto [p_wait, p_lost] = std::move(p_sent).send(s::Labelled<M, int>{}, push_value);
    if (p_lost) return fail("a payload to a live peer came back");
    (void)std::move(p_wait).crash(s::CrashCause::Abort, cell_p);

    bool took_message = false;
    std::move(q).branch(poll_label, [&](auto q_branch) noexcept {
        using Head = typename decltype(q_branch)::protocol;
        if constexpr (s::is_crash_branch_v<Head>) {
            std::fprintf(stderr, "q detected the crash before the queued message\n");
            std::abort();
        } else {
            auto [message, q_reply] = std::move(q_branch).recv(pop_value<s::Labelled<M, int>>);
            (void)message;
            auto q_sel = std::move(q_reply).template select<0>(push_label);
            auto [q_end, q_reply_lost] = std::move(q_sel).send(s::Labelled<Answer, int>{}, push_value);
            took_message = q_reply_lost.has_value();
            (void)std::move(q_end).close();
        }
    });
    if (!to_p.slots.empty()) return fail("a message reached the queue of a crashed peer");
    return took_message ? 0 : fail("q did not receive the queued message or its reply was not lost");
}

// p crashes before it sends.  q detects the crash with its cause.
int run_sender_crashes_first() {
    Mailbox to_p;
    Mailbox to_q;
    s::PeerCrashCell cell_p;
    s::PeerCrashCell cell_q;
    auto p = s::mint_crash_session<BinaryP, P, Q>(Port{&to_p, &to_q}, cell_q);
    auto q = s::mint_crash_session<BinaryQ, Q, P>(Port{&to_q, &to_p}, cell_p);
    (void)std::move(p).crash(s::CrashCause::ErrorReturn, cell_p);

    bool detected = false;
    std::move(q).branch(poll_label, [&](auto q_branch) noexcept {
        using Head = typename decltype(q_branch)::protocol;
        if constexpr (s::is_crash_branch_v<Head>) {
            auto [record, q_end] = std::move(q_branch).recv();
            detected = record.cause == s::CrashCause::ErrorReturn;
            (void)std::move(q_end).close();
        } else {
            std::fprintf(stderr, "q received from a peer that never sent\n");
            std::abort();
        }
    });
    return detected ? 0 : fail("q did not detect the crash with its cause");
}

}  // namespace

int main() {
    if (const int rc = run_sender_crashes_after_send(); rc != 0) return rc;
    if (const int rc = run_sender_crashes_first(); rc != 0) return rc;
    return 0;
}
