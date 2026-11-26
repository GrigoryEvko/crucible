#pragma once

// The shared part of the attacks on crash-stop sessions, checkpoint
// sessions and the event log: the endpoints, the queues, the transports,
// the outcomes and the table of the runtime attacks.  Each attack is
// defined in the source file of its subject, and test_session_crash_attack.cpp
// runs the table.

#include <fixy/session/Checkpoint.h>
#include <fixy/session/CrashTransport.h>
#include <fixy/session/Delegate.h>
#include <fixy/session/EventLog.h>
#include <fixy/session/Projection.h>
#include <fixy/session/Recording.h>

#include <foundation/Brand.h>
#include <foundation/Linear.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace test_session_crash_attack {

namespace s = ::fixy::session;
namespace fp = ::foundation::permissions;
namespace eff = ::foundation::effects;

// The context of each session of the test.  No payload here carries an
// effect row, so the background context admits every protocol.
using BgCtx = eff::detail::ctx_witnesses::BgWitness;
[[nodiscard]] inline BgCtx bg_ctx() noexcept { return BgCtx{eff::testing::bg()}; }

struct P {};
struct Q {};
struct X {
    using permission_row = ::foundation::effects::Row<>;
};

// A Resource that one holder moves.  A copy of it would reach the channel
// of a live session, so it has none.
struct OneHolderChannel {
    int* in = nullptr;
    int* out = nullptr;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};

enum class Outcome : std::uint8_t {
    Correct,
    Caught,
    Silent,
    Deadlock,
};

inline constexpr int kCorrectExit = 0;
inline constexpr int kSilentExit = 3;
inline constexpr int kDeadlockExit = 4;
inline constexpr unsigned kWatchdogSeconds = 5;

// The room of the queue into one endpoint.  An attack queues a few words.
inline constexpr std::size_t mailbox_room = 64;

// The queue into one endpoint.  A crash of that endpoint closes it, and a
// write into a closed queue is refused, which is the check a transport
// makes at the write.  The queue holds its words in place, so a transport
// that captures a mailbox by reference reaches no pointer, and the store
// check of the transports admits it.
struct Mailbox {
    ::fixy::FixedList<std::uint64_t, mailbox_room> slots{};
    bool is_closed = false;

    // Puts the word at the end of the queue.
    void push(std::uint64_t word) noexcept {
        static_cast<void>(slots.append(word).expect("a mailbox of an attack holds at most mailbox_room words"));
    }

    // Takes the first word out of the queue, or gives no value for an empty
    // queue.  Complexity: linear in the words of the queue.
    [[nodiscard]] ::fixy::Option<std::uint64_t> take_first() noexcept { return slots.remove(::fixy::index<0>()); }
};

struct Port {
    Mailbox* in = nullptr;
    Mailbox* out = nullptr;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};

// The one root site of X in the attacks.  One site gives one brand, so the
// protocols of the attacks name the brand of each token that a read packs,
// and each root of the site is a token of that brand.
[[nodiscard]] inline auto mint_x_root() noexcept { return fp::mint_permission_root<X>(); }
using XBrand = ::foundation::brand::brand_of_t<decltype(mint_x_root())>;

using Token = s::Transferable<int, X, XBrand>;

// The cells of the two pairs of p and q.  The cell of p watches p for q:
// the crash of p and the count of the messages of p to q.
using CellOfP = s::CrashCell<P, Q>;
using CellOfQ = s::CrashCell<Q, P>;

// The failure detector reports the crash of the sender of the pair that
// `cell` watches.  The report carries the count that the session of the
// sender wrote into the cell, or zero when no session counted there.
void report_crash(s::CrashCellCore& cell, s::CrashCause cause);

// The crash of the endpoint whose inbound queue is `inbox`: the detector
// reports it, and the queue closes.
void crash_endpoint(s::CrashCellCore& cell, Mailbox& inbox, s::CrashCause cause);

// True when the cell holds a report, and the report counts `count`
// messages of the sender of the pair that the cell watches.
[[nodiscard]] bool report_counts(const s::CrashCellCore& cell, std::uint64_t count);

// True when a send gave its token back, and the token holds `payload`.
// The token ends here, through the drop door of foundation/Linear.h.  A fresh
// brand is a closure type with no linkage, so each unit has its own Token,
// and the function is inline in each unit.
[[nodiscard]] inline bool take_token_back(::fixy::Option<Token> undelivered, int payload) {
    return std::move(undelivered)
        .match(
            [payload](Token token) noexcept {
                const bool is_same_payload = token.peek() == payload;
                ::foundation::linear::drop(std::move(token));
                return is_same_payload;
            },
            []() noexcept { return false; });
}

// p as a real crash session: it sends `count` messages, each the label 0
// and the value first, first + 1, and so on, into `out`, and its cell
// counts each one.  The session then stops with no crash of its own, so a
// report of the detector is the crash, and the report carries this count.
void p_sends(CellOfP& cell_p, const CellOfQ& cell_q, Mailbox& in, Mailbox& out, int first, int count);

// Ends the child process of an attack with the exit of its outcome.
[[noreturn]] void finish(Outcome outcome);

// A crash-watched send tries its write.  The queue has room for each word
// of an attack, so a try fails only when the queue of the peer is closed,
// and then the value stays with the caller.
[[nodiscard]] inline bool push_word(Mailbox& box, std::uint64_t word) noexcept {
    if (box.is_closed) return false;
    box.push(word);
    return true;
}
inline constexpr auto push_label = [](Port& port, std::size_t label) noexcept { return push_word(*port.out, label); };
inline constexpr auto push_int = [](Port& port, int& value) noexcept {
    return push_word(*port.out, static_cast<std::uint64_t>(value));
};
// A token is linear, so its write takes it by move and gives it back while
// the queue of the peer is closed.  The queue keeps the value of the token,
// and the write ends the token through the drop door.
inline constexpr auto push_token = [](Port& port, Token&& token) noexcept -> ::fixy::Option<Token> {
    if (port.out->is_closed) return ::fixy::Option<Token>::some(std::move(token));
    port.out->push(static_cast<std::uint64_t>(token.peek()));
    ::foundation::linear::drop(std::move(token));
    return ::fixy::none;
};
// A crash-watched reception reads with no wait: the payload when one is
// queued, and no value otherwise.
inline constexpr auto read_int = [](Port& port) noexcept -> ::fixy::Option<int> {
    return port.in->take_first().match(
        [](std::uint64_t const slot) noexcept { return ::fixy::Option<int>::some(static_cast<int>(slot)); },
        []() noexcept { return ::fixy::Option<int>{::fixy::none}; });
};
// The read returns the token in one place, so the token has one route out of
// the read.
inline constexpr auto read_token = [](Port& port) noexcept -> ::fixy::Option<Token> {
    ::fixy::Option<std::uint64_t> slot = port.in->take_first();
    if (slot.is_none()) return ::fixy::none;
    // Only the door of a hold builds a marker: the wire carries the value,
    // and the read packs it with a token of its own.
    auto [message, rest] =
        s::mint_permission_hold(mint_x_root())
            .template pack<X>(static_cast<int>(std::move(slot).expect("the test above found a word")));
    static_cast<void>(std::move(rest).into_permissions());
    return ::fixy::some(std::move(message));
};
inline constexpr auto poll_label = [](Port& port) noexcept -> ::fixy::Option<std::size_t> {
    return port.in->take_first().match(
        [](std::uint64_t const slot) noexcept {
            return ::fixy::Option<std::size_t>::some(static_cast<std::size_t>(slot));
        },
        []() noexcept { return ::fixy::Option<std::size_t>{::fixy::none}; });
};

// Runs the endpoint of a loop of one message and one crash branch to its
// end.  At each Offer, `poll` picks the branch.  `on_message` receives a
// message branch and gives the handle of the next turn of the loop, and
// `on_crash` receives the crash branch, which ends the loop.  Each turn
// is one call, so no handle waits in an optional between two turns.
// Complexity: one call for each message that the endpoint takes.
template <typename Loop, typename Poll, typename OnMessage, typename OnCrash>
void run_loop_to_crash(Loop handle, const Poll& poll, const OnMessage& on_message, const OnCrash& on_crash) {
    std::move(handle).branch(poll, [&](auto branch) noexcept {
        using Head = typename decltype(branch)::protocol;
        if constexpr (s::is_crash_branch_v<Head>) {
            on_crash(std::move(branch));
        } else {
            run_loop_to_crash(on_message(std::move(branch)), poll, on_message, on_crash);
        }
    });
}

// ── The runtime attacks, by subject ─────────────────────────────────

// test_session_crash_attack_detection.cpp: the detection of a crash at a
// reception and at a send.
[[noreturn]] void reliable_peer_crashes_while_waited_on();
[[noreturn]] void send_to_crashed_peer_returns_payload();
[[noreturn]] void token_relayed_to_crashed_peer();
[[noreturn]] void crash_detected_twice();
[[noreturn]] void queue_drains_before_detection();
[[noreturn]] void peer_sends_the_crash_label();
[[noreturn]] void reliable_message_does_not_drain_the_peer();
[[noreturn]] void reliable_offer_while_the_peer_is_crashed();

// test_session_crash_attack_fabrication.cpp: a transport that reports
// messages that the crashed peer never sent.
[[noreturn]] void transport_fabricates_messages_after_crash();
[[noreturn]] void transport_fabricates_after_queued_messages();
[[noreturn]] void crash_branch_waits_for_an_owed_message();
[[noreturn]] void endpoint_reports_its_own_count();
[[noreturn]] void bare_recv_refuses_a_fabricated_message();

// test_session_crash_attack_cells.cpp: the reporter and the writer of a
// crash cell, and the order of a crash against a write.
[[noreturn]] void second_reporter_for_one_cell();
[[noreturn]] void second_writer_for_one_cell();
[[noreturn]] void mint_takes_a_moved_from_writer();
[[noreturn]] void false_suspicion_fences_the_sender();
[[noreturn]] void crash_between_check_and_write_loses_token();
[[noreturn]] void bare_recv_from_crashed_reliable_peer();
[[noreturn]] void peer_dies_between_label_and_payload();
[[noreturn]] void false_report_between_label_and_payload();
[[noreturn]] void reported_sender_stops_at_the_payload();
[[noreturn]] void reported_sender_stops_at_the_keyed_value();
[[noreturn]] void keyed_word_to_crashed_peer_comes_back();
[[noreturn]] void keyed_messages_to_a_third_role_count_in_its_pair();
[[noreturn]] void keyed_messages_to_a_third_role_survive_the_peer_crash();
[[noreturn]] void reported_sender_stops_at_a_message_to_the_third_role();
[[noreturn]] void peer_reads_label_before_the_report();

// test_session_crash_attack.cpp: the event log.
[[noreturn]] void recorder_sees_the_crash();
[[noreturn]] void corrupt_event_bytes();

struct attack_case {
    std::string_view name;
    Outcome expected;
    void (*run)();
};

inline constexpr attack_case kAttacks[] = {
    {"reliable_peer_crashes_while_waited_on", Outcome::Caught, reliable_peer_crashes_while_waited_on},
    {"send_to_crashed_peer_returns_payload", Outcome::Correct, send_to_crashed_peer_returns_payload},
    {"token_relayed_to_crashed_peer", Outcome::Correct, token_relayed_to_crashed_peer},
    {"crash_detected_twice", Outcome::Correct, crash_detected_twice},
    {"queue_drains_before_detection", Outcome::Correct, queue_drains_before_detection},
    {"peer_sends_the_crash_label", Outcome::Caught, peer_sends_the_crash_label},
    {"reliable_message_does_not_drain_the_peer", Outcome::Correct, reliable_message_does_not_drain_the_peer},
    {"reliable_offer_while_the_peer_is_crashed", Outcome::Correct, reliable_offer_while_the_peer_is_crashed},
    {"transport_fabricates_messages_after_crash", Outcome::Correct, transport_fabricates_messages_after_crash},
    {"transport_fabricates_after_queued_messages", Outcome::Correct, transport_fabricates_after_queued_messages},
    {"crash_branch_waits_for_an_owed_message", Outcome::Correct, crash_branch_waits_for_an_owed_message},
    {"endpoint_reports_its_own_count", Outcome::Correct, endpoint_reports_its_own_count},
    {"bare_recv_refuses_a_fabricated_message", Outcome::Caught, bare_recv_refuses_a_fabricated_message},
    {"second_reporter_for_one_cell", Outcome::Caught, second_reporter_for_one_cell},
    {"second_writer_for_one_cell", Outcome::Caught, second_writer_for_one_cell},
    {"mint_takes_a_moved_from_writer", Outcome::Caught, mint_takes_a_moved_from_writer},
    {"false_suspicion_fences_the_sender", Outcome::Caught, false_suspicion_fences_the_sender},
    {"crash_between_check_and_write_loses_token", Outcome::Correct, crash_between_check_and_write_loses_token},
    {"bare_recv_from_crashed_reliable_peer", Outcome::Caught, bare_recv_from_crashed_reliable_peer},
    {"peer_dies_between_label_and_payload", Outcome::Correct, peer_dies_between_label_and_payload},
    {"false_report_between_label_and_payload", Outcome::Correct, false_report_between_label_and_payload},
    {"reported_sender_stops_at_the_payload", Outcome::Caught, reported_sender_stops_at_the_payload},
    {"reported_sender_stops_at_the_keyed_value", Outcome::Caught, reported_sender_stops_at_the_keyed_value},
    {"keyed_word_to_crashed_peer_comes_back", Outcome::Correct, keyed_word_to_crashed_peer_comes_back},
    {"keyed_messages_to_a_third_role_count_in_its_pair", Outcome::Correct,
     keyed_messages_to_a_third_role_count_in_its_pair},
    {"keyed_messages_to_a_third_role_survive_the_peer_crash", Outcome::Correct,
     keyed_messages_to_a_third_role_survive_the_peer_crash},
    {"reported_sender_stops_at_a_message_to_the_third_role", Outcome::Caught,
     reported_sender_stops_at_a_message_to_the_third_role},
    {"peer_reads_label_before_the_report", Outcome::Caught, peer_reads_label_before_the_report},
    {"recorder_sees_the_crash", Outcome::Correct, recorder_sees_the_crash},
    {"corrupt_event_bytes", Outcome::Correct, corrupt_event_bytes},
};

}  // namespace test_session_crash_attack
