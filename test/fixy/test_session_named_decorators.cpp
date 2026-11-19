// A recorded session and a crash session over named states (Named of
// fixy/session/Protocol.h).  A step resolves a name, so no handle stands at
// a named state, and a decorator steps through the state as through its
// protocol.
//
//   - A recorded session whose next step is a named state records the
//     same events as the session over the protocol of the name.
//   - A recorded session picks the branch of a choice that is a named
//     state, and its peer enters that branch.
//   - A recorded crash session starts at a named state that holds an Offer,
//     and it takes the crash branch when its peer crashed.

#include "session_recording.h"

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/session/Entry.h>

#include <foundation/effects/Ctx.h>

#include <type_traits>
#include <utility>

namespace test_session_named_decorators_types {

using namespace ::test_session_recording_types;

namespace {

using TestCtx = ::fixy::TestRunnerCtx;

// ── A step into a named state ───────────────────────────────────────

struct Tell {
    using protocol = s::Send<int, s::End>;
};
using AskThenTell = s::Send<int, s::Named<Tell>>;

void check_recorded_step_into_a_name(TestCtx const& test) {
    Mailbox to_self;
    Mailbox to_peer;
    s::SessionEventLog log;
    auto handle = s::mint_recorded_session(s::mint_session<AskThenTell>(bg_ctx(), Port{&to_self, &to_peer}),
                                           s::mint_session_event_writer(log), kSelf, kPeer);
    auto told = std::move(handle).send(5, push_int);
    static_assert(std::is_same_v<typename decltype(told)::protocol, s::Send<int, s::End>>,
                  "the step resolves the named state, so the handle stands at the protocol of the name");
    auto at_end = std::move(told).send(9, push_int);
    static_cast<void>(std::move(at_end).close());
    CRUCIBLE_TEST_ASSERT(test, to_peer.slots.size() == 2 && to_peer.slots[0] == 5 && to_peer.slots[1] == 9);
    CRUCIBLE_TEST_ASSERT(test, log.size() == 3);
    CRUCIBLE_TEST_ASSERT(test, log[0].op() == s::SessionOp::Send && log[1].op() == s::SessionOp::Send);
    CRUCIBLE_TEST_ASSERT(test, log[1].payload_schema() == s::default_schema_hash<int>);
    CRUCIBLE_TEST_ASSERT(test, log[2].op() == s::SessionOp::Close);
}

// ── A branch that is a named state ──────────────────────────────────

struct Done {
    using protocol = s::End;
};
using Chooser = s::Select<s::Send<int, s::End>, s::Named<Done>>;
using ChooserPeer = s::dual_of_t<Chooser>;

void check_recorded_named_branch(TestCtx const& test) {
    Mailbox to_left;
    Mailbox to_right;
    s::SessionEventLog log_left;
    s::SessionEventLog log_right;
    auto left = s::mint_recorded_session(s::mint_session<Chooser>(bg_ctx(), Port{&to_left, &to_right}),
                                         s::mint_session_event_writer(log_left), kSelf, kPeer);
    auto right = s::mint_recorded_session(s::mint_session<ChooserPeer>(bg_ctx(), Port{&to_right, &to_left}),
                                          s::mint_session_event_writer(log_right), kPeer, kSelf);
    auto chosen = std::move(left).select<1>(push_label);
    static_assert(std::is_same_v<typename decltype(chosen)::protocol, s::End>,
                  "the branch that is a named state stands at the protocol of the name");
    static_cast<void>(std::move(chosen).close());
    bool took_name = false;
    std::move(right).branch(pop_label, [&](auto branch) {
        if constexpr (std::is_same_v<typename decltype(branch)::protocol, s::End>) {
            took_name = true;
            static_cast<void>(std::move(branch).close());
        } else {
            ::fixy::fatal("the peer entered the branch of the int, and the chooser picked the named branch");
        }
    });
    CRUCIBLE_TEST_ASSERT(test, took_name);
    CRUCIBLE_TEST_ASSERT(test, log_left.size() == 2 && log_left[0].op() == s::SessionOp::Select
                                   && log_left[0].branch_index() == 1);
    CRUCIBLE_TEST_ASSERT(test, log_right.size() == 2 && log_right[0].op() == s::SessionOp::Offer
                                   && log_right[0].branch_index() == 1);
}

// ── A crash session over a named state ──────────────────────────────

struct P {};
struct Q {};

// P starts at a named state: an Offer of the value of Q and of the crash of
// Q.  The crash erasure of P is the dual of the protocol of Q.
struct WaitForQ {
    using protocol = s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<Q>, s::End>>;
};
using NamedP = s::Named<WaitForQ>;
using PlainQ = s::Select<s::Send<int, s::End>>;

static_assert(s::is_crash_well_formed_v<NamedP>, "a crash branch of an Offer that a named state holds is well-formed");

void check_recorded_crash_over_a_name(TestCtx const& test) {
    Mailbox to_p;
    Mailbox to_q;
    s::CrashCell<P, Q> cell_p;
    s::CrashCell<Q, P> cell_q;
    s::SessionEventLog log_p;
    s::SessionEventLog log_q;
    auto p = s::mint_recorded_session(
        s::mint_crash_session<NamedP, P, Q>(bg_ctx(), Port{&to_p, &to_q}, cell_q, s::mint_crash_writer(cell_p)),
        s::mint_session_event_writer(log_p), kSelf, kPeer);
    auto q = s::mint_recorded_session(
        s::mint_crash_session<PlainQ, Q, P>(bg_ctx(), Port{&to_q, &to_p}, cell_p, s::mint_crash_writer(cell_q)),
        s::mint_session_event_writer(log_q), kPeer, kSelf);

    static_cast<void>(std::move(q).crash(s::CrashCause::Throw));
    bool took_crash = false;
    std::move(p).branch(pop_label, [&](auto branch) {
        // The crash arm ends with a crash of P, so the test binds no pair of a
        // receive (the rule quarantine counts each).
        if constexpr (std::is_same_v<typename decltype(branch)::protocol, s::Recv<s::Crash<Q>, s::End>>) {
            took_crash = true;
            static_cast<void>(std::move(branch).crash(s::CrashCause::ErrorReturn));
        } else {
            ::fixy::fatal("p entered the value branch of the named state, and its peer crashed");
        }
    });
    CRUCIBLE_TEST_ASSERT(test, took_crash);
    CRUCIBLE_TEST_ASSERT(test, log_q.size() == 1 && log_q[0].op() == s::SessionOp::Stop
                                   && log_q[0].stop_reason() == s::StopReasonKind::LocalAbort);
    CRUCIBLE_TEST_ASSERT(test, log_p.size() >= 2);
    CRUCIBLE_TEST_ASSERT(test, log_p[0].op() == s::SessionOp::Offer && log_p[0].branch_index() == 1);
    CRUCIBLE_TEST_ASSERT(test, log_p[log_p.size() - 1].op() == s::SessionOp::Stop
                                   && log_p[log_p.size() - 1].stop_reason() == s::StopReasonKind::LocalAbort
                                   && log_p[log_p.size() - 1].crash_cause() == s::CrashCause::ErrorReturn);
}

}  // namespace

}  // namespace test_session_named_decorators_types

int main() {
    namespace t = ::test_session_named_decorators_types;
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    t::check_recorded_step_into_a_name(test);
    t::check_recorded_named_branch(test);
    t::check_recorded_crash_over_a_name(test);
    ::fixy::check::pass(test, "test_session_named_decorators: all checks passed\n");
    return 0;
}
