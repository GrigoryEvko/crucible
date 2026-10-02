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
#include <fixy/session/Recording.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <cstdint>
#include <deque>
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

// The queue into one endpoint.  A crash of that endpoint closes it, and a
// write into a closed queue is refused, which is the check a transport
// makes at the write.
struct Mailbox {
    std::deque<std::uint64_t> slots;
    bool is_closed = false;
};

struct Port {
    Mailbox* in = nullptr;
    Mailbox* out = nullptr;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};

using Token = s::Transferable<int, X>;

// The failure detector reports the crash of the endpoint that `cell`
// watches.  The report carries the count that the session of that
// endpoint wrote into the cell, or zero when no session counted there.
void report_crash(s::PeerCrashCell& cell, s::CrashCause cause);

// The crash of the endpoint whose inbound queue is `inbox`: the detector
// reports it, and the queue closes.
void crash_endpoint(s::PeerCrashCell& cell, Mailbox& inbox, s::CrashCause cause);

// p as a real crash session: it sends `count` messages, each the label 0
// and the value first, first + 1, and so on, into `out`, and its cell
// counts each one.  The session then stops with no crash of its own, so a
// report of the detector is the crash, and the report carries this count.
void p_sends(s::PeerCrashCell& cell_p, const s::PeerCrashCell& cell_q, Mailbox& in, Mailbox& out, int first, int count);

// Ends the child process of an attack with the exit of its outcome.
[[noreturn]] void finish(Outcome outcome);

// A crash-watched send tries its write.  The queue has no bound, so a try
// fails only when the queue of the peer is closed, and then the value
// stays with the caller.
inline constexpr auto push_label = [](Port& port, std::size_t label) noexcept {
    if (port.out->is_closed) return false;
    port.out->slots.push_back(label);
    return true;
};
inline constexpr auto push_int = [](Port& port, int& value) noexcept {
    if (port.out->is_closed) return false;
    port.out->slots.push_back(static_cast<std::uint64_t>(value));
    return true;
};
inline constexpr auto push_token = [](Port& port, Token& token) noexcept {
    if (port.out->is_closed) return false;
    port.out->slots.push_back(static_cast<std::uint64_t>(token.value));
    return true;
};
// A crash-watched reception reads with no wait: the payload when one is
// queued, and no value otherwise.
inline constexpr auto read_int = [](Port& port) noexcept -> std::optional<int> {
    if (port.in->slots.empty()) return std::nullopt;
    const std::uint64_t slot = port.in->slots.front();
    port.in->slots.pop_front();
    return static_cast<int>(slot);
};
inline constexpr auto read_token = [](Port& port) noexcept -> std::optional<Token> {
    if (port.in->slots.empty()) return std::nullopt;
    const std::uint64_t slot = port.in->slots.front();
    port.in->slots.pop_front();
    return Token{static_cast<int>(slot), fp::mint_permission_root<X>()};
};
inline constexpr auto poll_label = [](Port& port) noexcept -> std::optional<std::size_t> {
    if (port.in->slots.empty()) return std::nullopt;
    const std::uint64_t slot = port.in->slots.front();
    port.in->slots.pop_front();
    return static_cast<std::size_t>(slot);
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
    {"peer_dies_between_label_and_payload", Outcome::Caught, peer_dies_between_label_and_payload},
    {"recorder_sees_the_crash", Outcome::Correct, recorder_sees_the_crash},
    {"corrupt_event_bytes", Outcome::Correct, corrupt_event_bytes},
};

}  // namespace test_session_crash_attack
