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
#include <fixy/session/Projection.h>
#include <fixy/session/Subtype.h>

#include <foundation/effects/Computation.h>
#include <foundation/effects/Ctx.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <memory>
#include <meta>
#include <optional>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

namespace s = fixy::session;
namespace eff = ::foundation::effects;

namespace {

// The context of each session in this file.  No payload of Example 3.2
// carries an effect row, so the background context admits it.
using BgCtx = eff::detail::ctx_witnesses::BgWitness;
using BgIoCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO>>;
[[nodiscard]] BgCtx bg_ctx() noexcept { return BgCtx{eff::testing::bg()}; }

struct P {};  // role p of Example 3.2
struct Q {};  // role q of Example 3.2
struct R {};  // a third role
// The payload row walk of fixy/concurrent/PayloadRow.h reads no
// std::string_view, so the text travels as a pointer to its characters.
struct Text {
    const char* value = "";
    [[nodiscard]] std::string_view view() const noexcept { return value; }
};

// ── Each verdict is a concept ────────────────────────────────────────
//
// A concept has no specialization, so no declaration of a program can
// change the answer of one of these verdicts.
static_assert(std::meta::is_concept(^^s::every_reception_handles_crash_v),
              "every_reception_handles_crash_v must stay a concept, so that no specialization hides a bare reception");
static_assert(std::meta::is_concept(^^s::is_crash_well_formed_v),
              "is_crash_well_formed_v must stay a concept, so that no specialization admits a crash label that is sent");

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

// ── A Select with a Sender note ─────────────────────────────────────
//
// The note names the endpoint that picks, and it is not a branch.  The
// walk reads a noted Select as the Select of its branches, so the note
// hides neither a sound branch nor a defect in one.
namespace walk = s::detail::crash;
using NotedSelect = s::Select<s::Sender<P>, s::Send<int, Guarded>>;
using NotedSelectSendsCrash = s::Select<s::Sender<P>, s::Send<s::Crash<Q>, s::End>>;
using NotedSelectThenBare = s::Select<s::Sender<P>, s::Send<int, Bare>>;
struct DelegatedWire {};
using NotedSelectDelegates =
    s::Select<s::Sender<P>, s::Send<s::DelegatedSession<Bare, DelegatedWire, s::DefaultAbandonmentPolicy,
                                                        ::foundation::permissions::EmptyPermSet>,
                                    s::End>>;
static_assert(s::is_crash_well_formed_v<NotedSelect>);
static_assert(!s::is_crash_well_formed_v<NotedSelectSendsCrash>);
static_assert(walk::is_delegation_free_v<NotedSelect>);
static_assert(!walk::is_delegation_free_v<NotedSelectDelegates>);
static_assert(s::every_reception_handles_crash_v<NotedSelect, Q, s::ReliableSet<>>);
static_assert(!s::every_reception_handles_crash_v<NotedSelectThenBare, Q, s::ReliableSet<>>);
static_assert(std::is_same_v<s::erase_crash_t<NotedSelect>, s::Select<s::Sender<P>, s::Send<int, s::Offer<s::Recv<int, s::End>>>>>);
static_assert(walk::is_every_sender_watched_v<NotedSelect, Q, s::ReliableSet<>>);
static_assert(!walk::is_every_sender_watched_v<
              s::Select<s::Sender<P>, s::Send<int, s::Offer<s::Sender<R>, s::Recv<int, s::End>, s::Recv<s::Crash<R>, s::End>>>>,
              Q, s::ReliableSet<>>);

// ── The roles that a keyed message names ────────────────────────────
//
// One cell watches one role.  A keyed reception comes from the role that
// its PeerMsg names, and a keyed send goes to that role, so each named
// role is the watched peer or reliable, as the sender of an Offer is.
struct Hello {};
using FromQ = s::Recv<s::PeerMsg<Q, Hello, int>, s::End>;
using ToQ = s::Send<s::PeerMsg<Q, Hello, int>, s::End>;
static_assert(!s::CrashSessionAdmissible<FromQ, P, R, s::ReliableSet<R>>,
              "the message comes from q, which is unreliable and not the watched peer r");
static_assert(s::CrashSessionAdmissible<FromQ, P, R, s::ReliableSet<Q>>, "a reliable q needs no watch");
static_assert(s::CrashSessionAdmissible<FromQ, P, Q, s::ReliableSet<Q>>);
static_assert(!s::CrashSessionAdmissible<ToQ, P, R, s::ReliableSet<>>, "the send goes to an unwatched q");
static_assert(s::CrashSessionAdmissible<ToQ, P, Q, s::ReliableSet<>>);

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

// ── A vendor pin passes to what it pins ─────────────────────────────
using PinnedP = s::VendorPinned<::foundation::algebra::lattices::VendorBackend::NV, ProtoP>;
static_assert(s::CrashSessionAdmissible<PinnedP, P, Q, s::ReliableSet<>>);
static_assert(!s::CrashSessionAdmissible<s::VendorPinned<::foundation::algebra::lattices::VendorBackend::NV, FromQ>, P,
                                         R, s::ReliableSet<R>>);

// ── Subtyping side conditions ───────────────────────────────────────
//
// The refinement of fixy/session/Subtype.h holds rules Sub-stop and Sub-&
// through the registry.  Stop is not plain, so it is no operand of the
// relation at all, which is stricter than stop ⩽ stop and needs no
// design-time protocol to hold it.  The crash label is no label and is
// not sendable.
static_assert(!s::is_subtype_sync_v<s::Stop, s::Stop>);
static_assert(!s::is_subtype_sync_v<s::Stop, s::End>);
static_assert(!s::is_subtype_sync_v<s::End, s::Stop>);
// The subtype Offer may have more message branches.
static_assert(s::is_subtype_sync_v<s::Offer<s::Recv<int, s::End>, s::Recv<long, s::End>, s::Recv<s::Crash<Q>, s::End>>,
                                   Guarded>);
// It may not add a crash branch the supertype lacks.
static_assert(!s::is_subtype_sync_v<Guarded, s::Offer<s::Recv<int, s::End>>>);
// The supertype may not be a pure crash choice.
static_assert(!s::is_subtype_sync_v<Guarded, PureCrash>);

// ── The mint ────────────────────────────────────────────────────────

struct Mailbox {
    std::deque<std::uint64_t> slots;
};

// One endpoint's view of the wire.  It reaches the two queues, so it has
// one holder.
struct Port {
    Mailbox* in = nullptr;
    Mailbox* out = nullptr;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};

// A crash-watched send tries its write: the queue has no bound, so each
// try takes the value.
constexpr auto push_label = [](Port& port, std::size_t label) noexcept {
    port.out->slots.push_back(label);
    return true;
};
constexpr auto push_text = [](Port& port, Text& text) noexcept {
    port.out->slots.push_back(text.view().size());
    return true;
};
constexpr auto push_int = [](Port& port, int& value) noexcept {
    port.out->slots.push_back(static_cast<std::uint64_t>(value));
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
constexpr auto read_text = [](Port& port) noexcept -> std::optional<Text> {
    if (port.in->slots.empty()) return std::nullopt;
    port.in->slots.pop_front();
    return Text{"abc"};
};
constexpr auto poll_label = [](Port& port) noexcept -> std::optional<std::size_t> {
    if (port.in->slots.empty()) return std::nullopt;
    const std::uint64_t slot = port.in->slots.front();
    port.in->slots.pop_front();
    return static_cast<std::size_t>(slot);
};

using CheckedP = decltype(s::mint_crash_session<ProtoP, P, Q>(std::declval<const BgCtx&>(), Port{},
                                                               std::declval<const s::PeerCrashCell&>(),
                                                               std::declval<s::CrashWriter>()));
static_assert(std::is_same_v<CheckedP::protocol, ProtoP>);

// The context of a crash session admits the effect row of each payload,
// as the context of mint_session does.
using IoPayload = eff::Computation<eff::Row<eff::Effect::IO>, int>;
using ReceivesIo = s::Offer<s::Recv<IoPayload, s::End>, s::Recv<s::Crash<P>, s::End>>;
static_assert(s::CrashSessionAdmissible<ReceivesIo, Q, P, s::ReliableSet<>>);
static_assert(!s::CtxFitsCrashSession<BgCtx, ReceivesIo, Q, P, s::ReliableSet<>, Port>, "the background context holds no IO");
static_assert(s::CtxFitsCrashSession<BgIoCtx, ReceivesIo, Q, P, s::ReliableSet<>, Port>);
static_assert(!s::CtxFitsCrashSession<int, ProtoQ, Q, P, s::ReliableSet<>, Port>, "an int is not an execution context");
static_assert(!std::is_copy_constructible_v<CheckedP>);
static_assert(std::is_move_constructible_v<CheckedP>);

// ── The entry of a crash branch ─────────────────────────────────────
//
// The decorator enters a crash branch through HandleFactory::recover,
// with the context of the mint.  The gate admits a branch that is no
// label, from the empty set, with a context that admits its row.
struct Region {
    using permission_row = eff::Row<>;
};
using Waits = s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<P>, s::End>>;
using ReceivesIoOnCrash = s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<P>, s::Recv<IoPayload, s::End>>>;
template <std::size_t I, typename PS, typename Ctx = BgCtx, typename Choice = Waits>
constexpr bool recovers =
    requires(Ctx const& ctx, s::SessionHandle<Choice, Port, void, s::DefaultAbandonmentPolicy, PS>&& handle) {
        s::HandleFactory::recover<I>(ctx, std::move(handle));
    };
static_assert(recovers<1, ::foundation::permissions::EmptyPermSet>);
static_assert(!recovers<0, ::foundation::permissions::EmptyPermSet>, "branch 0 is a label, which the peer picks");
static_assert(!recovers<1, ::foundation::permissions::PermSet<Region>>, "the handle holds a token");
static_assert(!recovers<2, ::foundation::permissions::EmptyPermSet>, "the Offer has two branches");
static_assert(!recovers<1, ::foundation::permissions::EmptyPermSet, int>, "an int is not an execution context");
static_assert(!recovers<1, ::foundation::permissions::EmptyPermSet, BgCtx, ReceivesIoOnCrash>,
              "the background context holds no IO");
static_assert(recovers<1, ::foundation::permissions::EmptyPermSet, BgIoCtx, ReceivesIoOnCrash>);
static_assert(std::is_same_v<typename CheckedP::context_type, BgCtx>, "the decorator keeps the context of the mint");

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
    auto p = s::mint_crash_session<ProtoP, P, Q>(bg_ctx(), Port{&to_p, &to_q}, cell_q, s::mint_crash_writer(cell_p));
    auto q = s::mint_crash_session<ProtoQ, Q, P>(bg_ctx(), Port{&to_q, &to_p}, cell_p, s::mint_crash_writer(cell_q));

    auto p_sent = std::move(p).select<0>(push_label);
    auto [p_wait, p_undelivered] = std::move(p_sent).send(Text{"abc"}, push_text);
    if (p_undelivered) return fail("a payload to a live peer came back");

    int reply = 0;
    std::move(q).branch(poll_label, [&](auto q_branch) {
        if constexpr (std::is_same_v<typename decltype(q_branch)::protocol, s::Recv<s::Crash<P>, s::End>>) {
            std::fprintf(stderr, "q took the crash branch of a live peer\n");
            std::abort();
        } else {
            auto [text, q_reply] = std::move(q_branch).recv(read_text);
            auto q_sel = std::move(q_reply).template select<0>(push_label);
            auto [q_end, q_lost] = std::move(q_sel).send(static_cast<int>(text.view().size()) + 39, push_int);
            if (q_lost) std::abort();
            (void)std::move(q_end).close();
        }
    });

    std::move(p_wait).branch(poll_label, [&](auto p_branch) {
        if constexpr (std::is_same_v<typename decltype(p_branch)::protocol, s::Recv<int, s::End>>) {
            auto [value, p_end] = std::move(p_branch).recv(read_int);
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
    auto p = s::mint_crash_session<ProtoP, P, Q>(bg_ctx(), Port{&to_p, &to_q}, cell_q, s::mint_crash_writer(cell_p));
    auto q = s::mint_crash_session<ProtoQ, Q, P>(bg_ctx(), Port{&to_q, &to_p}, cell_p, s::mint_crash_writer(cell_q));

    const Port q_port = std::move(q).crash(s::CrashCause::Throw);
    if (q_port.in != &to_q) return fail("crash() did not give back the resource");

    auto p_sent = std::move(p).select<0>(push_label);
    auto [p_wait, p_undelivered] = std::move(p_sent).send(Text{"abc"}, push_text);
    if (!p_undelivered || p_undelivered->view() != "abc") return fail("the lost payload did not come back");
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
    auto p = s::mint_crash_session<ProtoP, P, Q>(bg_ctx(), Port{&to_p, &to_q}, cell_q, s::mint_crash_writer(cell_p));
    auto q = s::mint_crash_session<ProtoQ, Q, P>(bg_ctx(), Port{&to_q, &to_p}, cell_p, s::mint_crash_writer(cell_q));

    auto p_sent = std::move(p).select<0>(push_label);
    auto [p_wait, p_undelivered] = std::move(p_sent).send(Text{"abc"}, push_text);
    if (p_undelivered) return fail("a payload to a live peer came back");
    (void)std::move(p_wait).crash(s::CrashCause::Abort);
    // The label and its payload are one message, and the session of p
    // wrote the count that the report of its crash carries.
    const std::optional<s::CrashWitness> p_witness = cell_p.witness();
    if (!p_witness || std::to_underlying(p_witness->messages_sent) != 1)
        return fail("the report did not carry the count that the session of p wrote");

    bool took_message = false;
    std::move(q).branch(poll_label, [&](auto q_branch) {
        if constexpr (std::is_same_v<typename decltype(q_branch)::protocol, s::Recv<s::Crash<P>, s::End>>) {
            std::fprintf(stderr, "q detected the crash before the queued message\n");
            std::abort();
        } else {
            auto [text, q_reply] = std::move(q_branch).recv(read_text);
            took_message = text.view() == "abc";
            auto q_sel = std::move(q_reply).template select<0>(push_label);
            auto [q_end, q_lost] = std::move(q_sel).send(42, push_int);
            took_message = took_message && q_lost.has_value();
            (void)std::move(q_end).close();
        }
    });
    if (!to_p.slots.empty()) return fail("a message reached the queue of a crashed peer");
    return took_message ? 0 : fail("q lost the message that was queued before the crash");
}

// ── Two threads ─────────────────────────────────────────────────────
//
// The decorator reads the crash cell between polls, and the peer writes
// its queue and then marks the cell on a different thread.  A message
// that the peer queued before the crash must still arrive (r-rcv-⊙),
// so the release of the cell must make every earlier write visible.

// A single-producer, single-consumer queue of wire words.  The producer
// owns head_ and the consumer owns tail_.  The capacity bounds one run,
// so the producer never waits.
class WireQueue {
public:
    static constexpr std::size_t capacity = 1024;

    void push(std::uint64_t word) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        slots_[head % capacity] = word;
        head_.store(head + 1, std::memory_order_release);
    }

    [[nodiscard]] std::optional<std::uint64_t> pop() noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (head_.load(std::memory_order_acquire) == tail) return std::nullopt;
        const std::uint64_t word = slots_[tail % capacity];
        tail_.store(tail + 1, std::memory_order_release);
        return word;
    }

private:
    std::array<std::uint64_t, capacity> slots_{};
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};

struct WirePort {
    WireQueue* in = nullptr;
    WireQueue* out = nullptr;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};

using StreamP = s::Loop<s::Select<s::Send<int, s::Continue>>>;
using StreamQ = s::Loop<s::Offer<s::Recv<int, s::Continue>, s::Recv<s::Crash<P>, s::End>>>;
using StreamQHandle = decltype(s::mint_crash_session<StreamQ, Q, P>(std::declval<const BgCtx&>(), WirePort{},
                                                                    std::declval<const s::PeerCrashCell&>(),
                                                                    std::declval<s::CrashWriter>()));

constexpr int kStreamMessages = 200;
constexpr int kStreamRuns = 50;
static_assert(2 * kStreamMessages <= static_cast<int>(WireQueue::capacity), "a run must fit in the queue");

// p streams its messages and crashes.  q, on a second thread, must
// receive every message in order, and then take the crash branch with
// the cause that p recorded.
int run_stream_across_threads() {
    for (int run = 0; run < kStreamRuns; ++run) {
        WireQueue to_p;
        WireQueue to_q;
        s::PeerCrashCell cell_p;
        s::PeerCrashCell cell_q;
        int received = 0;
        bool is_in_order = true;
        bool took_crash_branch = false;

        std::jthread receiver([&] {
            std::optional<StreamQHandle> q{
                s::mint_crash_session<StreamQ, Q, P>(bg_ctx(), WirePort{&to_q, &to_p}, cell_p, s::mint_crash_writer(cell_q))};
            bool is_done = false;
            while (!is_done) {
                StreamQHandle current = std::move(*q);
                q.reset();
                std::move(current).branch(
                    [](WirePort& port) noexcept -> std::optional<std::size_t> {
                        const auto word = port.in->pop();
                        if (!word) return std::nullopt;
                        return static_cast<std::size_t>(*word);
                    },
                    [&](auto branch) {
                        using Head = typename decltype(branch)::protocol;
                        if constexpr (s::is_crash_branch_v<Head>) {
                            auto [record, end] = std::move(branch).recv();
                            took_crash_branch = record.cause == s::CrashCause::Abort;
                            (void)std::move(end).close();
                            is_done = true;
                        } else {
                            auto [value, next] = std::move(branch).recv([](WirePort& port) noexcept -> std::optional<int> {
                                const auto word = port.in->pop();
                                if (!word) return std::nullopt;
                                return static_cast<int>(*word);
                            });
                            is_in_order = is_in_order && value == received;
                            ++received;
                            q.emplace(std::move(next));
                        }
                    });
            }
        });

        // The crash at the end also releases q if a send goes wrong, so
        // the join always returns.
        bool was_payload_returned = false;
        auto p = s::mint_crash_session<StreamP, P, Q>(bg_ctx(), WirePort{&to_p, &to_q}, cell_q, s::mint_crash_writer(cell_p));
        for (int message = 0; message < kStreamMessages; ++message) {
            auto chosen = std::move(p).select<0>([](WirePort& port, std::size_t label) noexcept {
                port.out->push(label);
                return true;
            });
            auto [next, undelivered] = std::move(chosen).send(message, [](WirePort& port, int& value) noexcept {
                port.out->push(static_cast<std::uint64_t>(value));
                return true;
            });
            was_payload_returned = was_payload_returned || undelivered.has_value();
            p = std::move(next);
        }
        (void)std::move(p).crash(s::CrashCause::Abort);
        receiver.join();

        if (was_payload_returned) return fail("a payload to a live peer came back");
        if (received != kStreamMessages) return fail("q lost a message that p queued before the crash");
        if (!is_in_order) return fail("q received the messages out of order");
        if (!took_crash_branch) return fail("q did not take the crash branch with the recorded cause");
    }
    return 0;
}

// The cell keeps the one report of its one reporter, with its cause and
// the count that the session of the endpoint wrote.  An endpoint with no
// session sent nothing, so its report carries the count zero.
int run_cell() {
    s::PeerCrashCell cell;
    if (cell.has_crashed() || cell.crash_cause() || cell.witness()) return fail("a fresh cell reports a crash");
    if (!s::mint_crash_reporter(cell).report(s::CrashCause::ErrorReturn)) return fail("the first report was refused");
    const std::optional<s::CrashWitness> witness = cell.witness();
    if (!witness || witness->cause != s::CrashCause::ErrorReturn || std::to_underlying(witness->messages_sent) != 0)
        return fail("the report did not keep its cause and the count of the session");
    if (cell.crash_cause() != s::CrashCause::ErrorReturn) return fail("the cause did not match the report");
    return 0;
}

}  // namespace

int main() {
    if (const int rc = run_cell(); rc != 0) return rc;
    if (const int rc = run_without_crash(); rc != 0) return rc;
    if (const int rc = run_receiver_crashes_first(); rc != 0) return rc;
    if (const int rc = run_sender_crashes_after_send(); rc != 0) return rc;
    if (const int rc = run_stream_across_threads(); rc != 0) return rc;
    return 0;
}
