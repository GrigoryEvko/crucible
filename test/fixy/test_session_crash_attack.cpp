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
#include <fixy/session/Delegate.h>
#include <fixy/session/EventLog.h>
#include <fixy/session/Recording.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <sys/wait.h>
#include <unistd.h>

#include <any>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
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

namespace s = ::fixy::session;
namespace fp = ::foundation::permissions;
namespace eff = ::foundation::effects;

namespace {

// The context of each session in this file.  No payload here carries an
// effect row, so the background context admits every protocol.
using BgCtx = eff::detail::ctx_witnesses::BgWitness;
[[nodiscard]] BgCtx bg_ctx() noexcept { return BgCtx{eff::testing::bg()}; }

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
struct DelegatedWire {};
using Delegated = s::DelegatedSession<s::Recv<int, s::End>, DelegatedWire, s::DefaultAbandonmentPolicy, fp::EmptyPermSet>;
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
// each one, and it must say how it saw it.  Before the query, the two
// sessions looked for the hand-off marker alone.  Each handle below then
// travelled.

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

struct OneHolderChannel {
    int* in = nullptr;
    int* out = nullptr;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
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

// The failure detector reports the crash of the endpoint that `cell`
// watches.  The report carries the count that the session of that
// endpoint wrote into the cell, or zero when no session counted there.
void report_crash(s::PeerCrashCell& cell, s::CrashCause cause) {
    static_cast<void>(s::mint_crash_reporter(cell).report(cause));
}

// The crash of the endpoint whose inbound queue is `inbox`: the detector
// reports it, and the queue closes.
void crash_endpoint(s::PeerCrashCell& cell, Mailbox& inbox, s::CrashCause cause) {
    report_crash(cell, cause);
    inbox.is_closed = true;
}

using Token = s::Transferable<int, X>;

// A crash-watched send tries its write.  The queue has no bound, so a try
// fails only when the queue of the peer is closed, and then the value
// stays with the caller.
constexpr auto push_label = [](Port& port, std::size_t label) noexcept {
    if (port.out->is_closed) return false;
    port.out->slots.push_back(label);
    return true;
};
constexpr auto push_int = [](Port& port, int& value) noexcept {
    if (port.out->is_closed) return false;
    port.out->slots.push_back(static_cast<std::uint64_t>(value));
    return true;
};
constexpr auto push_token = [](Port& port, Token& token) noexcept {
    if (port.out->is_closed) return false;
    port.out->slots.push_back(static_cast<std::uint64_t>(token.value));
    return true;
};
// A crash-watched reception reads with no wait: the payload when one is
// queued, and no value otherwise.
constexpr auto read_int = [](Port& port) noexcept -> std::optional<int> {
    if (port.in->slots.empty()) return std::nullopt;
    const std::uint64_t slot = port.in->slots.front();
    port.in->slots.pop_front();
    return static_cast<int>(slot);
};
constexpr auto read_token = [](Port& port) noexcept -> std::optional<Token> {
    if (port.in->slots.empty()) return std::nullopt;
    const std::uint64_t slot = port.in->slots.front();
    port.in->slots.pop_front();
    return Token{static_cast<int>(slot), fp::mint_permission_root<X>()};
};
constexpr auto poll_label = [](Port& port) noexcept -> std::optional<std::size_t> {
    if (port.in->slots.empty()) return std::nullopt;
    const std::uint64_t slot = port.in->slots.front();
    port.in->slots.pop_front();
    return static_cast<std::size_t>(slot);
};

// p as a real crash session: it sends `count` messages, each the label 0
// and the value first, first + 1, and so on, into `out`, and its cell
// counts each one.  The session then stops with no crash of its own, so a
// report of the detector is the crash, and the report carries this count.
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
[[noreturn]] void send_to_crashed_peer_returns_payload() {
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
[[noreturn]] void token_relayed_to_crashed_peer() {
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
[[noreturn]] void crash_detected_twice() {
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
[[noreturn]] void queue_drains_before_detection() {
    using ProtoQ = s::Loop<s::Offer<s::Recv<int, s::Continue>, s::Recv<s::Crash<P>, s::End>>>;
    Mailbox to_p;
    Mailbox to_q;
    s::PeerCrashCell cell_p;
    s::PeerCrashCell cell_q;
    p_sends(cell_p, cell_q, to_p, to_q, 1, 4);
    report_crash(cell_p, s::CrashCause::Abort);
    auto q = s::mint_crash_session<ProtoQ, Q, P>(bg_ctx(), Port{&to_q, &to_p}, cell_p, s::mint_crash_writer(cell_q));
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
                auto [value, next] = std::move(branch).recv(read_int);
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
    s::PeerCrashCell cell_q;
    auto q = s::mint_crash_session<ProtoQ, Q, P>(bg_ctx(), Port{&to_q, &to_p}, cell_p, s::mint_crash_writer(cell_q));
    std::move(q).branch(poll_label, [](auto branch) noexcept { std::move(branch).detach(s::detach_reason::TestInstrumentation{}); });
    finish(Outcome::Silent);
}

// A transport that fabricates messages after the crash.  The report says
// that the peer sent no message, so a word that the transport reports is
// past the witness: the decorator refuses it and takes the crash branch.
[[noreturn]] void transport_fabricates_messages_after_crash() {
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
[[noreturn]] void transport_fabricates_after_queued_messages() {
    using ProtoQ = s::Loop<s::Offer<s::Recv<int, s::Continue>, s::Recv<s::Crash<P>, s::End>>>;
    Mailbox to_p;
    Mailbox to_q;
    s::PeerCrashCell cell_p;
    s::PeerCrashCell cell_q;
    p_sends(cell_p, cell_q, to_p, to_q, 5, 2);
    report_crash(cell_p, s::CrashCause::Abort);
    auto q = s::mint_crash_session<ProtoQ, Q, P>(bg_ctx(), Port{&to_q, &to_p}, cell_p, s::mint_crash_writer(cell_q));
    using Loop = decltype(q);
    std::optional<Loop> current{std::move(q)};
    int received = 0;
    bool detected = false;
    const auto always_a_word = [](Port&) noexcept -> std::optional<std::size_t> { return std::size_t{0}; };
    const auto always_a_value = [](Port&) noexcept -> std::optional<int> { return 5; };
    while (current) {
        Loop handle = std::move(*current);
        current.reset();
        std::move(handle).branch(always_a_word, [&](auto branch) noexcept {
            using Head = typename decltype(branch)::protocol;
            if constexpr (s::is_crash_branch_v<Head>) {
                auto [record, end] = std::move(branch).recv();
                detected = record.cause == s::CrashCause::Abort;
                (void)std::move(end).close();
            } else {
                auto [value, next] = std::move(branch).recv(always_a_value);
                if (value == 5) ++received;
                current.emplace(std::move(next));
            }
        });
    }
    finish(received == 2 && detected ? Outcome::Correct : Outcome::Silent);
}

// The report counts one message that has not arrived yet.  The queue is
// not empty, so the decorator waits for it instead of taking the crash
// branch, and takes the crash branch at the next reception.
[[noreturn]] void crash_branch_waits_for_an_owed_message() {
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
    using Loop = decltype(q);
    std::optional<Loop> current{std::move(q)};
    int empty_polls = 0;
    int received = 0;
    bool detected = false;
    // The message arrives only after three polls find nothing.
    const auto late_word = [&empty_polls, &in_flight, &to_q](Port& port) noexcept -> std::optional<std::size_t> {
        if (empty_polls < 3) {
            ++empty_polls;
            if (empty_polls == 3) {
                for (const std::uint64_t slot : in_flight.slots) to_q.slots.push_back(slot);
                in_flight.slots.clear();
            }
            return std::nullopt;
        }
        return poll_label(port);
    };
    while (current) {
        Loop handle = std::move(*current);
        current.reset();
        std::move(handle).branch(late_word, [&](auto branch) noexcept {
            using Head = typename decltype(branch)::protocol;
            if constexpr (s::is_crash_branch_v<Head>) {
                auto [record, end] = std::move(branch).recv();
                (void)record;
                detected = true;
                (void)std::move(end).close();
            } else {
                auto [value, next] = std::move(branch).recv(read_int);
                if (value == 11) ++received;
                current.emplace(std::move(next));
            }
        });
    }
    finish(received == 1 && detected && empty_polls == 3 ? Outcome::Correct : Outcome::Silent);
}

// An endpoint that crashes reports the count that its session wrote into
// its cell: three messages.  The survivor receives the three and then takes
// the crash branch, although its transport keeps reporting messages.
[[noreturn]] void endpoint_reports_its_own_count() {
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
    using Loop = decltype(q);
    std::optional<Loop> current{std::move(q)};
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
    while (current) {
        Loop handle = std::move(*current);
        current.reset();
        std::move(handle).branch(word_or_more, [&](auto branch) noexcept {
            using Head = typename decltype(branch)::protocol;
            if constexpr (s::is_crash_branch_v<Head>) {
                auto [record, end] = std::move(branch).recv();
                detected = record.cause == s::CrashCause::ErrorReturn;
                (void)std::move(end).close();
            } else {
                auto [value, next] = std::move(branch).recv(value_or_more);
                if (value == received) ++received;
                current.emplace(std::move(next));
            }
        });
    }
    finish(received == 3 && detected ? Outcome::Correct : Outcome::Silent);
}

// A bare reception from a crashed reliable peer whose transport invents a
// value.  The report counts no message, so the decorator refuses the value
// and ends the wait with Crash_Of_Reliable_Peer.
[[noreturn]] void bare_recv_refuses_a_fabricated_message() {
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

// A cell has one reporter.  A second mint for the same cell ends the
// process with Crash_Reporter_Twice.
[[noreturn]] void second_reporter_for_one_cell() {
    s::PeerCrashCell cell;
    auto first = s::mint_crash_reporter(cell);
    auto second = s::mint_crash_reporter(cell);
    static_cast<void>(std::move(first).report(s::CrashCause::Abort));
    static_cast<void>(std::move(second).report(s::CrashCause::Throw));
    finish(Outcome::Silent);
}

// One session counts into a cell.  A second writer for the same cell ends
// the process with Crash_Writer_Twice.
[[noreturn]] void second_writer_for_one_cell() {
    s::PeerCrashCell cell;
    auto first = s::mint_crash_writer(cell);
    auto second = s::mint_crash_writer(cell);
    static_cast<void>(first);
    static_cast<void>(second);
    finish(Outcome::Silent);
}

// A writer that a move emptied names no cell.  The mint refuses it with
// Crash_Writer_Moved_From, so no session counts into nothing.
[[noreturn]] void mint_takes_a_moved_from_writer() {
    using ProtoQ = s::Loop<s::Select<s::Send<int, s::Continue>>>;
    Mailbox to_p;
    Mailbox to_q;
    s::PeerCrashCell cell_p;
    s::PeerCrashCell cell_q;
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
[[noreturn]] void false_suspicion_fences_the_sender() {
    using ProtoP = s::Loop<s::Select<s::Send<int, s::Continue>>>;
    Mailbox to_p;
    Mailbox to_q;
    s::PeerCrashCell cell_p;
    s::PeerCrashCell cell_q;
    auto p = s::mint_crash_session<ProtoP, P, Q>(bg_ctx(), Port{&to_p, &to_q}, cell_q, s::mint_crash_writer(cell_p));
    auto chosen = std::move(p).select<0>(push_label);
    auto [next, undelivered] = std::move(chosen).send(1, push_int);
    (void)undelivered;
    report_crash(cell_p, s::CrashCause::Unknown);
    const std::optional<s::CrashWitness> witness = cell_p.witness();
    if (!witness || std::to_underlying(witness->messages_sent) != 1) finish(Outcome::Silent);
    auto after = std::move(next).select<0>(push_label);
    std::move(after).detach(s::detach_reason::TestInstrumentation{});
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
[[noreturn]] void crash_between_check_and_write_loses_token() {
    using Relay = s::Offer<s::Recv<Token, s::Select<s::Send<Token, s::End>>>, s::Recv<s::Crash<P>, s::End>>;
    Mailbox to_p;
    Mailbox to_q;
    to_q.slots.push_back(0);
    to_q.slots.push_back(7);
    s::PeerCrashCell cell_p;
    s::PeerCrashCell cell_q;
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
            token_lost = !(undelivered.has_value() && undelivered->value == 7) || is_token_queued;
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
[[noreturn]] void bare_recv_from_crashed_reliable_peer() {
    using ProtoQ = s::Recv<int, s::End>;
    Mailbox to_p;
    Mailbox to_q;
    s::PeerCrashCell cell_p;
    s::PeerCrashCell cell_q;
    report_crash(cell_p, s::CrashCause::Abort);
    auto q = s::mint_crash_session<ProtoQ, Q, P, s::ReliableSet<P>>(bg_ctx(), Port{&to_q, &to_p}, cell_p,
                                                                    s::mint_crash_writer(cell_q));
    auto [value, end] = std::move(q).recv(read_int);
    (void)value;
    (void)std::move(end).close();
    finish(Outcome::Silent);
}

// The peer sends a label, and its process dies before the payload of the
// branch.  The label and the payload are one message, so the transport
// split it.  The branch that the label entered has no crash branch, so
// the wait for the payload ends with Crash_Splits_A_Message.  The peer's
// own crash() cannot split a message: neg_sess_crash_between_label_and_
// payload and neg_sess_crash_recorded_between_label_and_payload.
[[noreturn]] void peer_dies_between_label_and_payload() {
    using ProtoP = s::Select<s::Send<int, s::End>>;
    using ProtoQ = s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<P>, s::End>>;
    Mailbox to_p;
    Mailbox to_q;
    s::PeerCrashCell cell_p;
    s::PeerCrashCell cell_q;
    auto p = s::mint_crash_session<ProtoP, P, Q>(bg_ctx(), Port{&to_p, &to_q}, cell_q, s::mint_crash_writer(cell_p));
    auto q = s::mint_crash_session<ProtoQ, Q, P>(bg_ctx(), Port{&to_q, &to_p}, cell_p, s::mint_crash_writer(cell_q));
    auto half_sent = std::move(p).select<0>(push_label);
    std::move(half_sent).detach(s::detach_reason::TestInstrumentation{});
    crash_endpoint(cell_p, to_p, s::CrashCause::Abort);
    std::move(q).branch(poll_label, [](auto branch) noexcept {
        using Head = typename decltype(branch)::protocol;
        if constexpr (s::is_crash_branch_v<Head>) {
            finish(Outcome::Silent);
        } else {
            auto [value, end] = std::move(branch).recv(read_int);
            (void)value;
            (void)std::move(end).close();
        }
    });
    finish(Outcome::Silent);
}

// The recorder outside the crash transport records a send to a crashed
// peer as lost, and the crash branch as a stop with its cause.
[[noreturn]] void recorder_sees_the_crash() {
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

}  // namespace

int main() {
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
