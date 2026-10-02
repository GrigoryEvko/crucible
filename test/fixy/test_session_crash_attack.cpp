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
//
// The test is several source files of one executable, so that no
// translation unit compiles every attack:
//
//   session_crash_attack.h     the shared part and the table of the
//                              runtime attacks
//   this file                  the refusals at compile time, the attacks
//                              on the event log, the ledger, the runner
//                              and main
//   ..._detection.cpp          the detection of a crash
//   ..._fabrication.cpp        a transport that invents messages
//   ..._cells.cpp              the reporter and the writer of a crash
//                              cell, and a crash against a write

#include "session_crash_attack.h"

#include <sys/wait.h>
#include <unistd.h>

#include <any>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace test_session_crash_attack {

void report_crash(s::PeerCrashCell& cell, s::CrashCause cause) {
    static_cast<void>(s::mint_crash_reporter(cell).report(cause));
}

void crash_endpoint(s::PeerCrashCell& cell, Mailbox& inbox, s::CrashCause cause) {
    report_crash(cell, cause);
    inbox.is_closed = true;
}

void p_sends(s::PeerCrashCell& cell_p, const s::PeerCrashCell& cell_q, Mailbox& in, Mailbox& out, int first,
             int count) {
    using Stream = s::Loop<s::Select<s::Send<int, s::Continue>>>;
    auto p = s::mint_crash_session<Stream, P, Q>(bg_ctx(), Port{&in, &out}, cell_q, s::mint_crash_writer(cell_p));
    for (int round = 0; round < count; ++round) {
        auto chosen = std::move(p).select<0>(push_label);
        auto [next, undelivered] = std::move(chosen).send(first + round, push_int);
        (void)undelivered;
        p = std::move(next);
    }
    std::move(p).detach(s::detach_reason::InfiniteLoopProtocol{});
}

void finish(Outcome outcome) {
    std::fflush(stderr);
    switch (outcome) {
        case Outcome::Correct:
            std::_Exit(kCorrectExit);
        case Outcome::Silent:
            std::_Exit(kSilentExit);
        case Outcome::Deadlock:
            std::_Exit(kDeadlockExit);
        case Outcome::Caught:
        default:
            std::abort();
    }
}

// ── The attacks on the event log ────────────────────────────────────

// The recorder outside the crash transport records a send to a crashed
// peer as lost, and the crash branch as a stop with its cause.
void recorder_sees_the_crash() {
    using ProtoQ = s::Select<s::Send<int, s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<P>, s::End>>>>;
    Mailbox to_p;
    Mailbox to_q;
    s::PeerCrashCell cell_p;
    s::PeerCrashCell cell_q;
    report_crash(cell_p, s::CrashCause::Throw);
    s::SessionEventLog log{s::SessionTagId{9}};
    auto q = s::mint_recorded_session(
        s::mint_crash_session<ProtoQ, Q, P>(bg_ctx(), Port{&to_q, &to_p}, cell_p, s::mint_crash_writer(cell_q)), log,
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
void corrupt_event_bytes() {
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
    for (std::size_t index = 0; index < bytes.size(); ++index)
        torn[index] = bytes[index];
    const auto torn_log = s::decode_session_log(torn);
    if (torn_log || torn_log.error().error != s::EventDecodeError::Truncated) finish(Outcome::Silent);
    finish(refused > 0 && accepted > 0 ? Outcome::Correct : Outcome::Silent);
}

namespace {

// ── Refused at compile time ─────────────────────────────────────────

// A delegated endpoint has peers that no detector of this session
// watches.  The coverage walk does not look inside a payload, so before
// the delegation rule this protocol was admitted, and a crash of the
// delegated peer left the holder waiting.
struct DelegatedWire {};
using Delegated =
    s::DelegatedSession<s::Recv<int, s::End>, DelegatedWire, s::DefaultAbandonmentPolicy, fp::EmptyPermSet>;
using ReceivesDelegation = s::Offer<s::Recv<Delegated, s::End>, s::Recv<s::Crash<P>, s::End>>;
static_assert(!s::CrashSessionAdmissible<ReceivesDelegation, Q, P, s::NoReliableRoles>);
struct HidesDelegation {
    int tag = 0;
    Delegated inner;
};
using ReceivesHiddenDelegation = s::Offer<s::Recv<HidesDelegation, s::End>, s::Recv<s::Crash<P>, s::End>>;
static_assert(!s::CrashSessionAdmissible<ReceivesHiddenDelegation, Q, P, s::NoReliableRoles>);
static_assert(s::payload_conveys_delegation_v<HidesDelegation>);
static_assert(!s::payload_conveys_delegation_v<int>);

// ── Delegation, read by one query ───────────────────────────────────
//
// A crash session and a checkpoint session ask one question of each
// payload: payload_conveys_delegation_v of fixy/session/Payload.h.  Each
// carrier below hides an endpoint in a legal payload.  The query must see
// each one, and it must say how it saw it.  A check that looked for the
// hand-off marker alone would let each handle below travel.

using DelegationCarrier = s::DelegationCarrier;
using Endpoint = s::SessionHandle<s::Recv<int, s::End>, int*>;

template <typename T>
inline constexpr DelegationCarrier carrier_of = s::payload_delegation_carrier_v<T>;

// A class template that only names its argument.
template <typename T>
struct Mention {};

struct NestsEndpoint {
    struct Inner {
        int sequence = 0;
        Endpoint end;
    };
    Inner inner;
};

union HoldsEndpointInUnion {
    Endpoint end;
    int raw;
    HoldsEndpointInUnion() noexcept : raw{0} {}
    ~HoldsEndpointInUnion() noexcept {}
};

struct ReferencesEndpoint {
    Endpoint& end;
};

// A handle of a session library that fixy does not know.
struct ForeignProtocol {};
template <typename Proto>
struct ForeignHandle {
    using protocol = Proto;
    int descriptor = 0;
};

// A specialization that holds a foreign handle in a member that none of
// its arguments names.
template <typename T>
struct ForeignBox {
    T tag{};
    ForeignHandle<ForeignProtocol> held;
};

// A closure that owns an endpoint.  GCC 16 reflects no capture.  The
// query cannot read what the closure holds, and it refuses the closure.
[[maybe_unused]] auto capture_endpoint(Endpoint end) {
    return [held = std::move(end)]() mutable noexcept { (void)held; };
}
using CapturesEndpoint = decltype(capture_endpoint(std::declval<Endpoint>()));

using WatchedEndpoint = s::CrashWatched<Endpoint, Q, P, s::NoReliableRoles, BgCtx>;
using RecordedEndpoint = s::Recorded<Endpoint>;

// A class that the payload holds, points at, or names in a template
// argument is read for its members.  A handle of any library declares
// the protocol it runs, so the query sees it through each reach.
static_assert(carrier_of<Endpoint> == DelegationCarrier::Endpoint);
static_assert(carrier_of<NestsEndpoint> == DelegationCarrier::Endpoint);
static_assert(carrier_of<HoldsEndpointInUnion> == DelegationCarrier::Endpoint);
static_assert(carrier_of<Endpoint[2]> == DelegationCarrier::Endpoint);
static_assert(carrier_of<std::optional<Endpoint>> == DelegationCarrier::Endpoint);
static_assert(carrier_of<std::variant<int, Endpoint>> == DelegationCarrier::Endpoint);
static_assert(carrier_of<std::array<Endpoint, 2>> == DelegationCarrier::Endpoint);
static_assert(carrier_of<WatchedEndpoint> == DelegationCarrier::Endpoint);
static_assert(carrier_of<RecordedEndpoint> == DelegationCarrier::Endpoint);
static_assert(carrier_of<ForeignHandle<ForeignProtocol>> == DelegationCarrier::Endpoint);
static_assert(carrier_of<Endpoint*> == DelegationCarrier::Endpoint);
static_assert(carrier_of<ReferencesEndpoint> == DelegationCarrier::Endpoint);
static_assert(carrier_of<std::unique_ptr<Endpoint>> == DelegationCarrier::Endpoint);
static_assert(carrier_of<std::shared_ptr<Endpoint>> == DelegationCarrier::Endpoint);
static_assert(carrier_of<std::vector<Endpoint>> == DelegationCarrier::Endpoint);
static_assert(carrier_of<ForeignHandle<s::Recv<int, s::End>>*> == DelegationCarrier::Endpoint);
static_assert(carrier_of<ForeignHandle<ForeignProtocol>*> == DelegationCarrier::Endpoint);
static_assert(carrier_of<std::vector<ForeignHandle<ForeignProtocol>>> == DelegationCarrier::Endpoint);
static_assert(carrier_of<ForeignBox<int>*> == DelegationCarrier::Endpoint,
              "the member that holds the handle is named by no argument, so only a read of the pointee finds it");
static_assert(carrier_of<std::unique_ptr<ForeignBox<int>>> == DelegationCarrier::Endpoint);
// A class or a template with no definition here cannot be read, and the
// query stops the build instead of giving a value:
// neg_sess_crash_delegation_undefined_template and
// neg_sess_crash_delegation_declared_class.  The read instantiates each
// specialization it reaches, so a later explicit specialization of it is
// ill-formed: neg_sess_crash_delegation_specialized_after_query.
static_assert(carrier_of<Mention<Delegated>> == DelegationCarrier::HandOff);
static_assert(carrier_of<s::Transferable<Delegated, X>> == DelegationCarrier::HandOff);
static_assert(carrier_of<CapturesEndpoint> == DelegationCarrier::UnreadableState);
static_assert(carrier_of<std::function<void()>> == DelegationCarrier::TypeErasure);
static_assert(carrier_of<std::any> == DelegationCarrier::TypeErasure);
static_assert(carrier_of<void*> == DelegationCarrier::OpaquePointer);
static_assert(carrier_of<const void*> == DelegationCarrier::OpaquePointer);
// A function names code that the recipient runs.  Its target can step an
// endpoint that the sender left in state that the target reaches, and
// the query cannot read the target.
struct Stepper {
    void step() noexcept {}
};
static_assert(carrier_of<void (*)() noexcept> == DelegationCarrier::FunctionPointer);
static_assert(carrier_of<int (&)(int)> == DelegationCarrier::FunctionPointer);
static_assert(carrier_of<void (Stepper::*)() noexcept> == DelegationCarrier::FunctionPointer);
static_assert(carrier_of<std::pair<int, void (*)()>> == DelegationCarrier::FunctionPointer);
static_assert(carrier_of<Stepper> == DelegationCarrier::None);

// A local class reaches the query as any class does.
[[nodiscard]] consteval bool local_endpoint_is_seen() {
    struct LocalEndpoint {
        using protocol [[maybe_unused]] = s::End;
    };
    struct LocalMessage {
        int sequence = 0;
    };
    return s::payload_conveys_delegation_v<LocalEndpoint> && s::payload_conveys_delegation_v<Mention<LocalEndpoint>*>
        && !s::payload_conveys_delegation_v<LocalMessage> && !s::payload_conveys_delegation_v<Mention<LocalMessage>*>;
}
static_assert(local_endpoint_is_seen());

// What delegates nothing.  A protocol is a type that names a
// conversation, and not an endpoint of it.  A payload that names one is
// plain.
struct PlainMessage {
    int sequence = 0;
    std::array<char, 8> text{};
};
static_assert(carrier_of<int> == DelegationCarrier::None);
static_assert(carrier_of<PlainMessage> == DelegationCarrier::None);
static_assert(carrier_of<s::Send<int, s::End>> == DelegationCarrier::None);
static_assert(carrier_of<s::VendorPinned<s::VendorBackend::Portable, s::End>> == DelegationCarrier::None);
static_assert(carrier_of<s::Crash<P>> == DelegationCarrier::None);
static_assert(carrier_of<s::Transferable<int, X>> == DelegationCarrier::None);
static_assert(carrier_of<std::string> == DelegationCarrier::None);
static_assert(carrier_of<std::vector<int>> == DelegationCarrier::None);
static_assert(carrier_of<std::optional<PlainMessage>> == DelegationCarrier::None);

// The two sessions refuse what the query sees.
using ReceivesEndpoint = s::Offer<s::Recv<Endpoint, s::End>, s::Recv<s::Crash<P>, s::End>>;
static_assert(!s::CrashSessionAdmissible<ReceivesEndpoint, Q, P, s::NoReliableRoles>);
using ReceivesEndpointPointer = s::Offer<s::Recv<Endpoint*, s::End>, s::Recv<s::Crash<P>, s::End>>;
static_assert(!s::CrashSessionAdmissible<ReceivesEndpointPointer, Q, P, s::NoReliableRoles>);
using SendsOwnedEndpoint = s::Select<s::Commit<s::Send<std::unique_ptr<Endpoint>, s::End>>, s::Roll>;
static_assert(s::checkpoint_verdict_v<SendsOwnedEndpoint, s::dual_of_t<SendsOwnedEndpoint>>
              == s::CheckpointVerdict::NotCheckpointShaped);
using SendsPlain = s::Select<s::Commit<s::Send<PlainMessage, s::End>>, s::Roll>;
static_assert(s::checkpoint_verdict_v<SendsPlain, s::dual_of_t<SendsPlain>> == s::CheckpointVerdict::Compliant);

// ── The delegation ledger ───────────────────────────────────────────
//
// Each row is a carrier that the query does not see.  The ledger can
// only shrink: when the query starts to see a carrier, its row stops the
// build.

struct SharedChannel {
    int* in = nullptr;
    int* out = nullptr;
};

// A copy of the Resource of a live session is closed: a Resource that can
// be copied and reaches a channel is not a Resource, so no session holds
// one.  The one-holder form moves, and it is on the ledger below.
static_assert(!s::SessionResource<SharedChannel>);
static_assert(!s::SessionResource<int*>);

static_assert(s::SessionResource<OneHolderChannel>);
static_assert(!std::is_copy_constructible_v<OneHolderChannel>);
static_assert(!std::is_copy_assignable_v<OneHolderChannel>);
static_assert(sizeof(OneHolderChannel) == 2 * sizeof(int*), "the move-only member takes no storage");

struct delegation_gap {
    std::string_view carrier;
    std::string_view why;
    bool is_seen;
};

constexpr delegation_gap delegation_gaps[] = {
    {"an integer that holds the address of an endpoint",
     "the query reads types, and an integer has no type provenance.  std::bit_cast turns the integer back into a "
     "pointer to the endpoint, and an index into a table of endpoints needs no cast at all.  C++ has no provenance "
     "on an integer, so no type can refuse it",
     s::payload_conveys_delegation_v<std::uintptr_t>},
    {"a Resource moved out through the reference a transport receives",
     "a transport gets the Resource by non-const reference, so it can move the Resource out and send it, and the "
     "handle keeps a moved-from Resource.  The handle must lend the Resource to the transport, and a C++ reference "
     "cannot stop a move through it.  A Resource that the recipient builds again from raw parts is the same case",
     s::payload_conveys_delegation_v<OneHolderChannel>},
};

consteval bool delegation_gaps_are_open() {
    for (const delegation_gap& gap : delegation_gaps) {
        if (gap.is_seen || gap.why.empty()) return false;
    }
    return true;
}
static_assert(delegation_gaps_are_open(),
              "the query now sees a carrier on the delegation ledger: remove its row, and add the carrier to the "
              "static assertions above");

// A rollback cannot recall a delegated endpoint.
using DelegatesThenCommits = s::Select<s::Send<Delegated, s::Select<s::Commit<s::End>, s::Abort>>>;
static_assert(s::checkpoint_verdict_v<DelegatesThenCommits, s::dual_of_t<DelegatesThenCommits>>
              == s::CheckpointVerdict::NotCheckpointShaped);

// A crash branch inside a checkpoint session: the two disciplines do
// not compose, and the checkpoint mint refuses the mixture.
using CrashInsideCheckpoint =
    s::Offer<s::Recv<int, s::Offer<s::Commit<s::End>, s::Abort>>, s::Recv<s::Crash<P>, s::End>>;
static_assert(
    s::checkpoint_verdict_v<CrashInsideCheckpoint, s::Select<s::Send<int, s::Select<s::Commit<s::End>, s::Abort>>>>
    != s::CheckpointVerdict::Compliant);

// A crash session cannot start with a token: its mint has an empty set,
// so the only token in flight is one it received.
using SendsToken = s::Select<s::Send<s::Transferable<int, X>, s::End>>;
static_assert(!s::CrashSessionAdmissible<SendsToken, Q, P, s::NoReliableRoles>);

// A reception hidden in a loop, one step after the guarded choice.
using HiddenInLoop = s::Loop<s::Offer<s::Recv<int, s::Recv<int, s::Continue>>, s::Recv<s::Crash<P>, s::End>>>;
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

// ── The ledger ──────────────────────────────────────────────────────
//
// Each row is an attack that succeeds, and the condition it breaks.  The
// ledger is empty: a transport that invents messages after the crash met
// the count of the crash report, which the transport cannot write.

struct limitation_row {
    std::string_view attack;
    std::string_view breaks;
};

constexpr std::array<limitation_row, 0> known_limitations{};

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

// ── The runner ──────────────────────────────────────────────────────

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
        case Outcome::Caught:
            return "caught";
        case Outcome::Silent:
            return "silent";
        case Outcome::Deadlock:
            return "deadlock";
        case Outcome::Correct:
            return "correct";
        default:
            return "unknown";
    }
}

// The one-holder Resource at run time: a handle takes it by move, a step
// hands it on, and close() gives back the two channel addresses.
[[nodiscard]] int one_holder_resource_moves() {
    int in = 0;
    int out = 0;
    OneHolderChannel channel{&in, &out};
    auto head = s::mint_session_handle<s::Send<int, s::End>>(std::move(channel));
    auto at_end = std::move(head).send(9, [](OneHolderChannel& ch, int& value) noexcept {
        *ch.out = value;
        return true;
    });
    const OneHolderChannel back = std::move(at_end).close();
    if (back.in != &in || back.out != &out || out != 9) {
        std::fprintf(stderr, "the one-holder Resource did not carry its channel through the session\n");
        return 1;
    }
    return 0;
}

// Runs each attack of the table in a child, and compares its outcome with
// the expected one.  Returns the exit code of the test.
[[nodiscard]] int run_campaign() {
    if (const int rc = one_holder_resource_moves(); rc != 0) return rc;
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

}  // namespace

}  // namespace test_session_crash_attack

int main() { return test_session_crash_attack::run_campaign(); }
