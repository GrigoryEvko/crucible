// The attacks on the reporter and the writer of a crash cell, and on the
// order of a crash against a write.  test_session_crash_attack.cpp runs
// each one in a child process.

#include "session_crash_attack.h"

#include <optional>
#include <utility>

namespace test_session_crash_attack {

// A cell has one reporter.  A second mint for the same cell ends the
// process with Crash_Reporter_Twice.
void second_reporter_for_one_cell() {
    CellOfP cell;
    auto first = s::mint_crash_reporter(cell);
    auto second = s::mint_crash_reporter(cell);
    static_cast<void>(std::move(first).report(s::CrashCause::Abort));
    static_cast<void>(std::move(second).report(s::CrashCause::Throw));
    finish(Outcome::Silent);
}

// One session counts into a cell.  A second writer for the same cell ends
// the process with Crash_Writer_Twice.
void second_writer_for_one_cell() {
    CellOfP cell;
    auto first = s::mint_crash_writer(cell);
    auto second = s::mint_crash_writer(cell);
    static_cast<void>(first);
    static_cast<void>(second);
    finish(Outcome::Silent);
}

// A writer that a move emptied names no cell.  The mint refuses it with
// Crash_Writer_Moved_From, so no session counts into nothing.
void mint_takes_a_moved_from_writer() {
    using ProtoQ = s::Loop<s::Select<s::Send<int, s::Continue>>>;
    Mailbox to_p;
    Mailbox to_q;
    CellOfP cell_p;
    CellOfQ cell_q;
    auto writer = s::mint_crash_writer(cell_q);
    auto kept = std::move(writer);
    auto q = s::mint_crash_session<ProtoQ, Q, P>(bg_ctx(), Port{&to_q, &to_p}, cell_p, std::move(writer));
    std::move(q).detach(s::detach_reason::InfiniteLoopProtocol{});
    static_cast<void>(kept);
    finish(Outcome::Silent);
}

// The detector reports p while p is alive.  The report fences the count of
// p, so the next message that p finishes is past the count that q reads.
// The session of p stops the process with Crash_Endpoint_Reported, so p and
// q agree on the last message that counted.
void false_suspicion_fences_the_sender() {
    using ProtoP = s::Loop<s::Select<s::Send<int, s::Continue>>>;
    Mailbox to_p;
    Mailbox to_q;
    CellOfP cell_p;
    CellOfQ cell_q;
    auto p = s::mint_crash_session<ProtoP, P, Q>(bg_ctx(), Port{&to_p, &to_q}, cell_q, s::mint_crash_writer(cell_p));
    auto chosen = std::move(p).select<0>(push_label);
    auto [next, undelivered] = std::move(chosen).send(1, push_int);
    (void)undelivered;
    report_crash(cell_p, s::CrashCause::Unknown);
    if (!report_counts(cell_p, 1)) finish(Outcome::Silent);
    auto after = std::move(next).select<0>(push_label);
    auto later = std::move(after).send(2, push_int);
    std::move(later.next).detach(s::detach_reason::TestInstrumentation{});
    finish(Outcome::Silent);
}

// The check of the crash cell and the write of the transport are two
// steps.  The peer crashes between them: the check sees it alive, and the
// peer's queue closes before the write.  A crash-watched send accepts only
// a trying write, so the closed queue refuses the token and keeps it with
// the caller.  The decorator reads the crash cell before the next try, and
// the token comes back as the undelivered payload.  A write that cannot
// refuse does not compile: neg_sess_crash_token_send_without_refusal and
// neg_sess_crash_recorded_token_send_without_refusal.
void crash_between_check_and_write_loses_token() {
    using Relay = s::Offer<s::Recv<Token, s::Select<s::Send<Token, s::End>>>, s::Recv<s::Crash<P>, s::End>>;
    Mailbox to_p;
    Mailbox to_q;
    to_q.slots.push_back(0);
    to_q.slots.push_back(7);
    CellOfP cell_p;
    CellOfQ cell_q;
    auto q = s::mint_crash_session<Relay, Q, P>(bg_ctx(), Port{&to_q, &to_p}, cell_p, s::mint_crash_writer(cell_q));
    bool token_lost = true;
    std::move(q).branch(poll_label, [&](auto branch) noexcept {
        using Head = typename decltype(branch)::protocol;
        if constexpr (s::is_crash_branch_v<Head>) {
            finish(Outcome::Silent);
        } else {
            auto [token, reply] = std::move(branch).recv(read_token);
            auto chosen = std::move(reply).template select<0>(push_label);
            auto [end, undelivered] =
                std::move(chosen).send(std::move(token), [&cell_p, &to_p](Port& port, Token& held) noexcept {
                    crash_endpoint(cell_p, to_p, s::CrashCause::Abort);
                    return push_token(port, held);
                });
            // The label went before the crash, so the queue of p holds it
            // and must hold nothing more.
            const bool is_token_queued = to_p.slots.size() > 1;
            token_lost = !(undelivered.has_value() && undelivered->peek() == 7) || is_token_queued;
            (void)std::move(end).close();
        }
    });
    finish(token_lost ? Outcome::Silent : Outcome::Correct);
}

// A bare reception from a peer counted reliable, which crashes.  The
// theory assumes a reliable peer never crashes (LMCS 2025, p. 11), and
// this endpoint and the peer disagree about that.  The reception has no
// crash branch, so no recovery exists.  The read returns no value while
// the queue is empty, so the decorator sees the crash between reads, and
// the wait ends with Crash_Of_Reliable_Peer instead of lasting for ever.
void bare_recv_from_crashed_reliable_peer() {
    using ProtoQ = s::Recv<int, s::End>;
    Mailbox to_p;
    Mailbox to_q;
    CellOfP cell_p;
    CellOfQ cell_q;
    report_crash(cell_p, s::CrashCause::Abort);
    auto q = s::mint_crash_session<ProtoQ, Q, P, s::ReliableSet<P>>(bg_ctx(), Port{&to_q, &to_p}, cell_p,
                                                                    s::mint_crash_writer(cell_q));
    auto [value, end] = std::move(q).recv(read_int);
    (void)value;
    (void)std::move(end).close();
    finish(Outcome::Silent);
}

namespace {

// The protocols of a label whose branch opens with a payload.  p sends the
// label and then the payload, and q has a crash branch only before the
// label.
using LabelThenPayload = s::Select<s::Send<int, s::End>>;
using TakesLabelThenPayload = s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<P>, s::End>>;

// q takes its crash branch, and the label in its queue does not reach it.
// The process ends with Correct from the crash branch.  A label branch
// reads the payload, which never comes, and ends with
// Crash_Splits_A_Message.
[[noreturn]] void take_crash_branch_or_split(Port port, CellOfP& cell_p, CellOfQ& cell_q) {
    auto q = s::mint_crash_session<TakesLabelThenPayload, Q, P>(bg_ctx(), std::move(port), cell_p,
                                                                s::mint_crash_writer(cell_q));
    std::move(q).branch(poll_label, [](auto branch) noexcept {
        using Head = typename decltype(branch)::protocol;
        if constexpr (s::is_crash_branch_v<Head>) {
            static_cast<void>(std::move(std::move(branch).recv().second).close());
            finish(Outcome::Correct);
        } else {
            static_cast<void>(std::move(std::move(branch).recv(read_int).second).close());
        }
    });
    finish(Outcome::Silent);
}

}  // namespace

// The peer sends a label, and its process dies before the payload of the
// branch.  The label is half of its message, so the count of the report
// does not hold it.  q finds the queue drained at the count and takes its
// crash branch.  The peer's own crash() cannot split a message:
// neg_sess_crash_between_label_and_payload and
// neg_sess_crash_recorded_between_label_and_payload.
void peer_dies_between_label_and_payload() {
    Mailbox to_p;
    Mailbox to_q;
    CellOfP cell_p;
    CellOfQ cell_q;
    auto p = s::mint_crash_session<LabelThenPayload, P, Q>(bg_ctx(), Port{&to_p, &to_q}, cell_q,
                                                           s::mint_crash_writer(cell_p));
    auto half_sent = std::move(p).select<0>(push_label);
    std::move(half_sent).detach(s::detach_reason::TestInstrumentation{});
    crash_endpoint(cell_p, to_p, s::CrashCause::Abort);
    take_crash_branch_or_split(Port{&to_q, &to_p}, cell_p, cell_q);
}

// The detector reports p while p is alive, after the label and before the
// payload.  The count of the report does not hold the message of the
// label, so q takes its crash branch, as for a real crash.
void false_report_between_label_and_payload() {
    Mailbox to_p;
    Mailbox to_q;
    CellOfP cell_p;
    CellOfQ cell_q;
    auto p = s::mint_crash_session<LabelThenPayload, P, Q>(bg_ctx(), Port{&to_p, &to_q}, cell_q,
                                                           s::mint_crash_writer(cell_p));
    auto half_sent = std::move(p).select<0>(push_label);
    report_crash(cell_p, s::CrashCause::Unknown);
    std::move(half_sent).detach(s::detach_reason::TestInstrumentation{});
    take_crash_branch_or_split(Port{&to_q, &to_p}, cell_p, cell_q);
}

// After the report of the attack above, p goes on to the payload.  The
// report did not count the message of the label, so the count after the
// payload reads the fence and stops the process with
// Crash_Endpoint_Reported.
void reported_sender_stops_at_the_payload() {
    Mailbox to_p;
    Mailbox to_q;
    CellOfP cell_p;
    CellOfQ cell_q;
    auto p = s::mint_crash_session<LabelThenPayload, P, Q>(bg_ctx(), Port{&to_p, &to_q}, cell_q,
                                                           s::mint_crash_writer(cell_p));
    auto half_sent = std::move(p).select<0>(push_label);
    report_crash(cell_p, s::CrashCause::Unknown);
    auto sent = std::move(half_sent).send(5, push_int);
    static_cast<void>(std::move(sent.next).close());
    finish(Outcome::Silent);
}

// A keyed message with a value: its label word, then its value.  This unit
// does not include fixy/session/Projection.h, so PeerMsg has no definition
// here, as in a unit that names a local type by hand.  A crash-watched
// keyed send compiles all the same, as a plain keyed send does.  The label
// has a declared name in a named namespace, so it has a label word (rule 7
// of fixy/session/Fold.h).
struct Hello {};
using SendsHello = s::Send<s::PeerMsg<Q, Hello, int>, s::End>;

// The label word of a keyed message with a value is half of its message,
// as a positional label is.  The report after the word does not count the
// message, and p stops at the value with Crash_Endpoint_Reported.
void reported_sender_stops_at_the_keyed_value() {
    Mailbox to_p;
    Mailbox to_q;
    CellOfP cell_p;
    CellOfQ cell_q;
    auto p =
        s::mint_crash_session<SendsHello, P, Q>(bg_ctx(), Port{&to_p, &to_q}, cell_q, s::mint_crash_writer(cell_p));
    auto half_sent = std::move(p).send(push_label);
    // The word went to the queue of q.
    Port queue_of_q{&to_q, &to_p};
    if (poll_label(queue_of_q).is_none()) finish(Outcome::Silent);
    report_crash(cell_p, s::CrashCause::Unknown);
    if (!report_counts(cell_p, 0)) finish(Outcome::Silent);
    auto sent = std::move(half_sent.next).send(5, push_int);
    static_cast<void>(std::move(sent.next).close());
    finish(Outcome::Silent);
}

// The peer crashed before the label word of a keyed message.  The word is
// lost, so it comes back to the caller, and the queue of the peer stays
// empty.
void keyed_word_to_crashed_peer_comes_back() {
    Mailbox to_p;
    Mailbox to_q;
    CellOfP cell_p;
    CellOfQ cell_q;
    report_crash(cell_q, s::CrashCause::Abort);
    auto p =
        s::mint_crash_session<SendsHello, P, Q>(bg_ctx(), Port{&to_p, &to_q}, cell_q, s::mint_crash_writer(cell_p));
    auto half_sent = std::move(p).send(push_label);
    const bool is_word_back = half_sent.undelivered == s::step_wire_word_v<SendsHello>;
    std::move(half_sent.next).detach(s::detach_reason::TestInstrumentation{});
    Port queue_of_q{&to_q, &to_p};
    finish(is_word_back && poll_label(queue_of_q).is_none() ? Outcome::Correct : Outcome::Silent);
}

// A third role that p counts reliable, and the labels of the messages that
// p sends it.  Each has a declared name in a named namespace, so each
// label has a label word.
struct Third {};
struct Ready {};
struct Accepted {};
struct Refused {};

// p sends three keyed messages to the third role: a label with no value, a
// label with a value, and a keyed choice.  Then p closes, and the detector
// reports it.  No message went to q.
using SendsToThird =
    s::Send<s::PeerMsg<Third, Ready, void>,
            s::Send<s::PeerMsg<Third, Hello, int>, s::Select<s::Send<s::PeerMsg<Third, Accepted, void>, s::End>,
                                                             s::Send<s::PeerMsg<Third, Refused, void>, s::End>>>>;
using FromPOrCrashOfP = s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<P>, s::End>>;

// The cell of the pair of p and the third role.
using CellOfPForThird = s::CrashCell<P, Third>;

// The cell of p for q counts the messages of p to q, which reads the cell.
// The three messages went to the third role, so they count in the cell of
// that pair, and the report of p for q counts none.  q takes its crash
// branch.  A count of the messages to the third role in the cell of q
// makes q wait for a message that p never sent to it.
void keyed_messages_to_a_third_role_count_in_its_pair() {
    Mailbox to_p;
    Mailbox to_q;
    Mailbox to_third;
    CellOfP cell_p;
    CellOfQ cell_q;
    CellOfPForThird cell_p_for_third;
    // The port of the third role: its queue takes each message of p to it.
    Port third_port{&to_third, &to_third};
    const auto push_to_third = [&third_port](Port&, std::size_t word) noexcept { return push_label(third_port, word); };
    const auto push_int_to_third = [&third_port](Port&, int& value) noexcept { return push_int(third_port, value); };
    auto p = s::mint_crash_session<SendsToThird, P, Q, s::ReliableSet<Third>>(
        bg_ctx(), Port{&to_p, &to_q}, cell_q, s::mint_crash_writer(cell_p), s::mint_crash_writer(cell_p_for_third));
    auto ready = std::move(p).send(push_to_third);
    auto hello = std::move(ready.next).send(push_to_third);
    auto valued = std::move(hello.next).send(5, push_int_to_third);
    auto chosen = std::move(valued.next).select<0>(push_to_third);
    static_cast<void>(std::move(chosen).close());
    report_crash(cell_p, s::CrashCause::Abort);
    report_crash(cell_p_for_third, s::CrashCause::Abort);
    int messages_to_third = 0;
    while (poll_label(third_port).is_some())
        ++messages_to_third;
    Port queue_of_q{&to_q, &to_p};
    const bool is_each_pair_counted = report_counts(cell_p, 0) && report_counts(cell_p_for_third, 3);
    if (messages_to_third != 4 || poll_label(queue_of_q).is_some() || !is_each_pair_counted) finish(Outcome::Silent);
    auto q = s::mint_crash_session<FromPOrCrashOfP, Q, P>(bg_ctx(), Port{&to_q, &to_p}, cell_p,
                                                          s::mint_crash_writer(cell_q));
    std::move(q).branch(poll_label, [](auto branch) noexcept {
        using Head = typename decltype(branch)::protocol;
        if constexpr (s::is_crash_branch_v<Head>) {
            static_cast<void>(std::move(std::move(branch).recv().second).close());
            finish(Outcome::Correct);
        } else {
            static_cast<void>(std::move(std::move(branch).recv(read_int).second).close());
        }
    });
    finish(Outcome::Silent);
}

// The channel peer q crashed before p sends.  The three keyed messages go
// to the third role, which p counts reliable and which is alive.  So each
// word and each value reaches the queue of the third role, and no send
// gives back an undelivered part.
void keyed_messages_to_a_third_role_survive_the_peer_crash() {
    Mailbox to_p;
    Mailbox to_q;
    Mailbox to_third;
    CellOfP cell_p;
    CellOfQ cell_q;
    CellOfPForThird cell_p_for_third;
    report_crash(cell_q, s::CrashCause::Abort);
    Port third_port{&to_third, &to_third};
    const auto push_to_third = [&third_port](Port&, std::size_t word) noexcept { return push_label(third_port, word); };
    const auto push_int_to_third = [&third_port](Port&, int& value) noexcept { return push_int(third_port, value); };
    auto p = s::mint_crash_session<SendsToThird, P, Q, s::ReliableSet<Third>>(
        bg_ctx(), Port{&to_p, &to_q}, cell_q, s::mint_crash_writer(cell_p), s::mint_crash_writer(cell_p_for_third));
    auto ready = std::move(p).send(push_to_third);
    auto hello = std::move(ready.next).send(push_to_third);
    auto valued = std::move(hello.next).send(5, push_int_to_third);
    auto chosen = std::move(valued.next).select<0>(push_to_third);
    static_cast<void>(std::move(chosen).close());
    // The queue holds the word of Ready, the word of Hello, its value, and
    // the word of Accepted, in that order, and nothing more.  A part that a
    // send gives back is a part that the transport did not take, so a whole
    // queue also tells that no send gave a part back.
    const bool is_queue_whole =
        poll_label(third_port).value_or(0) == s::step_wire_word_v<SendsToThird>
        && poll_label(third_port).value_or(0) == s::step_wire_word_v<s::Send<s::PeerMsg<Third, Hello, int>, s::End>>
        && poll_label(third_port).value_or(0) == 5
        && poll_label(third_port).value_or(0) == s::step_wire_word_v<s::Send<s::PeerMsg<Third, Accepted, void>, s::End>>
        && poll_label(third_port).is_none();
    finish(is_queue_whole ? Outcome::Correct : Outcome::Silent);
}

// The detector reports p on its cell for q, while p is alive.  The report
// fences p, so the next message that p finishes stops the process with
// Crash_Endpoint_Reported, also when that message goes to the third role.
void reported_sender_stops_at_a_message_to_the_third_role() {
    Mailbox to_p;
    Mailbox to_q;
    Mailbox to_third;
    CellOfP cell_p;
    CellOfQ cell_q;
    CellOfPForThird cell_p_for_third;
    Port third_port{&to_third, &to_third};
    const auto push_to_third = [&third_port](Port&, std::size_t word) noexcept { return push_label(third_port, word); };
    auto p = s::mint_crash_session<SendsToThird, P, Q, s::ReliableSet<Third>>(
        bg_ctx(), Port{&to_p, &to_q}, cell_q, s::mint_crash_writer(cell_p), s::mint_crash_writer(cell_p_for_third));
    report_crash(cell_p, s::CrashCause::Unknown);
    auto ready = std::move(p).send(push_to_third);
    std::move(ready.next).detach(s::detach_reason::TestInstrumentation{});
    finish(Outcome::Silent);
}

// A transport that shows a label to the peer before its payload breaks
// the trust of the crash transport.  q reads the label, then p dies before
// the payload.  The branch of the label has no crash branch, so the wait
// for the payload ends with Crash_Splits_A_Message.
void peer_reads_label_before_the_report() {
    Mailbox to_p;
    Mailbox to_q;
    CellOfP cell_p;
    CellOfQ cell_q;
    auto p = s::mint_crash_session<LabelThenPayload, P, Q>(bg_ctx(), Port{&to_p, &to_q}, cell_q,
                                                           s::mint_crash_writer(cell_p));
    auto q = s::mint_crash_session<TakesLabelThenPayload, Q, P>(bg_ctx(), Port{&to_q, &to_p}, cell_p,
                                                                s::mint_crash_writer(cell_q));
    auto half_sent = std::move(p).select<0>(push_label);
    std::move(half_sent).detach(s::detach_reason::TestInstrumentation{});
    std::move(q).branch(poll_label, [&cell_p, &to_p](auto branch) noexcept {
        using Head = typename decltype(branch)::protocol;
        if constexpr (s::is_crash_branch_v<Head>) {
            finish(Outcome::Silent);
        } else {
            crash_endpoint(cell_p, to_p, s::CrashCause::Abort);
            static_cast<void>(std::move(std::move(branch).recv(read_int).second).close());
        }
    });
    finish(Outcome::Silent);
}

}  // namespace test_session_crash_attack
