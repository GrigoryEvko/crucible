// The attacks of a transport that reports messages that the crashed peer
// never sent.  test_session_crash_attack.cpp runs each one in a child
// process.

#include "session_crash_attack.h"

#include <cstdint>
#include <optional>
#include <utility>

namespace test_session_crash_attack {

// A transport that fabricates messages after the crash.  The report says
// that the peer sent no message, so a word that the transport reports is
// past the witness: the decorator refuses it and takes the crash branch.
void transport_fabricates_messages_after_crash() {
    using ProtoQ = s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<P>, s::End>>;
    Mailbox to_p;
    Mailbox to_q;
    s::PeerCrashCell cell_p;
    s::PeerCrashCell cell_q;
    report_crash(cell_p, s::CrashCause::Abort);
    auto q = s::mint_crash_session<ProtoQ, Q, P>(bg_ctx(), Port{&to_q, &to_p}, cell_p, s::mint_crash_writer(cell_q));
    bool took_message = false;
    std::move(q).branch([](Port&) noexcept -> std::optional<std::size_t> { return std::size_t{0}; },
                        [&](auto branch) noexcept {
                            using Head = typename decltype(branch)::protocol;
                            if constexpr (s::is_crash_branch_v<Head>) {
                                auto [record, end] = std::move(branch).recv();
                                (void)record;
                                (void)std::move(end).close();
                            } else {
                                auto [value, end] =
                                    std::move(branch).recv([](Port&) noexcept -> std::optional<int> { return 99; });
                                took_message = value == 99;
                                (void)std::move(end).close();
                            }
                        });
    finish(took_message ? Outcome::Silent : Outcome::Correct);
}

// The peer sent two messages before it crashed, and the transport keeps
// reporting messages after them.  The decorator takes the two, because
// the report counts them, and refuses the third.
void transport_fabricates_after_queued_messages() {
    using ProtoQ = s::Loop<s::Offer<s::Recv<int, s::Continue>, s::Recv<s::Crash<P>, s::End>>>;
    Mailbox to_p;
    Mailbox to_q;
    s::PeerCrashCell cell_p;
    s::PeerCrashCell cell_q;
    p_sends(cell_p, cell_q, to_p, to_q, 5, 2);
    report_crash(cell_p, s::CrashCause::Abort);
    auto q = s::mint_crash_session<ProtoQ, Q, P>(bg_ctx(), Port{&to_q, &to_p}, cell_p, s::mint_crash_writer(cell_q));
    int received = 0;
    bool detected = false;
    const auto always_a_word = [](Port&) noexcept -> std::optional<std::size_t> { return std::size_t{0}; };
    const auto always_a_value = [](Port&) noexcept -> std::optional<int> { return 5; };
    run_loop_to_crash(
        std::move(q), always_a_word,
        [&](auto branch) noexcept {
            auto [value, next] = std::move(branch).recv(always_a_value);
            if (value == 5) ++received;
            return std::move(next);
        },
        [&](auto branch) noexcept {
            auto [record, end] = std::move(branch).recv();
            detected = record.cause == s::CrashCause::Abort;
            (void)std::move(end).close();
        });
    finish(received == 2 && detected ? Outcome::Correct : Outcome::Silent);
}

// The report counts one message that has not arrived yet.  The queue is
// not empty, so the decorator waits for it instead of taking the crash
// branch, and takes the crash branch at the next reception.
void crash_branch_waits_for_an_owed_message() {
    using ProtoQ = s::Loop<s::Offer<s::Recv<int, s::Continue>, s::Recv<s::Crash<P>, s::End>>>;
    Mailbox to_p;
    Mailbox in_flight;
    Mailbox to_q;
    s::PeerCrashCell cell_p;
    s::PeerCrashCell cell_q;
    // p sent one message, which is still in flight when the report comes.
    p_sends(cell_p, cell_q, to_p, in_flight, 11, 1);
    report_crash(cell_p, s::CrashCause::Throw);
    auto q = s::mint_crash_session<ProtoQ, Q, P>(bg_ctx(), Port{&to_q, &to_p}, cell_p, s::mint_crash_writer(cell_q));
    int empty_polls = 0;
    int received = 0;
    bool detected = false;
    // The message arrives only after three polls find nothing.
    const auto late_word = [&empty_polls, &in_flight, &to_q](Port& port) noexcept -> std::optional<std::size_t> {
        if (empty_polls < 3) {
            ++empty_polls;
            if (empty_polls == 3) {
                for (const std::uint64_t slot : in_flight.slots)
                    to_q.slots.push_back(slot);
                in_flight.slots.clear();
            }
            return std::nullopt;
        }
        return poll_label(port);
    };
    run_loop_to_crash(
        std::move(q), late_word,
        [&](auto branch) noexcept {
            auto [value, next] = std::move(branch).recv(read_int);
            if (value == 11) ++received;
            return std::move(next);
        },
        [&](auto branch) noexcept {
            auto [record, end] = std::move(branch).recv();
            (void)record;
            detected = true;
            (void)std::move(end).close();
        });
    finish(received == 1 && detected && empty_polls == 3 ? Outcome::Correct : Outcome::Silent);
}

// An endpoint that crashes reports the count that its session wrote into
// its cell: three messages.  The survivor receives the three and then takes
// the crash branch, although its transport keeps reporting messages.
void endpoint_reports_its_own_count() {
    using ProtoP = s::Loop<s::Select<s::Send<int, s::Continue>>>;
    using ProtoQ = s::Loop<s::Offer<s::Recv<int, s::Continue>, s::Recv<s::Crash<P>, s::End>>>;
    Mailbox to_p;
    Mailbox to_q;
    s::PeerCrashCell cell_p;
    s::PeerCrashCell cell_q;
    auto p = s::mint_crash_session<ProtoP, P, Q>(bg_ctx(), Port{&to_p, &to_q}, cell_q, s::mint_crash_writer(cell_p));
    for (int round = 0; round < 3; ++round) {
        auto chosen = std::move(p).select<0>(push_label);
        auto [next, undelivered] = std::move(chosen).send(round, push_int);
        (void)undelivered;
        p = std::move(next);
    }
    if (p.messages_sent() != 3) finish(Outcome::Silent);
    (void)std::move(p).crash(s::CrashCause::ErrorReturn);
    const std::optional<s::CrashWitness> witness = cell_p.witness();
    if (!witness || std::to_underlying(witness->messages_sent) != 3) finish(Outcome::Silent);
    auto q = s::mint_crash_session<ProtoQ, Q, P>(bg_ctx(), Port{&to_q, &to_p}, cell_p, s::mint_crash_writer(cell_q));
    int received = 0;
    bool detected = false;
    // The queue holds the three messages, and the transport invents more
    // after them.
    const auto word_or_more = [](Port& port) noexcept -> std::optional<std::size_t> {
        if (auto word = poll_label(port)) return word;
        return std::size_t{0};
    };
    const auto value_or_more = [](Port& port) noexcept -> std::optional<int> {
        if (auto value = read_int(port)) return value;
        return 99;
    };
    run_loop_to_crash(
        std::move(q), word_or_more,
        [&](auto branch) noexcept {
            auto [value, next] = std::move(branch).recv(value_or_more);
            if (value == received) ++received;
            return std::move(next);
        },
        [&](auto branch) noexcept {
            auto [record, end] = std::move(branch).recv();
            detected = record.cause == s::CrashCause::ErrorReturn;
            (void)std::move(end).close();
        });
    finish(received == 3 && detected ? Outcome::Correct : Outcome::Silent);
}

// A bare reception from a crashed reliable peer whose transport invents a
// value.  The report counts no message, so the decorator refuses the value
// and ends the wait with Crash_Of_Reliable_Peer.
void bare_recv_refuses_a_fabricated_message() {
    using ProtoQ = s::Recv<int, s::End>;
    Mailbox to_p;
    Mailbox to_q;
    s::PeerCrashCell cell_p;
    s::PeerCrashCell cell_q;
    report_crash(cell_p, s::CrashCause::Abort);
    auto q = s::mint_crash_session<ProtoQ, Q, P, s::ReliableSet<P>>(bg_ctx(), Port{&to_q, &to_p}, cell_p,
                                                                    s::mint_crash_writer(cell_q));
    auto [value, end] = std::move(q).recv([](Port&) noexcept -> std::optional<int> { return 99; });
    (void)value;
    (void)std::move(end).close();
    finish(Outcome::Silent);
}

}  // namespace test_session_crash_attack
