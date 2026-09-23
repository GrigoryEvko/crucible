// What fixy/session/Crash.h and fixy/session/CrashTransport.h claim,
// checked.
//
// The compile-time half checks the rules of Crash.h one by one against
// protocols that obey and break each rule, the crash dual, the subtyping
// side conditions, and the mint gate.  The runtime half runs Example 3.2
// of Barwell, Hou, Yoshida and Zhou (LMCS 2025, p. 8) under three
// schedules: no crash, a crash of the receiver before it receives, and a
// crash of the sender after its message is queued.
//
// The paper receives one label with a crash branch as a choice with one
// message label.  Here that is an Offer with one message branch, and the
// sender takes it with a Select of one branch, so the label travels.

#include <fixy/session/CrashTransport.h>

#include <cstdint>
#include <cstdio>
#include <deque>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace s = fixy::session;

namespace {

struct P {};  // role p of Example 3.2
struct Q {};  // role q of Example 3.2
struct R {};  // a third role
struct Text {
    std::string_view value;
};

// ── Rule 1: the crash label is never sent ───────────────────────────
static_assert(!s::is_well_formed_v<s::Send<s::Crash<Q>, s::End>>);
static_assert(!s::is_well_formed_v<s::Recv<int, s::Send<s::Crash<Q>, s::End>>>);
static_assert(!s::is_crash_well_formed_v<s::Select<s::Send<s::Crash<Q>, s::End>>>);

// ── Rule 2: no pure crash choice ────────────────────────────────────
using PureCrash = s::Offer<s::Recv<s::Crash<Q>, s::End>>;
static_assert(s::is_empty_choice_v<PureCrash>);
static_assert(s::is_empty_choice_v<s::Send<int, PureCrash>>);
static_assert(s::is_empty_choice_v<s::Offer<s::Sender<Q>, s::Recv<s::Crash<Q>, s::End>>>);
static_assert(!s::is_crash_well_formed_v<PureCrash>);

// ── Rule 4: no bare reception of the crash label ────────────────────
static_assert(s::is_well_formed_v<s::Recv<s::Crash<Q>, s::End>>);
static_assert(!s::is_crash_well_formed_v<s::Recv<s::Crash<Q>, s::End>>);

// ── Rules 6 and 7: crash branches trail, one for each peer ──────────
using CrashFirst = s::Offer<s::Recv<s::Crash<Q>, s::End>, s::Recv<int, s::End>>;
using TwoForQ =
    s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<Q>, s::End>, s::Recv<s::Crash<Q>, s::Send<int, s::End>>>;
static_assert(!s::is_empty_choice_v<CrashFirst>);
static_assert(!s::is_crash_well_formed_v<CrashFirst>);
static_assert(!s::is_crash_well_formed_v<TwoForQ>);

// ── Rules 3 and 5: coverage against the reliable set ────────────────
using Guarded = s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<Q>, s::End>>;
using Bare = s::Recv<int, s::End>;
static_assert(s::every_reception_handles_crash_v<Guarded, Q, s::ReliableSet<>>);
static_assert(!s::every_reception_handles_crash_v<Bare, Q, s::ReliableSet<>>);
static_assert(s::every_reception_handles_crash_v<Bare, Q, s::ReliableSet<Q>>);
// Rule 5: a crash branch for a reliable sender is untypable.
static_assert(!s::every_reception_handles_crash_v<Guarded, Q, s::ReliableSet<Q>>);
// Rule 3: a crash branch names the sender of its Offer.
static_assert(!s::every_reception_handles_crash_v<s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<R>, s::End>>, Q,
                                                  s::ReliableSet<>>);
// The payload of a message branch travels with its label.  The payload
// that follows the payload does not.
static_assert(!s::every_reception_handles_crash_v<s::Offer<s::Recv<int, s::Recv<int, s::End>>, s::Recv<s::Crash<Q>, s::End>>,
                                                  Q, s::ReliableSet<>>);
// A bare reception hidden inside a loop body is found.
static_assert(!s::every_reception_handles_crash_v<s::Loop<s::Send<int, s::Recv<int, s::Continue>>>, Q, s::ReliableSet<>>);
static_assert(
    s::every_reception_handles_crash_v<s::Loop<s::Send<int, s::Offer<s::Recv<int, s::Continue>, s::Recv<s::Crash<Q>, s::End>>>>,
                                       Q, s::ReliableSet<>>);
// A sender-annotated Offer is checked against its own sender.
static_assert(s::every_reception_handles_crash_v<s::Offer<s::Sender<R>, s::Recv<int, s::End>>, Q, s::ReliableSet<R>>);
static_assert(!s::every_reception_handles_crash_v<s::Offer<s::Sender<R>, s::Recv<int, s::End>>, Q, s::ReliableSet<>>);

// ── Stop is runtime syntax ──────────────────────────────────────────
static_assert(s::is_terminal_state_v<s::Stop>);
static_assert(std::is_same_v<s::dual_of_t<s::Stop>, s::Stop>);
static_assert(!s::is_well_formed_v<s::Stop>);
static_assert(!s::is_well_formed_v<s::Send<int, s::Stop>>);
static_assert(!s::is_well_formed_v<s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<Q>, s::Stop>>>);

// ── Crash index and crash dual ──────────────────────────────────────
static_assert(s::crash_branch_index_v<Guarded, Q> == 1);
static_assert(s::offer_has_crash_branch_v<Guarded, Q>);
static_assert(!s::offer_has_crash_branch_v<Guarded, P>);
static_assert(std::is_same_v<s::erase_crash_t<Guarded>, s::Offer<s::Recv<int, s::End>>>);
static_assert(std::is_same_v<s::crash_dual_t<Guarded>, s::Select<s::Send<int, s::End>>>);
// The plain dual of an endpoint with crash branches sends the crash
// label, so no handle can be minted on it.
static_assert(!s::is_well_formed_v<s::dual_of_t<Guarded>>);

// ── Example 3.2, as local protocols ─────────────────────────────────
//
// p: q!m<"abc">. (q?m'(x).0 + q?crash.0)
// q: p?m(x). p!m'<42>.0 + p?crash.0
using ProtoP = s::Select<s::Send<Text, s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<Q>, s::End>>>>;
using ProtoQ = s::Offer<s::Recv<Text, s::Select<s::Send<int, s::End>>>, s::Recv<s::Crash<P>, s::End>>;

static_assert(s::is_crash_well_formed_v<ProtoP> && s::is_crash_well_formed_v<ProtoQ>);
static_assert(s::is_crash_dual_v<ProtoP, ProtoQ>);
static_assert(s::CrashSessionAdmissible<ProtoP, P, Q, s::ReliableSet<>>);
static_assert(s::CrashSessionAdmissible<ProtoQ, Q, P, s::ReliableSet<>>);
// With q reliable, p may not keep a crash branch for q (rule 5).
static_assert(!s::CrashSessionAdmissible<ProtoP, P, Q, s::ReliableSet<Q>>);
static_assert(!s::CrashSessionAdmissible<ProtoP, P, P, s::ReliableSet<>>);

// ── Subtyping side conditions ───────────────────────────────────────
static_assert(s::crash_refinement_admissible_v<s::Stop, s::Stop>);
static_assert(!s::crash_refinement_admissible_v<s::Stop, s::End>);
static_assert(!s::crash_refinement_admissible_v<s::End, s::Stop>);
// The subtype Offer may have more message branches.
static_assert(s::crash_refinement_admissible_v<s::Offer<s::Recv<int, s::End>, s::Recv<long, s::End>, s::Recv<s::Crash<Q>, s::End>>,
                                               Guarded>);
// It may not add a crash branch the supertype lacks.
static_assert(!s::crash_refinement_admissible_v<Guarded, s::Offer<s::Recv<int, s::End>>>);
// The supertype may not be a pure crash choice.
static_assert(!s::crash_refinement_admissible_v<Guarded, PureCrash>);

// ── The mint ────────────────────────────────────────────────────────

struct Mailbox {
    std::deque<std::uint64_t> slots;
};

// One endpoint's view of the wire.
struct Port {
    Mailbox* in = nullptr;
    Mailbox* out = nullptr;
};

constexpr auto push_label = [](Port& port, std::size_t label) noexcept { port.out->slots.push_back(label); };
constexpr auto push_text = [](Port& port, Text&& text) noexcept { port.out->slots.push_back(text.value.size()); };
constexpr auto push_int = [](Port& port, int&& value) noexcept { port.out->slots.push_back(static_cast<std::uint64_t>(value)); };
constexpr auto pop_int = [](Port& port) noexcept {
    const std::uint64_t slot = port.in->slots.front();
    port.in->slots.pop_front();
    return static_cast<int>(slot);
};
constexpr auto pop_text = [](Port& port) noexcept {
    port.in->slots.pop_front();
    return Text{"abc"};
};
constexpr auto poll_label = [](Port& port) noexcept -> std::optional<std::size_t> {
    if (port.in->slots.empty()) return std::nullopt;
    const std::uint64_t slot = port.in->slots.front();
    port.in->slots.pop_front();
    return static_cast<std::size_t>(slot);
};

using CheckedP = decltype(s::mint_crash_session<ProtoP, P, Q>(Port{}, std::declval<const s::PeerCrashCell&>()));
static_assert(std::is_same_v<CheckedP::protocol, ProtoP>);
static_assert(!std::is_copy_constructible_v<CheckedP>);
static_assert(std::is_move_constructible_v<CheckedP>);

int fail(const char* what) {
    std::fprintf(stderr, "test_session_crash_stop: %s\n", what);
    return 1;
}

// Both endpoints live.  p sends, q replies, both reach End.
int run_without_crash() {
    Mailbox to_p;
    Mailbox to_q;
    s::PeerCrashCell cell_p;  // watched by q
    s::PeerCrashCell cell_q;  // watched by p
    auto p = s::mint_crash_session<ProtoP, P, Q>(Port{&to_p, &to_q}, cell_q);
    auto q = s::mint_crash_session<ProtoQ, Q, P>(Port{&to_q, &to_p}, cell_p);

    auto p_sent = std::move(p).select<0>(push_label);
    auto [p_wait, p_undelivered] = std::move(p_sent).send(Text{"abc"}, push_text);
    if (p_undelivered) return fail("a payload to a live peer came back");

    int reply = 0;
    std::move(q).branch(poll_label, [&](auto q_branch) {
        if constexpr (std::is_same_v<typename decltype(q_branch)::protocol, s::Recv<s::Crash<P>, s::End>>) {
            std::fprintf(stderr, "q took the crash branch of a live peer\n");
            std::abort();
        } else {
            auto [text, q_reply] = std::move(q_branch).recv(pop_text);
            auto q_sel = std::move(q_reply).template select<0>(push_label);
            auto [q_end, q_lost] = std::move(q_sel).send(static_cast<int>(text.value.size()) + 39, push_int);
            if (q_lost) std::abort();
            (void)std::move(q_end).close();
        }
    });

    std::move(p_wait).branch(poll_label, [&](auto p_branch) {
        if constexpr (std::is_same_v<typename decltype(p_branch)::protocol, s::Recv<int, s::End>>) {
            auto [value, p_end] = std::move(p_branch).recv(pop_int);
            reply = value;
            (void)std::move(p_end).close();
        } else {
            std::fprintf(stderr, "p took the crash branch of a live peer\n");
            std::abort();
        }
    });
    return reply == 42 ? 0 : fail("p received the wrong reply");
}

// q crashes before it receives.  p's message is lost and comes back,
// and p then takes its crash branch with the recorded cause.
int run_receiver_crashes_first() {
    Mailbox to_p;
    Mailbox to_q;
    s::PeerCrashCell cell_p;
    s::PeerCrashCell cell_q;
    auto p = s::mint_crash_session<ProtoP, P, Q>(Port{&to_p, &to_q}, cell_q);
    auto q = s::mint_crash_session<ProtoQ, Q, P>(Port{&to_q, &to_p}, cell_p);

    const Port q_port = std::move(q).crash(s::CrashCause::Throw, cell_q);
    if (q_port.in != &to_q) return fail("crash() did not give back the resource");

    auto p_sent = std::move(p).select<0>(push_label);
    auto [p_wait, p_undelivered] = std::move(p_sent).send(Text{"abc"}, push_text);
    if (!p_undelivered || p_undelivered->value != "abc") return fail("the lost payload did not come back");
    if (!to_q.slots.empty()) return fail("a message reached the queue of a crashed peer");

    bool took_crash_branch = false;
    std::move(p_wait).branch(poll_label, [&](auto p_branch) {
        if constexpr (std::is_same_v<typename decltype(p_branch)::protocol, s::Recv<s::Crash<Q>, s::End>>) {
            auto [record, p_end] = std::move(p_branch).recv();
            took_crash_branch = record.cause == s::CrashCause::Throw;
            (void)std::move(p_end).close();
        } else {
            std::fprintf(stderr, "p received from a crashed peer\n");
            std::abort();
        }
    });
    return took_crash_branch ? 0 : fail("p did not take the crash branch with the recorded cause");
}

// p crashes after its message is queued.  q must still receive the
// message, because r-rcv-⊙ detects a crash only on an empty queue.
int run_sender_crashes_after_send() {
    Mailbox to_p;
    Mailbox to_q;
    s::PeerCrashCell cell_p;
    s::PeerCrashCell cell_q;
    auto p = s::mint_crash_session<ProtoP, P, Q>(Port{&to_p, &to_q}, cell_q);
    auto q = s::mint_crash_session<ProtoQ, Q, P>(Port{&to_q, &to_p}, cell_p);

    auto p_sent = std::move(p).select<0>(push_label);
    auto [p_wait, p_undelivered] = std::move(p_sent).send(Text{"abc"}, push_text);
    if (p_undelivered) return fail("a payload to a live peer came back");
    (void)std::move(p_wait).crash(s::CrashCause::Abort, cell_p);

    bool took_message = false;
    std::move(q).branch(poll_label, [&](auto q_branch) {
        if constexpr (std::is_same_v<typename decltype(q_branch)::protocol, s::Recv<s::Crash<P>, s::End>>) {
            std::fprintf(stderr, "q detected the crash before the queued message\n");
            std::abort();
        } else {
            auto [text, q_reply] = std::move(q_branch).recv(pop_text);
            took_message = text.value == "abc";
            auto q_sel = std::move(q_reply).template select<0>(push_label);
            auto [q_end, q_lost] = std::move(q_sel).send(42, push_int);
            took_message = took_message && q_lost.has_value();
            (void)std::move(q_end).close();
        }
    });
    if (!to_p.slots.empty()) return fail("a message reached the queue of a crashed peer");
    return took_message ? 0 : fail("q lost the message that was queued before the crash");
}

// The cell records the first report and keeps it.
int run_cell() {
    s::PeerCrashCell cell;
    if (cell.has_crashed() || cell.crash_cause()) return fail("a fresh cell reports a crash");
    if (!cell.mark_crashed(s::CrashCause::ErrorReturn)) return fail("the first report was refused");
    if (cell.mark_crashed(s::CrashCause::Abort)) return fail("a second report was recorded");
    if (cell.crash_cause() != s::CrashCause::ErrorReturn) return fail("a second report replaced the cause");
    return 0;
}

}  // namespace

int main() {
    if (const int rc = run_cell(); rc != 0) return rc;
    if (const int rc = run_without_crash(); rc != 0) return rc;
    if (const int rc = run_receiver_crashes_first(); rc != 0) return rc;
    if (const int rc = run_sender_crashes_after_send(); rc != 0) return rc;
    return 0;
}
