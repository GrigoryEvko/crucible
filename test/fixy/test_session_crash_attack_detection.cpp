// The attacks on the detection of a crash, at a reception and at a send.
// test_session_crash_attack.cpp runs each one in a child process.

#include "session_crash_attack.h"

#include <optional>
#include <utility>

namespace test_session_crash_attack {

// Each endpoint names its own reliable set.  q counts p reliable, so its
// Offer has no crash branch, and p, whose own set is empty, crashes.
// Without a check q would wait for ever.  The crash transport aborts
// with Crash_Of_Reliable_Peer.
void reliable_peer_crashes_while_waited_on() {
    using ProtoP = s::Select<s::Send<int, s::End>>;
    using ProtoQ = s::Offer<s::Recv<int, s::End>>;
    Mailbox to_p;
    Mailbox to_q;
    s::PeerCrashCell cell_p;
    s::PeerCrashCell cell_q;
    auto p = s::mint_crash_session<ProtoP, P, Q>(bg_ctx(), Port{&to_p, &to_q}, cell_q, s::mint_crash_writer(cell_p));
    auto q = s::mint_crash_session<ProtoQ, Q, P, s::ReliableSet<P>>(bg_ctx(), Port{&to_q, &to_p}, cell_p,
                                                                    s::mint_crash_writer(cell_q));
    (void)std::move(p).crash(s::CrashCause::Abort);
    std::move(q).branch(poll_label, [](auto branch) noexcept {
        auto [value, end] = std::move(branch).recv(read_int);
        (void)value;
        (void)std::move(end).close();
    });
    finish(Outcome::Silent);
}

// A send to a crashed peer gives the payload back and writes nothing.
void send_to_crashed_peer_returns_payload() {
    using ProtoQ = s::Loop<s::Select<s::Send<int, s::Continue>>>;
    Mailbox to_p;
    Mailbox to_q;
    s::PeerCrashCell cell_p;
    s::PeerCrashCell cell_q;
    report_crash(cell_p, s::CrashCause::Throw);
    auto q = s::mint_crash_session<ProtoQ, Q, P>(bg_ctx(), Port{&to_q, &to_p}, cell_p, s::mint_crash_writer(cell_q));
    int returned = 0;
    for (int round = 0; round < 3; ++round) {
        auto sent = std::move(q).select<0>(push_label);
        auto [next, undelivered] = std::move(sent).send(round, push_int);
        if (undelivered && *undelivered == round) ++returned;
        q = std::move(next);
    }
    std::move(q).detach(s::detach_reason::InfiniteLoopProtocol{});
    finish(returned == 3 && to_p.slots.empty() ? Outcome::Correct : Outcome::Silent);
}

// q relays a token to p, and p has crashed.  The token comes back once,
// in the undelivered payload, and the queue of p holds nothing.
void token_relayed_to_crashed_peer() {
    using Relay = s::Offer<s::Recv<Token, s::Select<s::Send<Token, s::End>>>, s::Recv<s::Crash<P>, s::End>>;
    Mailbox to_p;
    Mailbox to_q;
    to_q.slots.push_back(0);  // the label of the message branch
    to_q.slots.push_back(7);  // the value of the token's payload
    s::PeerCrashCell cell_p;
    s::PeerCrashCell cell_q;
    auto q = s::mint_crash_session<Relay, Q, P>(bg_ctx(), Port{&to_q, &to_p}, cell_p, s::mint_crash_writer(cell_q));
    bool came_back_once = false;
    std::move(q).branch(poll_label, [&](auto branch) noexcept {
        using Head = typename decltype(branch)::protocol;
        if constexpr (s::is_crash_branch_v<Head>) {
            finish(Outcome::Silent);
        } else {
            auto [token, reply] = std::move(branch).recv(read_token);
            crash_endpoint(cell_p, to_p, s::CrashCause::Abort);
            auto chosen = std::move(reply).template select<0>(push_label);
            auto [end, undelivered] = std::move(chosen).send(std::move(token), push_token);
            came_back_once = undelivered.has_value() && undelivered->value == 7;
            (void)std::move(end).close();
        }
    });
    finish(came_back_once && to_p.slots.empty() ? Outcome::Correct : Outcome::Silent);
}

// q detects the crash of p, and its crash branch receives from p again.
// The second reception detects the crash again, as rule r-rcv-⊙ does.
void crash_detected_twice() {
    using Again = s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<P>, s::End>>;
    using ProtoQ = s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<P>, Again>>;
    Mailbox to_p;
    Mailbox to_q;
    s::PeerCrashCell cell_p;
    s::PeerCrashCell cell_q;
    report_crash(cell_p, s::CrashCause::ErrorReturn);
    auto q = s::mint_crash_session<ProtoQ, Q, P>(bg_ctx(), Port{&to_q, &to_p}, cell_p, s::mint_crash_writer(cell_q));
    int detections = 0;
    std::move(q).branch(poll_label, [&](auto first) noexcept {
        using Head = typename decltype(first)::protocol;
        if constexpr (s::is_crash_branch_v<Head>) {
            auto [record, again] = std::move(first).recv();
            if (record.cause == s::CrashCause::ErrorReturn) ++detections;
            std::move(again).branch(poll_label, [&](auto second) noexcept {
                using Next = typename decltype(second)::protocol;
                if constexpr (s::is_crash_branch_v<Next>) {
                    auto [record2, end] = std::move(second).recv();
                    if (record2.cause == s::CrashCause::ErrorReturn) ++detections;
                    (void)std::move(end).close();
                } else {
                    finish(Outcome::Silent);
                }
            });
        } else {
            finish(Outcome::Silent);
        }
    });
    finish(detections == 2 ? Outcome::Correct : Outcome::Silent);
}

// Fairness clause F3: a detection that stays enabled happens.  Four
// messages are queued before the crash.  q receives all four, and the
// fifth Offer takes the crash branch, so the queue bounds the delay.
void queue_drains_before_detection() {
    using ProtoQ = s::Loop<s::Offer<s::Recv<int, s::Continue>, s::Recv<s::Crash<P>, s::End>>>;
    Mailbox to_p;
    Mailbox to_q;
    s::PeerCrashCell cell_p;
    s::PeerCrashCell cell_q;
    p_sends(cell_p, cell_q, to_p, to_q, 1, 4);
    report_crash(cell_p, s::CrashCause::Abort);
    auto q = s::mint_crash_session<ProtoQ, Q, P>(bg_ctx(), Port{&to_q, &to_p}, cell_p, s::mint_crash_writer(cell_q));
    int received = 0;
    bool detected = false;
    run_loop_to_crash(
        std::move(q), poll_label,
        [&](auto branch) noexcept {
            auto [value, next] = std::move(branch).recv(read_int);
            if (value == received + 1) ++received;
            return std::move(next);
        },
        [&](auto branch) noexcept {
            auto [record, end] = std::move(branch).recv();
            (void)record;
            detected = true;
            (void)std::move(end).close();
        });
    finish(received == 4 && detected ? Outcome::Correct : Outcome::Silent);
}

// A label at or after the first crash branch is the crash
// pseudo-message, which no peer can send.
void peer_sends_the_crash_label() {
    using ProtoQ = s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<P>, s::End>>;
    Mailbox to_p;
    Mailbox to_q;
    to_q.slots.push_back(1);
    s::PeerCrashCell cell_p;
    s::PeerCrashCell cell_q;
    auto q = s::mint_crash_session<ProtoQ, Q, P>(bg_ctx(), Port{&to_q, &to_p}, cell_p, s::mint_crash_writer(cell_q));
    std::move(q).branch(
        poll_label, [](auto branch) noexcept { std::move(branch).detach(s::detach_reason::TestInstrumentation{}); });
    finish(Outcome::Silent);
}

}  // namespace test_session_crash_attack
