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
    CellOfP cell_p;
    CellOfQ cell_q;
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
    CellOfP cell_p;
    CellOfQ cell_q;
    report_crash(cell_p, s::CrashCause::Throw);
    auto q = s::mint_crash_session<ProtoQ, Q, P>(bg_ctx(), Port{&to_q, &to_p}, cell_p, s::mint_crash_writer(cell_q));
    int returned = 0;
    for (int round = 0; round < 3; ++round) {
        auto sent = std::move(q).select<0>(push_label);
        auto [next, undelivered] = std::move(sent).send(round, push_int);
        if (undelivered == ::fixy::some(round)) ++returned;
        q = std::move(next);
    }
    std::move(q).detach(s::detach_reason::InfiniteLoopProtocol{});
    finish(returned == 3 && to_p.slots.size() == 0 ? Outcome::Correct : Outcome::Silent);
}

// The payload that a send gives back is in the base Option, which holds a
// linear payload such as a token, and which no assignment drops.
template <typename T>
inline constexpr bool is_base_option_v = false;
template <typename T>
inline constexpr bool is_base_option_v<::fixy::Option<T>> = true;
static_assert(is_base_option_v<decltype(s::CrashSend<int, Token>::undelivered)>,
              "a crash send gives the payload back in the base Option");

// q relays a token to p, and p has crashed.  The token comes back once,
// in the undelivered payload, and the queue of p holds nothing.
void token_relayed_to_crashed_peer() {
    using Relay = s::Offer<s::Recv<Token, s::Select<s::Send<Token, s::End>>>, s::Recv<s::Crash<P>, s::End>>;
    Mailbox to_p;
    Mailbox to_q;
    to_q.push(0);  // the label of the message branch
    to_q.push(7);  // the value of the token's payload
    CellOfP cell_p;
    CellOfQ cell_q;
    auto q = s::mint_crash_session<Relay, Q, P>(bg_ctx(), Port{&to_q, &to_p}, cell_p, s::mint_crash_writer(cell_q));
    bool came_back_once = false;
    std::move(q).branch(poll_label, [&](auto branch) noexcept {
        using Head = typename decltype(branch)::protocol;
        if constexpr (s::is_crash_branch_v<Head>) {
            finish(Outcome::Silent);
        } else {
            // The reception puts the token into the slots and gives the value.
            auto [payload, reply] = std::move(branch).recv(read_token);
            crash_endpoint(cell_p, to_p, s::CrashCause::Abort);
            auto chosen = std::move(reply).template select<0>(push_label);
            auto [end, undelivered] = std::move(chosen).send(std::move(payload), push_token);
            // The step took the token out of the slots to build the marker,
            // and the stopped send gives the marker back.  So the handle
            // after the send claims no slot.
            static_assert(
                std::is_same_v<typename decltype(end)::handle_type::perm_set, ::foundation::permissions::EmptyPermSet>,
                "the handle after a stopped send of a marker claims no slot");
            came_back_once = take_token_back(std::move(undelivered), 7);
            (void)std::move(end).close();
        }
    });
    finish(came_back_once && to_p.slots.size() == 0 ? Outcome::Correct : Outcome::Silent);
}

// q detects the crash of p, and its crash branch receives from p again.
// The second reception detects the crash again, as rule r-rcv-⊙ does.
void crash_detected_twice() {
    using Again = s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<P>, s::End>>;
    using ProtoQ = s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<P>, Again>>;
    Mailbox to_p;
    Mailbox to_q;
    CellOfP cell_p;
    CellOfQ cell_q;
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
    CellOfP cell_p;
    CellOfQ cell_q;
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
    to_q.push(1);
    CellOfP cell_p;
    CellOfQ cell_q;
    auto q = s::mint_crash_session<ProtoQ, Q, P>(bg_ctx(), Port{&to_q, &to_p}, cell_p, s::mint_crash_writer(cell_q));
    std::move(q).branch(
        poll_label, [](auto branch) noexcept { std::move(branch).detach(s::detach_reason::TestInstrumentation{}); });
    finish(Outcome::Silent);
}

namespace {

// A third role that q counts reliable.  It sends to q on the same queue
// as the watched peer p.
struct R {};

// An Offer of r, with no crash branch because r is reliable.
template <typename Next>
using FromR = s::Offer<s::Sender<R>, s::Recv<int, Next>>;
using FromPOrCrash = s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<P>, s::End>>;

// r puts the label of its Offer and the payload 7 into the queue of q.
void r_sends_seven(Mailbox& to_q) noexcept {
    Mailbox to_r;
    Port r_port{&to_r, &to_q};
    static_cast<void>(push_label(r_port, 0));
    int seven = 7;
    static_cast<void>(push_int(r_port, seven));
}

// A polling read of an int that also writes the value into a slot of the
// attack, so a step needs no pair of the value and the next handle.
[[nodiscard]] auto read_int_into(int& slot) noexcept {
    return [&slot](Port& port) noexcept -> ::fixy::Option<int> {
        ::fixy::Option<int> got = read_int(port);
        for (int const value : got)
            slot = value;
        return got;
    };
}

}  // namespace

// p sends one message and crashes, and its report counts one.  A message
// of r reaches q first.  The report counts the messages of p only, so q
// waits for the message of p and receives it, and does not take its crash
// branch at the count of r.
void reliable_message_does_not_drain_the_peer() {
    using ProtoP = s::Select<s::Send<int, s::End>>;
    Mailbox to_p;
    Mailbox to_q;
    CellOfP cell_p;
    CellOfQ cell_q;
    r_sends_seven(to_q);
    auto p = s::mint_crash_session<ProtoP, P, Q>(bg_ctx(), Port{&to_p, &to_q}, cell_q, s::mint_crash_writer(cell_p));
    auto chosen = std::move(p).select<0>(push_label);
    auto sent = std::move(chosen).send(5, push_int);
    static_cast<void>(std::move(sent.next).close());
    report_crash(cell_p, s::CrashCause::Abort);
    auto q = s::mint_crash_session<FromR<FromPOrCrash>, Q, P, s::ReliableSet<R>>(bg_ctx(), Port{&to_q, &to_p}, cell_p,
                                                                                 s::mint_crash_writer(cell_q));
    int from_r = 0;
    int from_p = 0;
    std::move(q).branch(poll_label, [&from_r, &from_p](auto message_of_r) noexcept {
        auto [value_of_r, waits_for_p] = std::move(message_of_r).recv(read_int_into(from_r));
        static_cast<void>(value_of_r);
        if (from_r != 7) finish(Outcome::Silent);
        std::move(waits_for_p).branch(poll_label, [&from_p](auto branch) noexcept {
            using Head = typename decltype(branch)::protocol;
            if constexpr (s::is_crash_branch_v<Head>) {
                finish(Outcome::Silent);
            } else {
                auto [value_of_p, at_end] = std::move(branch).recv(read_int_into(from_p));
                static_cast<void>(value_of_p);
                static_cast<void>(std::move(at_end).close());
            }
        });
    });
    finish(from_p == 5 ? Outcome::Correct : Outcome::Silent);
}

// p crashes and sends nothing.  r stays alive, and its message waits in
// the queue of q.  An Offer of r does not read the report of p, so q takes
// the message of r.
void reliable_offer_while_the_peer_is_crashed() {
    Mailbox to_p;
    Mailbox to_q;
    CellOfP cell_p;
    CellOfQ cell_q;
    report_crash(cell_p, s::CrashCause::Abort);
    r_sends_seven(to_q);
    auto q = s::mint_crash_session<FromR<s::End>, Q, P, s::ReliableSet<R>>(bg_ctx(), Port{&to_q, &to_p}, cell_p,
                                                                           s::mint_crash_writer(cell_q));
    int from_r = 0;
    std::move(q).branch(poll_label, [&from_r](auto branch) noexcept {
        auto [value_of_r, at_end] = std::move(branch).recv(read_int_into(from_r));
        static_cast<void>(value_of_r);
        static_cast<void>(std::move(at_end).close());
    });
    finish(from_r == 7 ? Outcome::Correct : Outcome::Silent);
}

}  // namespace test_session_crash_attack
