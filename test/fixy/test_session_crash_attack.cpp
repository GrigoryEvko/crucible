// Attacks on crash-stop sessions, checkpoint sessions and the event log
// that use the discipline correctly.
//
// Each attack is legal code through the public API: no cast that the
// build bans, no undefined behaviour, no namespace reopened, no friend
// door.  An attack that the type system refuses is a static assertion
// on the admission predicate here, and a negative fixture where the
// refusal is a diagnostic.  An attack that compiles runs in a child
// process under a watchdog, so a hang ends as a deadlock verdict and
// never hangs the test.  An attack that compiles and misbehaves is
// either fixed, or it has a row on the ledger below.  The ledger names
// the attack and the condition of the paper that it breaks, and it can
// only shrink: a row whose attack no longer succeeds fails the build.
//
// The paper is Barwell, Hou, Yoshida and Zhou, "Crash-Stop Failures in
// Asynchronous Multiparty Session Types" (LMCS 21:2, 2025), for crash;
// Mezzina, Tiezzi and Yoshida (LMCS 2025) for checkpoints.

#include <fixy/session/Checkpoint.h>
#include <fixy/session/CrashTransport.h>
#include <fixy/session/EventLog.h>
#include <fixy/session/Recording.h>

#include <foundation/permissions/Permission.h>

#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace s = ::fixy::session;
namespace fp = ::foundation::permissions;

namespace {

struct P {};
struct Q {};
struct X {
    using permission_row = ::foundation::effects::Row<>;
};

// ── Refused at compile time ─────────────────────────────────────────

// A delegated endpoint has peers that no detector of this session
// watches.  The coverage walk does not look inside a payload, so before
// the delegation rule this protocol was admitted, and a crash of the
// delegated peer left the holder waiting.
using Delegated = s::DelegatedSession<s::Recv<int, s::End>, fp::EmptyPermSet>;
using ReceivesDelegation = s::Offer<s::Recv<Delegated, s::End>, s::Recv<s::Crash<P>, s::End>>;
static_assert(!s::CrashSessionAdmissible<ReceivesDelegation, Q, P, s::NoReliableRoles>);
struct HidesDelegation {
    int tag = 0;
    Delegated inner;
};
using ReceivesHiddenDelegation = s::Offer<s::Recv<HidesDelegation, s::End>, s::Recv<s::Crash<P>, s::End>>;
static_assert(!s::CrashSessionAdmissible<ReceivesHiddenDelegation, Q, P, s::NoReliableRoles>);
static_assert(s::detail::crash::carries_delegation_v<HidesDelegation>);
static_assert(!s::detail::crash::carries_delegation_v<int>);

// A rollback cannot recall a delegated endpoint.
using DelegatesThenCommits = s::Select<s::Send<Delegated, s::Select<s::Commit<s::End>, s::Abort>>>;
static_assert(s::checkpoint_verdict_v<DelegatesThenCommits, s::dual_of_t<DelegatesThenCommits>>
              == s::CheckpointVerdict::NotCheckpointShaped);

// A crash branch inside a checkpoint session: the two disciplines do
// not compose, and the checkpoint mint refuses the mixture.
using CrashInsideCheckpoint =
    s::Offer<s::Recv<int, s::Offer<s::Commit<s::End>, s::Abort>>, s::Recv<s::Crash<P>, s::End>>;
static_assert(s::checkpoint_verdict_v<CrashInsideCheckpoint, s::Select<s::Send<int, s::Select<s::Commit<s::End>, s::Abort>>>>
              != s::CheckpointVerdict::Compliant);

// A crash session cannot start with a token: its mint has an empty set,
// so the only token in flight is one it received.
using SendsToken = s::Select<s::Send<s::Transferable<int, X>, s::End>>;
static_assert(!s::CrashSessionAdmissible<SendsToken, Q, P, s::NoReliableRoles>);

// A reception hidden in a loop, one step after the guarded choice.
using HiddenInLoop =
    s::Loop<s::Offer<s::Recv<int, s::Recv<int, s::Continue>>, s::Recv<s::Crash<P>, s::End>>>;
static_assert(!s::CrashSessionAdmissible<HiddenInLoop, Q, P, s::NoReliableRoles>);

// A crash branch for a peer that the endpoint counts reliable.
using DeadBranch = s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<P>, s::End>>;
static_assert(!s::CrashSessionAdmissible<DeadBranch, Q, P, s::ReliableSet<P>>);

// The crash label as a message of the registry: no endpoint sends it,
// and an Offer of crash branches only is empty.
static_assert(!s::is_well_formed_v<s::Select<s::Send<s::Crash<P>, s::End>>>);
static_assert(s::is_empty_choice_v<s::Offer<s::Recv<s::Crash<P>, s::End>>>);
// Stop absorbs a suffix, so composition cannot resume a crashed endpoint.
static_assert(std::is_same_v<s::compose_t<s::Send<int, s::Stop>, s::Recv<int, s::End>>, s::Send<int, s::Stop>>);
static_assert(std::is_same_v<s::dual_of_t<s::Stop>, s::Stop>);

// The recorder is the outer decorator.  The crash transport is built
// only by mint_crash_session, from a Resource, so nothing puts it around
// a recorded handle.  A recorded handle is built only by its mint.
using Plain = s::SessionHandle<s::End, int*>;
static_assert(!std::is_constructible_v<s::Recorded<Plain>, Plain, s::SessionEventLog&, s::RoleTagId, s::RoleTagId>);

// ── The runtime campaign ────────────────────────────────────────────

enum class Outcome : std::uint8_t {
    Correct,
    Caught,
    Silent,
    Deadlock,
};

constexpr int kCorrectExit = 0;
constexpr int kSilentExit = 3;
constexpr int kDeadlockExit = 4;
constexpr unsigned kWatchdogSeconds = 5;

struct Mailbox {
    std::deque<std::uint64_t> slots;
};

struct Port {
    Mailbox* in = nullptr;
    Mailbox* out = nullptr;
};

constexpr auto push_label = [](Port& port, std::size_t label) noexcept { port.out->slots.push_back(label); };
constexpr auto push_int = [](Port& port, int&& value) noexcept { port.out->slots.push_back(static_cast<std::uint64_t>(value)); };
constexpr auto pop_int = [](Port& port) noexcept {
    const std::uint64_t slot = port.in->slots.front();
    port.in->slots.pop_front();
    return static_cast<int>(slot);
};
constexpr auto poll_label = [](Port& port) noexcept -> std::optional<std::size_t> {
    if (port.in->slots.empty()) return std::nullopt;
    const std::uint64_t slot = port.in->slots.front();
    port.in->slots.pop_front();
    return static_cast<std::size_t>(slot);
};

[[noreturn]] void finish(Outcome outcome) {
    std::fflush(stderr);
    switch (outcome) {
        case Outcome::Correct: std::_Exit(kCorrectExit);
        case Outcome::Silent: std::_Exit(kSilentExit);
        case Outcome::Deadlock: std::_Exit(kDeadlockExit);
        case Outcome::Caught:
        default: std::abort();
    }
}

// Each endpoint names its own reliable set.  q counts p reliable, so its
// Offer has no crash branch, and p, whose own set is empty, crashes.
// Before the fix q waited for ever.  The crash transport now aborts with
// Crash_Of_Reliable_Peer.
[[noreturn]] void reliable_peer_crashes_while_waited_on() {
    using ProtoP = s::Select<s::Send<int, s::End>>;
    using ProtoQ = s::Offer<s::Recv<int, s::End>>;
    Mailbox to_p;
    Mailbox to_q;
    s::PeerCrashCell cell_p;
    s::PeerCrashCell cell_q;
    auto p = s::mint_crash_session<ProtoP, P, Q>(Port{&to_p, &to_q}, cell_q);
    auto q = s::mint_crash_session<ProtoQ, Q, P, s::ReliableSet<P>>(Port{&to_q, &to_p}, cell_p);
    (void)std::move(p).crash(s::CrashCause::Abort, cell_p);
    std::move(q).branch(poll_label, [](auto branch) noexcept {
        auto [value, end] = std::move(branch).recv(pop_int);
        (void)value;
        (void)std::move(end).close();
    });
    finish(Outcome::Silent);
}

// A send to a crashed peer gives the payload back and writes nothing.
[[noreturn]] void send_to_crashed_peer_returns_payload() {
    using ProtoQ = s::Loop<s::Select<s::Send<int, s::Continue>>>;
    Mailbox to_p;
    Mailbox to_q;
    s::PeerCrashCell cell_p;
    cell_p.mark_crashed(s::CrashCause::Throw);
    auto q = s::mint_crash_session<ProtoQ, Q, P>(Port{&to_q, &to_p}, cell_p);
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
[[noreturn]] void token_relayed_to_crashed_peer() {
    using Relay = s::Offer<s::Recv<s::Transferable<int, X>, s::Select<s::Send<s::Transferable<int, X>, s::End>>>,
                           s::Recv<s::Crash<P>, s::End>>;
    Mailbox to_p;
    Mailbox to_q;
    to_q.slots.push_back(0);  // the label of the message branch
    to_q.slots.push_back(7);  // the value of the token's payload
    s::PeerCrashCell cell_p;
    auto q = s::mint_crash_session<Relay, Q, P>(Port{&to_q, &to_p}, cell_p);
    bool came_back_once = false;
    std::move(q).branch(poll_label, [&](auto branch) noexcept {
        using Head = typename decltype(branch)::protocol;
        if constexpr (s::is_crash_branch_v<Head>) {
            finish(Outcome::Silent);
        } else {
            auto [token, reply] = std::move(branch).recv([](Port& port) noexcept {
                port.in->slots.pop_front();
                return s::Transferable<int, X>{7, fp::mint_permission_root<X>()};
            });
            cell_p.mark_crashed(s::CrashCause::Abort);
            auto chosen = std::move(reply).template select<0>(push_label);
            auto [end, undelivered] =
                std::move(chosen).send(std::move(token), [](Port& port, s::Transferable<int, X>&& moved) noexcept {
                    port.out->slots.push_back(static_cast<std::uint64_t>(moved.value));
                });
            came_back_once = undelivered.has_value() && undelivered->value == 7;
            (void)std::move(end).close();
        }
    });
    finish(came_back_once && to_p.slots.empty() ? Outcome::Correct : Outcome::Silent);
}

// q detects the crash of p, and its crash branch receives from p again.
// The second reception detects the crash again, as rule r-rcv-⊙ does.
[[noreturn]] void crash_detected_twice() {
    using Again = s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<P>, s::End>>;
    using ProtoQ = s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<P>, Again>>;
    Mailbox to_p;
    Mailbox to_q;
    s::PeerCrashCell cell_p;
    cell_p.mark_crashed(s::CrashCause::ErrorReturn);
    auto q = s::mint_crash_session<ProtoQ, Q, P>(Port{&to_q, &to_p}, cell_p);
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
[[noreturn]] void queue_drains_before_detection() {
    using ProtoQ = s::Loop<s::Offer<s::Recv<int, s::Continue>, s::Recv<s::Crash<P>, s::End>>>;
    Mailbox to_p;
    Mailbox to_q;
    for (std::uint64_t value = 1; value <= 4; ++value) {
        to_q.slots.push_back(0);
        to_q.slots.push_back(value);
    }
    s::PeerCrashCell cell_p;
    cell_p.mark_crashed(s::CrashCause::Abort);
    auto q = s::mint_crash_session<ProtoQ, Q, P>(Port{&to_q, &to_p}, cell_p);
    using Loop = decltype(q);
    std::optional<Loop> current{std::move(q)};
    int received = 0;
    bool detected = false;
    while (current) {
        Loop handle = std::move(*current);
        current.reset();
        std::move(handle).branch(poll_label, [&](auto branch) noexcept {
            using Head = typename decltype(branch)::protocol;
            if constexpr (s::is_crash_branch_v<Head>) {
                auto [record, end] = std::move(branch).recv();
                (void)record;
                detected = true;
                (void)std::move(end).close();
            } else {
                auto [value, next] = std::move(branch).recv(pop_int);
                if (value == received + 1) ++received;
                current.emplace(std::move(next));
            }
        });
    }
    finish(received == 4 && detected ? Outcome::Correct : Outcome::Silent);
}

// A label at or after the first crash branch is the crash
// pseudo-message, which no peer can send.
[[noreturn]] void peer_sends_the_crash_label() {
    using ProtoQ = s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<P>, s::End>>;
    Mailbox to_p;
    Mailbox to_q;
    to_q.slots.push_back(1);
    s::PeerCrashCell cell_p;
    auto q = s::mint_crash_session<ProtoQ, Q, P>(Port{&to_q, &to_p}, cell_p);
    std::move(q).branch(poll_label, [](auto branch) noexcept { std::move(branch).detach(s::detach_reason::TestInstrumentation{}); });
    finish(Outcome::Silent);
}

// A transport that fabricates messages after the crash.  The handle
// trusts the transport, so the crash is never detected.
[[noreturn]] void transport_fabricates_messages_after_crash() {
    using ProtoQ = s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<P>, s::End>>;
    Mailbox to_p;
    Mailbox to_q;
    s::PeerCrashCell cell_p;
    cell_p.mark_crashed(s::CrashCause::Abort);
    auto q = s::mint_crash_session<ProtoQ, Q, P>(Port{&to_q, &to_p}, cell_p);
    bool took_message = false;
    std::move(q).branch([](Port&) noexcept -> std::optional<std::size_t> { return std::size_t{0}; },
                        [&](auto branch) noexcept {
                            using Head = typename decltype(branch)::protocol;
                            if constexpr (s::is_crash_branch_v<Head>) {
                                auto [record, end] = std::move(branch).recv();
                                (void)record;
                                (void)std::move(end).close();
                            } else {
                                auto [value, end] = std::move(branch).recv([](Port&) noexcept { return 99; });
                                took_message = value == 99;
                                (void)std::move(end).close();
                            }
                        });
    finish(took_message ? Outcome::Silent : Outcome::Correct);
}

// The check of the crash cell and the write of the transport are two
// steps.  The peer crashes between them: the check sees it alive, and the
// transport writes into a queue that nobody reads.  The token inside the
// message is lost, and the undelivered payload is empty.
[[noreturn]] void crash_between_check_and_write_loses_token() {
    using Relay = s::Offer<s::Recv<s::Transferable<int, X>, s::Select<s::Send<s::Transferable<int, X>, s::End>>>,
                           s::Recv<s::Crash<P>, s::End>>;
    Mailbox to_p;
    Mailbox to_q;
    to_q.slots.push_back(0);
    to_q.slots.push_back(7);
    s::PeerCrashCell cell_p;
    auto q = s::mint_crash_session<Relay, Q, P>(Port{&to_q, &to_p}, cell_p);
    bool token_lost = false;
    std::move(q).branch(poll_label, [&](auto branch) noexcept {
        using Head = typename decltype(branch)::protocol;
        if constexpr (s::is_crash_branch_v<Head>) {
            finish(Outcome::Correct);
        } else {
            auto [token, reply] = std::move(branch).recv([](Port& port) noexcept {
                port.in->slots.pop_front();
                return s::Transferable<int, X>{7, fp::mint_permission_root<X>()};
            });
            auto chosen = std::move(reply).template select<0>(push_label);
            auto [end, undelivered] = std::move(chosen).send(
                std::move(token), [&cell_p](Port& port, s::Transferable<int, X>&& moved) noexcept {
                    cell_p.mark_crashed(s::CrashCause::Abort);
                    port.out->slots.push_back(static_cast<std::uint64_t>(moved.value));
                });
            token_lost = !undelivered.has_value() && cell_p.has_crashed();
            (void)std::move(end).close();
        }
    });
    finish(token_lost ? Outcome::Silent : Outcome::Correct);
}

// A bare reception from a peer counted reliable, which crashes.  The
// transport has no message to give, and the handle cannot tell a slow
// peer from a dead one without a poll.
[[noreturn]] void bare_recv_from_crashed_reliable_peer() {
    using ProtoQ = s::Recv<int, s::End>;
    Mailbox to_p;
    Mailbox to_q;
    s::PeerCrashCell cell_p;
    cell_p.mark_crashed(s::CrashCause::Abort);
    auto q = s::mint_crash_session<ProtoQ, Q, P, s::ReliableSet<P>>(Port{&to_q, &to_p}, cell_p);
    auto [value, end] = std::move(q).recv([](Port& port) noexcept {
        while (port.in->slots.empty()) {
            ::pause();
        }
        return static_cast<int>(port.in->slots.front());
    });
    (void)value;
    (void)std::move(end).close();
    finish(Outcome::Correct);
}

// The recorder outside the crash transport records a send to a crashed
// peer as lost, and the crash branch as a stop with its cause.
[[noreturn]] void recorder_sees_the_crash() {
    using ProtoQ = s::Select<s::Send<int, s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<P>, s::End>>>>;
    Mailbox to_p;
    Mailbox to_q;
    s::PeerCrashCell cell_p;
    cell_p.mark_crashed(s::CrashCause::Throw);
    s::SessionEventLog log{s::SessionTagId{9}};
    auto q = s::mint_recorded_session(s::mint_crash_session<ProtoQ, Q, P>(Port{&to_q, &to_p}, cell_p), log,
                                      s::RoleTagId{2}, s::RoleTagId{1});
    auto chosen = std::move(q).select<0>(push_label);
    auto [waiting, undelivered] = std::move(chosen).send(5, push_int);
    std::move(waiting).branch(poll_label, [](auto branch) noexcept {
        using Head = typename decltype(branch)::protocol;
        if constexpr (s::is_crash_branch_v<Head>) {
            auto [record, end] = std::move(branch).recv();
            (void)record;
            (void)std::move(end).close();
        } else {
            finish(Outcome::Silent);
        }
    });
    bool lost_recorded = false;
    bool stop_recorded = false;
    for (const s::SessionEvent& event : log) {
        if (event.op() == s::SessionOp::Send && event.delivery_fate() == s::DeliveryFate::LostToCrashedPeer)
            lost_recorded = true;
        if (event.op() == s::SessionOp::Stop && event.crash_cause() == s::CrashCause::Throw) stop_recorded = true;
    }
    finish(undelivered && lost_recorded && stop_recorded ? Outcome::Correct : Outcome::Silent);
}

// Every one-byte corruption of a valid record is refused, or decodes to
// an event whose bytes are the corrupted bytes.  A decode never repairs a
// record in silence.  Every short read is refused.
[[noreturn]] void corrupt_event_bytes() {
    const s::SessionEvent original = s::SessionEvent::stop(s::RoleTagId{1}, s::RoleTagId{2}, s::RoleTagId{2},
                                                           s::StopReasonKind::PeerCrashed, s::CrashCause::Throw);
    const auto bytes = original.encode();
    std::size_t refused = 0;
    std::size_t accepted = 0;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        for (unsigned value = 0; value < 256; ++value) {
            auto corrupt = bytes;
            corrupt[index] = static_cast<std::byte>(value);
            const auto decoded = s::decode_session_event(corrupt);
            if (!decoded) {
                ++refused;
                continue;
            }
            if (decoded->encode() != corrupt) finish(Outcome::Silent);
            ++accepted;
        }
    }
    for (std::size_t length = 0; length < bytes.size(); ++length) {
        const auto decoded = s::decode_session_event(std::span<const std::byte>{bytes.data(), length});
        if (decoded || decoded.error() != s::EventDecodeError::Truncated) finish(Outcome::Silent);
    }
    std::array<std::byte, 2 * s::session_event_size - 1> torn{};
    for (std::size_t index = 0; index < bytes.size(); ++index) torn[index] = bytes[index];
    const auto torn_log = s::decode_session_log(torn);
    if (torn_log || torn_log.error().error != s::EventDecodeError::Truncated) finish(Outcome::Silent);
    finish(refused > 0 && accepted > 0 ? Outcome::Correct : Outcome::Silent);
}

struct attack_case {
    std::string_view name;
    Outcome expected;
    void (*run)();
};

constexpr attack_case kAttacks[] = {
    {"reliable_peer_crashes_while_waited_on", Outcome::Caught, reliable_peer_crashes_while_waited_on},
    {"send_to_crashed_peer_returns_payload", Outcome::Correct, send_to_crashed_peer_returns_payload},
    {"token_relayed_to_crashed_peer", Outcome::Correct, token_relayed_to_crashed_peer},
    {"crash_detected_twice", Outcome::Correct, crash_detected_twice},
    {"queue_drains_before_detection", Outcome::Correct, queue_drains_before_detection},
    {"peer_sends_the_crash_label", Outcome::Caught, peer_sends_the_crash_label},
    {"transport_fabricates_messages_after_crash", Outcome::Silent, transport_fabricates_messages_after_crash},
    {"crash_between_check_and_write_loses_token", Outcome::Silent, crash_between_check_and_write_loses_token},
    {"bare_recv_from_crashed_reliable_peer", Outcome::Deadlock, bare_recv_from_crashed_reliable_peer},
    {"recorder_sees_the_crash", Outcome::Correct, recorder_sees_the_crash},
    {"corrupt_event_bytes", Outcome::Correct, corrupt_event_bytes},
};

// ── The ledger ──────────────────────────────────────────────────────
//
// Each row is an attack that succeeds, and the condition it breaks.

struct limitation_row {
    std::string_view attack;
    std::string_view breaks;
};

constexpr limitation_row known_limitations[] = {
    {"transport_fabricates_messages_after_crash",
     "rule r-rcv-⊙ (LMCS 2025, Fig. 4): the handle trusts the transport to report a queued message, so a "
     "transport that invents messages hides the crash for ever"},
    {"crash_between_check_and_write_loses_token",
     "a linear token is not lost: rule r-send-↯ drops a message to a crashed peer, and a crash between the "
     "check and the write drops the token inside it.  A transport that returns a refused payload would close it"},
    {"bare_recv_from_crashed_reliable_peer",
     "the reliable set is an assumption of the theory (LMCS 2025, p. 11): a reception from a reliable peer "
     "has no crash branch, so a crash of that peer blocks it, and without a poll the handle cannot detect it"},
};

consteval bool ledger_matches_the_campaign() {
    for (const attack_case& attack : kAttacks) {
        const bool succeeds = attack.expected == Outcome::Silent || attack.expected == Outcome::Deadlock;
        bool listed = false;
        for (const limitation_row& row : known_limitations) {
            if (row.attack == attack.name) listed = true;
        }
        if (succeeds != listed) return false;
    }
    for (const limitation_row& row : known_limitations) {
        bool found = false;
        for (const attack_case& attack : kAttacks) {
            if (attack.name == row.attack) found = true;
        }
        if (!found || row.breaks.empty()) return false;
    }
    return true;
}
static_assert(ledger_matches_the_campaign(),
              "the ledger and the campaign disagree: a successful attack has no row, or a row names no successful "
              "attack");

void on_watchdog(int) { std::_Exit(kDeadlockExit); }

[[nodiscard]] Outcome run_in_child(void (*attack)()) {
    std::fflush(stderr);
    // SPAWN-PROCESS-OK: an attack that the transport catches ends the
    // process, and a hang must end too, so each attack runs in a child.
    const pid_t pid = ::fork();  // SPAWN-PROCESS-OK: attack harness, see above
    if (pid < 0) {
        std::fprintf(stderr, "fork failed\n");
        std::_Exit(2);
    }
    if (pid == 0) {
        ::signal(SIGALRM, on_watchdog);
        ::alarm(kWatchdogSeconds);
        attack();
    }
    int status = 0;
    if (::waitpid(pid, &status, 0) != pid) {  // SPAWN-PROCESS-OK: attack harness, see above
        std::fprintf(stderr, "waitpid failed\n");
        std::_Exit(2);
    }
    if (WIFSIGNALED(status) != 0) return Outcome::Caught;
    const int code = WEXITSTATUS(status);
    if (code == kDeadlockExit) return Outcome::Deadlock;
    if (code == kCorrectExit) return Outcome::Correct;
    if (code == kSilentExit) return Outcome::Silent;
    std::fprintf(stderr, "an attack exited with code %d, which names no outcome\n", code);
    std::_Exit(2);
}

[[nodiscard]] const char* outcome_name(Outcome outcome) noexcept {
    switch (outcome) {
        case Outcome::Caught: return "caught";
        case Outcome::Silent: return "silent";
        case Outcome::Deadlock: return "deadlock";
        case Outcome::Correct: return "correct";
        default: return "unknown";
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "[expected] the attack campaign below prints diagnostics from child processes\n");
    int failures = 0;
    for (const attack_case& attack : kAttacks) {
        const Outcome seen = run_in_child(attack.run);
        std::fprintf(stderr, "[attack] %-44.*s expected %-8s seen %s\n", static_cast<int>(attack.name.size()),
                     attack.name.data(), outcome_name(attack.expected), outcome_name(seen));
        if (seen != attack.expected) {
            ++failures;
            if (attack.expected == Outcome::Silent || attack.expected == Outcome::Deadlock) {
                std::fprintf(stderr, "  the attack no longer succeeds: remove its ledger row\n");
            } else {
                std::fprintf(stderr, "  the attack now succeeds: this is a new gap in the discipline\n");
            }
        }
    }
    return failures == 0 ? 0 : 1;
}
