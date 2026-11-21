#pragma once

// Crash transport: one decorator that runs a session handle under the
// crash-stop semantics of fixy/session/Crash.h.
//
// CrashWatched<Handle, Self, Peer, Reliable, Ctx> wraps any session
// handle, and keeps the context of its mint.  Each operation forwards to
// the handle and wraps the successor, so the crash semantics hold at every
// step.  The operations follow LMCS 2025 Fig. 4:
//
//   send, select  If the peer has crashed, the message is lost (rule
//                 r-send-↯).  The decorator does not call the transport.
//                 A payload that was not delivered comes back to the
//                 caller.  A payload that carries a permission needs a
//                 transport that refuses at the write, because the peer
//                 can crash after the check and before the write.
//   branch        The decorator polls for a label.  If the peer has
//                 crashed and no message from the peer is left, it takes
//                 the crash branch (rule r-rcv-⊙).  A message that arrived
//                 before the crash is delivered first.
//   recv          A crash branch head Recv<Crash<Peer>, K> gives the
//                 crash record from the detector.  Any other reception
//                 reads with no wait and watches the peer between reads.
//                 The mint proved that such a reception is from a reliable
//                 peer, or is the payload of a label that already arrived,
//                 so a crash there has no branch to take, and the wait
//                 ends the process with a diagnostic.
//   crash         The local endpoint stops (rule r-↯).  Only a role
//                 outside the reliable set may do this.  The peer learns
//                 of it through the cell that the call marks.  A crash
//                 never falls between a label and the payload of its
//                 branch, because the two are one message.
//
// The handle records where it stands inside a message: a label whose
// branch opens with a send or a reception leaves the payload still to
// go.  The position is a type, so crash() refuses the split at compile
// time, and recv() knows which role sends the payload it waits for.
//
// Every read and every poll returns a fixy::Option.  A transport that
// returns the value itself waits inside the transport, where no crash is
// seen, so the exact return type refuses it.
//
// The detector is a CrashCell<Sender, Receiver>.  One cell watches one
// pair: the crash of Sender as Receiver learns it, and the messages of
// Sender to Receiver.  The session of Sender writes the count of those
// messages into the cell, through the CrashWriter that its mint consumed.
// The report of a crash comes from the one CrashReporter of the cell, held
// by the Keeper's failure detector, or from the endpoint itself when it
// crashes.  The session of Receiver reads the cell.
//
// ── The witness of a crash ──────────────────────────────────────────
//
// A crash report carries the number of messages that the peer sent on
// the session before it stopped.  The decorator counts the messages it
// receives.  A positional label with the payload of its branch counts as
// one, and a keyed label with the value of its payload counts as one.
// Rule r-rcv-⊙ (LMCS 2025, Fig. 4) takes the crash branch when the peer
// has crashed and no message of the peer is left in the queue.  The count
// makes "no message left" a fact the decorator checks: when the received
// count reaches the reported count, the queue is empty, and a message
// that the transport reports past that point was never sent.  The
// decorator refuses such a message and takes the crash branch.  While the
// received count is below the reported count, a message is still owed,
// and the decorator waits for it instead of taking the crash branch.
//
// The count has one source: the endpoint that sent the messages.  The
// cell holds it in one word, and the session adds one to the word with
// one read-modify-write after each message that it finished.  A report
// reads the count from that word, so neither the failure detector nor a
// transport chooses it, and the transport cannot write the witness at all.
//
// The model is crash-stop, and the report is a fence.  A failure detector
// can report an endpoint that is still alive.  The report sets a fence
// bit in the count word with one fetch_or, and publishes the count that
// the same operation read.  The session reads the bit from the result of
// each add.  A message that it finished after the fence is past the
// reported count.  A peer that had not received it before the report
// refuses it, and the session ends the process with
// Crash_Endpoint_Reported, so no endpoint goes on after its report.  A
// peer that received it before the report has a count above the reported
// count, and it takes the crash branch at its next reception.  The add is
// one locked operation for each message, on a crash-watched session only.
//
// A message counts when it is whole.  A label whose branch opens with a
// send, and a keyed label word with a value, are half of their message,
// and the send of the payload counts it.  A report between the label and
// the payload did not count the message of the label.  The count after the
// payload reads the fence, so the session stops there, and the peer takes
// its crash branch: the label and the payload that the transport reports
// are past the count.
//
// A keyed message names its receiver, and a protocol can send to a
// reliable third role as well as to the channel peer.  So the session
// holds one cell for each pair of this endpoint and a receiver, and a
// message counts in the cell of its own receiver.  The type of a cell
// names its pair, and the mint takes the cell of the pair of the peer and
// this endpoint, the writer of the pair of this endpoint and the peer, and
// the writer of the pair of this endpoint and the third role.  A third
// role that watches this endpoint reads the cell of its own pair, which
// counts the messages to it and no others.
//
// A send to the peer reads the cell of the peer, and a message to a
// crashed peer is lost.  A third role is reliable, as the crash walk
// demands of each role that the protocol names, so a send to it reads no
// crash and its message always goes.
//
// A report on any pair cell of this endpoint fences the endpoint.  The
// count of the next message that it finishes, to either receiver, reads
// the fence of the cell of that receiver and the report of the other pair
// cell, and stops the process.  A report fences each pair cell that it
// writes, and the failure detector reports an endpoint on each of its pair
// cells.
//
// ── One class for every head ────────────────────────────────────────
//
// One class handles every head, and the crash branch is what runs on a
// crash.  The one crash walk that gates the mint refuses a combinator
// that it does not know.
//
// ── What the transport is trusted with ──────────────────────────────
//
// After a crash, the witness bounds what the transport can report: no
// message past the reported count reaches the endpoint.  Before the
// crash is reported, a message is only what the transport says it is,
// and a transport that invents messages then is Byzantine, which the
// crash-stop model excludes.  Two trusts stay.  A trying write returns
// false when the queue of the peer is closed: a write that the transport
// reports as taken and that the peer never reads is the loss that rule
// r-↯ defines for a queue into a crashed peer, so the report and the loss
// look the same.  And the transport delivers a label and the payload of
// its branch together: a split shows as Crash_Splits_A_Message.  A
// transport that shows a label to the peer before its payload breaks this
// trust.  If the peer reads that label, and the sender then dies before
// the payload, the peer ends with Crash_Splits_A_Message.
//
// Every transport of a crash session returns at once.  A write tries, and
// a read polls, so every wait of the decorator is its own wait through the
// watch of fixy/session/Watch.h, and a crash ends it.
//
// A payload that the transport took before the crash and that the peer
// never read is lost with the queue of the peer (rule r-↯ makes that
// queue unavailable).  A transport that drains a closed queue back to its
// senders returns such payloads too.

#include <fixy/session/Crash.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>

#include <foundation/NoObject.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Armed.h>
#include <foundation/contracts/Pre.h>

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <meta>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

// The row-hash identity of a crash-watched handle, declared and never
// defined.  foundation/diag/RowHash.h folds it with the inner handle, so
// a crash-watched handle and the plain handle that it wraps take two
// cache slots.
namespace fixy::row_discipline {
template <typename Self, typename Peer, typename Reliable, typename Position>
struct crash_watched;
}  // namespace fixy::row_discipline

namespace fixy::session {

// ── The crash detector cell ──────────────────────────────────────────

// The number of messages that one endpoint sent to its peer on one
// session.  A label and the payload of its branch count as one message.
enum class MessageCount : std::uint64_t {
};

// What a crash report says: how the peer stopped, and how many messages
// it sent before it stopped.
struct CrashWitness {
    CrashCause cause = CrashCause::Unknown;
    MessageCount messages_sent{};
};

class CrashReporter;
template <typename Sender, typename Receiver>
class CrashWriter;
template <typename Sender, typename Receiver>
class CrashCell;
class CrashSessionDoor;

// The receiver of a crash session that sends to no role but its channel
// peer.  The writer of a pair with this receiver names no cell, and the
// mint takes it in place of the writer of a third pair.
struct NoThirdRole {};

// The two roles of a crash cell: the endpoint that sends, and the
// endpoint that receives and reads the cell.  A role is not its own peer,
// and NoThirdRole is no role.
template <typename Sender, typename Receiver>
concept CrashPairRoles =
    !std::is_same_v<Sender, Receiver> && !std::is_same_v<Sender, NoThirdRole> && !std::is_same_v<Receiver, NoThirdRole>;

// The part of a pair cell that names no role.  Only CrashCell builds one,
// so each core is the core of one pair.  It has two words, each on its own
// cache line.
//
//   - The report: the cause in the top byte, the message count in the low
//     56 bits, or all ones while the sender is alive.  A cause is at most
//     crash_cause_top_value, so no report is all ones.  The report is one
//     compare-and-swap, so the first report wins whole, and a reader never
//     sees the cause of one report beside the count of another.
//   - The count: the messages that the sender finished sending to the
//     receiver, in the low 56 bits, and the fence bit in the top bit.  Only
//     the session of the sender adds to it, and a report sets the fence
//     (the witness of a crash, above).
class CrashCellCore : public ::foundation::Pinned<CrashCellCore> {
    static constexpr std::uint64_t alive_state = ~std::uint64_t{0};
    static constexpr unsigned count_bits = 56;
    static constexpr std::uint64_t count_mask = (std::uint64_t{1} << count_bits) - 1;
    static constexpr std::uint64_t fence_bit = std::uint64_t{1} << 63;

    alignas(64) std::atomic<std::uint64_t> state_{alive_state};
    // Set when the one reporter of this cell is minted.
    std::atomic<bool> has_reporter_{false};
    alignas(64) std::atomic<std::uint64_t> sent_{0};
    // Set when the one writer of this cell is minted.
    std::atomic<bool> has_writer_{false};

    friend class CrashReporter;
    template <typename Sender, typename Receiver>
    friend class CrashWriter;
    template <typename Sender, typename Receiver>
    friend class CrashCell;
    friend class CrashSessionDoor;

    constexpr CrashCellCore() noexcept = default;

    // Sets the fence and publishes the count that the fence read.  Returns
    // true when this report is the first.
    bool publish_(CrashCause cause) noexcept {
        const std::uint64_t counted = sent_.fetch_or(fence_bit, std::memory_order_acq_rel) & count_mask;
        const std::uint64_t packed = (std::uint64_t{static_cast<std::uint8_t>(cause)} << count_bits) | counted;
        std::uint64_t expected = alive_state;
        return state_.compare_exchange_strong(expected, packed, std::memory_order_acq_rel, std::memory_order_acquire);
    }

    // Counts one more finished message.  Returns false when a report set
    // the fence before this message, so the message is past the count that
    // the receiver reads.
    [[nodiscard]] bool count_one_sent_() noexcept {
        const std::uint64_t before = sent_.fetch_add(1, std::memory_order_acq_rel);
        CRUCIBLE_FATAL_INVARIANT((before & count_mask) != count_mask);
        return (before & fence_bit) == 0;
    }

public:
    // The largest message count a report can carry.
    static constexpr std::uint64_t max_message_count = count_mask;

    // User-provided, so the core is not trivially copyable.  GCC counts a
    // class whose copies are all deleted as trivially copyable, and
    // std::bit_cast or std::start_lifetime_as would then build a core with
    // no constructor.
    constexpr ~CrashCellCore() noexcept {}

    [[nodiscard]] bool has_crashed() const noexcept { return state_.load(std::memory_order_acquire) != alive_state; }

    // The report, or no value while the endpoint is alive.  Only the
    // reporter and the crash of the endpoint write the state, their
    // preconditions keep the cause inside CrashCause, and the count is 56
    // bits wide, so the decode is total over what can be read.
    [[nodiscard]] std::optional<CrashWitness> witness() const noexcept {
        const std::uint64_t state = state_.load(std::memory_order_acquire);
        if (state == alive_state) return std::nullopt;
        return CrashWitness{static_cast<CrashCause>(state >> count_bits),
                            static_cast<MessageCount>(state & count_mask)};
    }

    [[nodiscard]] std::optional<CrashCause> crash_cause() const noexcept {
        const std::optional<CrashWitness> report = witness();
        if (!report) return std::nullopt;
        return report->cause;
    }
};

// The cell of one pair: the crash of Sender as Receiver learns it, and the
// count of the messages of Sender to Receiver.  The session of Sender
// counts into it, and the session of Receiver reads it.  The type names the
// pair, so a session cannot read the cell of a pair that is not its own.
template <typename Sender, typename Receiver>
class CrashCell final : public CrashCellCore {
    static_assert(CrashPairRoles<Sender, Receiver>,
                  "fixy::session::diagnostic [Crash_Cell_Roles]: CrashCell<Sender, Receiver>: the two roles of a "
                  "crash cell are the same role, or one of them is NoThirdRole.  A cell watches the messages of "
                  "one endpoint to another.");

public:
    using sender_role = Sender;
    using receiver_role = Receiver;

    constexpr CrashCell() noexcept = default;
};

namespace detail::crash_transport {

[[noreturn]] [[gnu::cold, gnu::noinline]] inline void abort_on_second_reporter() noexcept {
    std::fprintf(stderr,
                 "fixy::session: diagnostic [Crash_Reporter_Twice]: a crash reporter was minted for a cell that "
                 "already has one.  A cell has one reporter.  Give the one reporter to the failure detector.\n");
    std::abort();
}

[[noreturn]] [[gnu::cold, gnu::noinline]] inline void abort_on_second_writer() noexcept {
    std::fprintf(stderr,
                 "fixy::session: diagnostic [Crash_Writer_Twice]: a crash writer was minted for a cell that already "
                 "has one.  One cell watches one endpoint, and only the session of that endpoint counts its "
                 "messages.  Give each crash session a cell of its own.\n");
    std::abort();
}

[[noreturn]] [[gnu::cold, gnu::noinline]] inline void abort_on_moved_writer() noexcept {
    std::fprintf(stderr,
                 "fixy::session: diagnostic [Crash_Writer_Moved_From]: mint_crash_session took a crash writer that "
                 "names no cell, because a move took its cell before.  Pass the writer that mint_crash_writer "
                 "gave, by move, to one mint.\n");
    std::abort();
}

[[noreturn]] [[gnu::cold, gnu::noinline]] inline void abort_on_reported_endpoint() noexcept {
    std::fprintf(stderr,
                 "fixy::session: diagnostic [Crash_Endpoint_Reported]: this endpoint finished a message after a "
                 "report of its crash on one of its pair cells.  The report is a fence: the receiver reads the count "
                 "of messages that the report took, and it never takes a message past it.  Under crash-stop a "
                 "reported endpoint does not go on, so the process stops.\n");
    std::abort();
}

}  // namespace detail::crash_transport

// The one reporter of one cell.  Only mint_crash_reporter builds it, and
// each cell has one, so a transport that holds the cell cannot write a
// report: it would need the reporter, and the reporter is held by the
// failure detector.  A report consumes the reporter, because an endpoint
// crashes once.  It carries no count: the cell reads the count that the
// session of the sender wrote.  The failure detector holds one reporter
// for each pair cell of an endpoint.
//
// The move constructor is user-provided, so the class is not trivially
// copyable and std::bit_cast cannot build one.  It has no trivial
// constructor, so it is not an implicit-lifetime type and no lifetime
// start over bytes can make one.
class [[nodiscard]] CrashReporter {
    CrashCellCore* cell_ = nullptr;

    explicit CrashReporter(CrashCellCore& cell) noexcept : cell_{&cell} {}

    friend CrashReporter mint_crash_reporter(CrashCellCore& cell) noexcept;

public:
    CrashReporter(CrashReporter&& other) noexcept : cell_{std::exchange(other.cell_, nullptr)} {}
    CrashReporter(const CrashReporter&) = delete("a cell has one reporter, so a report has one author");
    CrashReporter& operator=(const CrashReporter&) = delete("a cell has one reporter, so a report has one author");
    CrashReporter& operator=(CrashReporter&&) = delete("a reporter names one cell for its whole life");
    ~CrashReporter() = default;

    // Publishes the crash with its cause and the count that the session of
    // the sender wrote, and fences the count.  Returns true when this is
    // the first report of the cell.
    bool report(CrashCause cause) && noexcept {
        CRUCIBLE_PRE(cell_ != nullptr);
        CRUCIBLE_PRE(static_cast<std::uint8_t>(cause) <= crash_cause_top_value);
        CrashCellCore* const cell = std::exchange(cell_, nullptr);
        return cell->publish_(cause);
    }

private:
    [[nodiscard]] static bool claim_(CrashCellCore& cell) noexcept {
        return !cell.has_reporter_.exchange(true, std::memory_order_acq_rel);
    }
};

// The one writer of the count of one pair cell.  Only mint_crash_writer
// builds it, and each cell has one.  mint_crash_session takes it by value,
// so no two sessions count into one cell: the writer is move-only, and a
// writer that a move emptied stops the mint with Crash_Writer_Moved_From.
// The type names the pair, so the mint takes the writer of each pair of
// its own endpoint and no other.  The writer of a pair with NoThirdRole is
// the one writer that names no cell, and anyone can build it.
template <typename Sender, typename Receiver>
class [[nodiscard]] CrashWriter {
    CrashCellCore* cell_ = nullptr;

    explicit CrashWriter(CrashCellCore& cell) noexcept : cell_{&cell} {}

    template <typename S, typename R>
        requires CrashPairRoles<S, R>
    friend auto mint_crash_writer(CrashCell<S, R>& cell) noexcept -> CrashWriter<S, R>;
    friend class CrashSessionDoor;

public:
    using sender_role = Sender;
    using receiver_role = Receiver;

    constexpr CrashWriter() noexcept
        requires std::is_same_v<Receiver, NoThirdRole>
    = default;
    CrashWriter(CrashWriter&& other) noexcept : cell_{std::exchange(other.cell_, nullptr)} {}
    CrashWriter(const CrashWriter&) = delete("one session counts into a cell, so the cell has one writer");
    CrashWriter& operator=(const CrashWriter&) = delete("one session counts into a cell, so the cell has one writer");
    CrashWriter& operator=(CrashWriter&&) = delete("a writer names one cell for its whole life");
    ~CrashWriter() = default;

private:
    [[nodiscard]] static bool claim_(CrashCellCore& cell) noexcept {
        return !cell.has_writer_.exchange(true, std::memory_order_acq_rel);
    }
};

// Mints the one writer of a pair cell.  A second mint for the same cell
// ends the process with Crash_Writer_Twice.
template <typename Sender, typename Receiver>
    requires CrashPairRoles<Sender, Receiver>
[[nodiscard]] inline auto mint_crash_writer(CrashCell<Sender, Receiver>& cell) noexcept
    -> CrashWriter<Sender, Receiver> {
    if (!CrashWriter<Sender, Receiver>::claim_(cell)) [[unlikely]]
        detail::crash_transport::abort_on_second_writer();
    return CrashWriter<Sender, Receiver>{cell};
}

template <typename Sender, typename Receiver>
void mint_crash_writer(const CrashCell<Sender, Receiver>&) noexcept = delete(
    "[Crash_Writer_From_Const_Cell] a const cell is a view of the detector, and a view gives no right to count "
    "the messages of an endpoint.  Mint the writer from the cell of the pair of this endpoint and its receiver.");

// Mints the one reporter of a cell.  A second mint for the same cell
// ends the process with Crash_Reporter_Twice.
[[nodiscard]] inline CrashReporter mint_crash_reporter(CrashCellCore& cell) noexcept {
    if (!CrashReporter::claim_(cell)) [[unlikely]]
        detail::crash_transport::abort_on_second_reporter();
    return CrashReporter{cell};
}

// A const cell is the survivor's view of the detector.  It gives no right
// to report.
void mint_crash_reporter(const CrashCellCore&) noexcept = delete(
    "[Crash_Reporter_From_Const_Cell] a const cell is a view of the detector, and a view gives no right to "
    "report a crash.  Mint the reporter from the cell that the failure detector owns.");

namespace detach_reason {

// The local endpoint stopped under crash-stop semantics.  The peer
// learns of it through its detector.
struct LocalCrashStop : tag_base {};

}  // namespace detach_reason

// ── The sender watch ─────────────────────────────────────────────────
//
// One cell watches one role.  The crash walk of fixy/session/Crash.h
// refuses a protocol that names an unreliable role other than the watched
// peer: the sender of an Offer, or the role of a keyed step.

namespace detail::crash_transport {

// The role that a keyed step names: the sender of a keyed reception, and
// the receiver of a keyed send.  A step that names no role is a step with
// the channel peer.  The mint admits the named role only when it is the
// peer or reliable.
[[nodiscard]] consteval std::meta::info role_of_step(std::meta::info step, std::meta::info peer) {
    const std::meta::info role =
        crash::named_role(crash::tr::decompose(protocol_registry, crash::tr::strip_wrappers(protocol_registry, step)));
    return role == std::meta::info{} ? peer : role;
}

template <typename P, typename Peer>
using keyed_sender_t = typename[:role_of_step(^^P, ^^Peer):];

template <typename P, typename Peer>
using keyed_receiver_t = typename[:role_of_step(^^P, ^^Peer):];

// The role other than the channel peer that a send of the protocol names:
// the receiver of a keyed send, or of a branch of a keyed Select.  The null
// reflection when the protocol sends to no such role.  The context is the
// channel peer.  The fold reads the spine of the protocol, as the walks of
// fixy/session/Flow.h do, and it gives the first such role of a walk that
// reads a node first, then its continuation, then each branch.  Admission
// refuses a local type that names two roles (NamesAtMostOnePeer of
// fixy/session/Admission.h), so the protocol names one such role at most.
struct third_receiver_algebra {
    using result = std::meta::info;
    using context = std::meta::info;

    template <class Child>
    consteval result terminal(const crash::tr::node&, context, const Child&) const {
        return {};
    }
    template <class Child>
    consteval result back(const crash::tr::node&, context, const Child&) const {
        return {};
    }
    template <class Child>
    consteval result step(const crash::tr::node& view, context channel_peer, const Child& child) const {
        if (view.entry.direction == crash::tr::polarity::output) {
            const std::meta::info role = crash::named_role(view);
            if (role != std::meta::info{} && std::meta::dealias(role) != channel_peer) return std::meta::dealias(role);
        }
        return child(view.next, channel_peer);
    }
    template <class Child>
    consteval result choice(const crash::tr::node& view, context channel_peer, const Child& child) const {
        for (const std::meta::info branch : view.branches) {
            const std::meta::info found = child(branch, channel_peer);
            if (found != std::meta::info{}) return found;
        }
        return {};
    }
    template <class Child>
    consteval result binder(const crash::tr::node& view, context channel_peer, const Child& child) const {
        return child(view.next, channel_peer);
    }
    template <class Child>
    consteval result wrapper(const crash::tr::node& view, context channel_peer, const Child& child) const {
        return child(view.next, channel_peer);
    }
    template <class Child>
    consteval result marker(const crash::tr::node& view, context channel_peer, const Child& child) const {
        return child(view.next, channel_peer);
    }
    template <class Child>
    consteval result named(const crash::tr::node& view, context channel_peer, const Child& child) const {
        return child(view.next, channel_peer);
    }
    consteval result unregistered(const crash::tr::node&, context) const { return {}; }
};

// Complexity: one fold, linear in the distinct nodes of the protocol.
[[nodiscard]] consteval std::meta::info third_receiver_of(std::meta::info proto, std::meta::info peer) {
    ::fixy::session::detail::require_acyclic_names(proto);
    return crash::tr::fold(protocol_registry, proto, third_receiver_algebra{}, std::meta::dealias(peer));
}

// True when Third is the third receiver of the protocol, or NoThirdRole
// when the protocol sends to no role but the channel peer.
[[nodiscard]] consteval bool is_third_receiver(std::meta::info proto, std::meta::info peer, std::meta::info third) {
    const std::meta::info found = third_receiver_of(proto, peer);
    const std::meta::info named = std::meta::dealias(third);
    return found == std::meta::info{} ? named == ^^NoThirdRole : named == found;
}

// The number of message branches of an Offer.  Rule 6 puts the crash
// branches last, so this is also the index of the first crash branch.
template <typename OfferType>
struct message_branch_count;
template <typename... Bs>
struct message_branch_count<Offer<Bs...>>
    : std::integral_constant<std::size_t, (std::size_t{!is_crash_branch_v<Bs>} + ... + std::size_t{0})> {};
template <typename Role, typename... Bs>
struct message_branch_count<Offer<Sender<Role>, Bs...>> : message_branch_count<Offer<Bs...>> {};

template <typename OfferType>
struct sender_of {
    using type = offer_sender_t<OfferType>;
};

// Each endpoint names its own reliable set, so two endpoints can
// disagree: this one counts the peer reliable, and the peer crashes.
// The reception has no crash branch to take, and a wait would last for
// ever.
[[noreturn]] [[gnu::cold, gnu::noinline]] inline void abort_on_reliable_peer_crash() noexcept {
    std::fprintf(stderr, "fixy::session: diagnostic [Crash_Of_Reliable_Peer]: the peer crashed, and this endpoint "
                         "counts it reliable, so the reception has no crash branch and would wait for ever.  The two "
                         "endpoints disagree about the reliable set.  Give both the same set, or remove the peer "
                         "from it and add the crash branch.\n");
    std::abort();
}

// The peer sent a label and crashed before the payload of its branch.  A
// label and that payload are one message, so the transport split it, and
// the branch that the label entered has no crash branch.
[[noreturn]] [[gnu::cold, gnu::noinline]] inline void abort_on_split_message() noexcept {
    std::fprintf(stderr, "fixy::session: diagnostic [Crash_Splits_A_Message]: the peer crashed after a label and "
                         "before the payload of its branch.  The label and the payload are one message, so the "
                         "transport delivered half of one.  Deliver a label and the payload of its branch together.\n");
    std::abort();
}

// Where a handle stands inside one message.  A label and the payload that
// heads its branch are one message (fixy/session/Crash.h), so a handle
// that holds the label and not yet the payload is inside a message.  A
// keyed label word and the value of its payload are one message too, so
// a handle at the value step of a keyed message is inside a message
// (fixy/session/Transport.h).  A keyed message with a payload of void is
// its label word alone.
struct between_messages {};

// This endpoint picked a label whose branch opens with a send, or it sent
// the label word of a keyed message with a value.  The payload still has
// to go, to Receiver.
template <typename Receiver>
struct payload_to_send {
    using receiver = Receiver;
};

template <typename Position>
inline constexpr bool is_payload_to_send_v = false;
template <typename Receiver>
inline constexpr bool is_payload_to_send_v<payload_to_send<Receiver>> = true;

// The role that receives the payload a send moves: the receiver of the
// label for a payload, and the channel peer for a bare send.
template <typename Position, typename Peer>
struct payload_receiver {
    using type = Peer;
};
template <typename Receiver, typename Peer>
struct payload_receiver<payload_to_send<Receiver>, Peer> {
    using type = Receiver;
};

// This endpoint took a label whose branch opens with a reception, or it
// read the label word of a keyed message with a value.  The payload still
// has to come, from Sender.
template <typename Sender>
struct payload_to_receive {
    using sender = Sender;
};

// The role that sends the payload a reception waits for: the sender of
// the label for a payload, and the channel peer for a bare reception.
template <typename Position, typename Peer>
struct payload_sender {
    using type = Peer;
};
template <typename Sender, typename Peer>
struct payload_sender<payload_to_receive<Sender>, Peer> {
    using type = Sender;
};

// The messages that the decorator counted on its session, one count for
// each direction.  A label and the payload of its branch are one message.
// A sent message counts when its last part goes, and a received message
// counts at its first part.  The counts travel from each handle to the
// next.
struct message_counts {
    std::uint64_t received = 0;
    std::uint64_t sent = 0;
};

// True when the peer has crashed and every message that its report counts
// has arrived.  The queue from the peer is then empty, which is the side
// condition of rule r-rcv-⊙, and a message past this point was never sent.
[[nodiscard]] inline bool queue_is_drained(const std::optional<CrashWitness>& witness,
                                           std::uint64_t received) noexcept {
    return witness.has_value() && received >= std::to_underlying(witness->messages_sent);
}

// Waits for a payload that `read` returns, and watches the peer between
// reads.
//
// A bare reception is a new message.  It is read only while the report,
// if any, says that one is still owed, so a message that the transport
// reports past the count is refused.  When the count is reached, no
// message can come.  The mint admits a bare reception only from a
// reliable peer, so the protocol gives no crash branch, and the wait ends
// the process with Crash_Of_Reliable_Peer.
//
// The payload of a label that arrived is part of that message, so the
// peer sent it.  When the peer has crashed and a last read finds nothing,
// the transport split the message, and the wait ends the process with
// Crash_Splits_A_Message.
//
// A payload from a role that the cell does not watch waits without the
// check: the mint admits such a role only when it is reliable.  Sender is
// the role that sends the payload: the sender of the label for a payload,
// the role that a keyed reception names, and the channel peer otherwise.
//
// The first read that finds nothing opens a watch::wait_scope on the
// endpoint of the session, so the watch sees the wait and checks it
// against the priority order.  A read that finds its payload at once
// never touches the watch.
template <typename Position, typename Peer, typename Sender, typename Read, typename Resource>
[[nodiscard]] constexpr auto await_payload(Read& read, Resource& resource, const CrashCellCore& cell,
                                           std::uint64_t received, watch::endpoint_id endpoint) {
    constexpr bool is_new_message = std::is_same_v<Position, between_messages>;
    std::optional<watch::wait_scope> wait;
    for (;;) {
        if constexpr (!std::is_same_v<Sender, Peer>) {
            auto payload = std::invoke(read, resource);
            if (payload.is_some()) return payload;
        } else if constexpr (is_new_message) {
            if (queue_is_drained(cell.witness(), received)) abort_on_reliable_peer_crash();
            auto payload = std::invoke(read, resource);
            if (payload.is_some()) return payload;
        } else {
            auto payload = std::invoke(read, resource);
            if (payload.is_some()) return payload;
            if (cell.has_crashed()) {
                auto last_payload = std::invoke(read, resource);
                if (last_payload.is_some()) return last_payload;
                abort_on_split_message();
            }
        }
        if (!wait) wait.emplace(endpoint);
        wait->poll();
    }
}

[[noreturn]] [[gnu::cold, gnu::noinline]] inline void abort_on_crash_label(std::size_t word,
                                                                           std::size_t message_branches) noexcept {
    std::fprintf(stderr,
                 "fixy::session: crash transport received the wire word %zu, which names no message branch of "
                 "the Offer (it has %zu message branches).  A crash branch is no label, so no peer can send its "
                 "word.  The two endpoints disagree about the protocol.\n",
                 word, message_branches);
    std::abort();
}

// True when the word names a message branch of the Offer P: a label
// branch, in the reading of fixy/session/Transport.h.  Message branches
// stand before the crash branches, so their indices are the first ones.
template <typename P>
[[nodiscard]] constexpr bool names_message_branch(std::size_t word) noexcept {
    const std::size_t index = branch_of_wire_word<P>(word);
    return index != no_branch && index < message_branch_count<P>::value;
}

}  // namespace detail::crash_transport

// Whether a crash-aware session can run Proto as Self, on a channel to
// Peer, with these reliable roles.  Each clause is its own atomic
// constraint, so a refusal names the clause that failed.
template <typename Proto, typename Self, typename Peer, typename Reliable>
concept CrashSessionAdmissible =
    is_reliable_set<Reliable>::value && !std::is_same_v<Self, Peer> && WellFormedRunnableProtocol<Proto>
    && PermissionFlowCloses<Proto, ::foundation::permissions::EmptyPermSet> && is_crash_well_formed_v<Proto>
    && every_reception_handles_crash_v<Proto, Peer, Reliable>
    && detail::crash::is_every_sender_watched_v<Proto, Peer, Reliable> && detail::crash::is_delegation_free_v<Proto>;

// The whole gate of mint_crash_session: an admissible crash session, a
// Resource that a mint admits, and a context that admits the effect row of
// the protocol (CtxAdmitsProtocolRow of fixy/session/Admission.h), as the
// context of mint_session does.  The decorator keeps a copy of the
// context, and enters each crash branch with it through
// HandleFactory::recover, so the context has a copy that cannot fail.
// Third is the third role that the protocol sends to, or NoThirdRole, so
// the mint takes the writer of each pair that a message of the session
// counts in.
template <typename Ctx, typename Proto, typename Self, typename Peer, typename Reliable, typename Resource,
          typename Third>
concept CtxFitsCrashSession = CrashSessionAdmissible<Proto, Self, Peer, Reliable> && PlainSessionResource<Resource>
                           && CtxAdmitsProtocolRow<Ctx, Proto> && std::is_nothrow_copy_constructible_v<Ctx>
                           && detail::crash_transport::is_third_receiver(^^Proto, ^^Peer, ^^Third);

// The same question as one type, so that the predicate below takes one
// argument and can hold an armed cell.
template <typename Proto, typename Self, typename Peer, typename Reliable>
struct CrashSession {};

namespace detail::crash_transport {

// True when the question is a CrashSession that CrashSessionAdmissible
// admits.  Any other type answers false.  A function at namespace scope
// that is not a template, so no translation unit can specialize the answer.
[[nodiscard]] consteval bool crash_session_admitted(std::meta::info question) {
    const std::meta::info asked = std::meta::dealias(question);
    if (!std::meta::is_type(asked) || !std::meta::has_template_arguments(asked)
        || std::meta::template_of(asked) != ^^CrashSession) {
        return false;
    }
    return std::meta::extract<bool>(
        std::meta::substitute(^^CrashSessionAdmissible, std::meta::template_arguments_of(asked)));
}

}  // namespace detail::crash_transport

template <typename Q>
using is_crash_session_admissible = std::bool_constant<detail::crash_transport::crash_session_admitted(^^Q)>;

// ── The decorator ────────────────────────────────────────────────────

template <typename Handle, typename Self, typename Peer, typename Reliable, typename Ctx,
          typename Position = detail::crash_transport::between_messages>
class CrashWatched;

// The result of a send.  `undelivered` holds the payload when the
// receiver had crashed before a try of the write.  It is empty when the
// transport took the payload.  For the label step of a keyed message, the
// payload is the label word.  The base Option holds a linear payload, such
// as a Transferable, so no assignment drops it.
template <typename Next, typename T>
struct [[nodiscard]] CrashSend {
    Next next;
    ::fixy::Option<T> undelivered;
};

namespace detail::crash_transport {

// The counts after a step that moved one more message the given way.
[[nodiscard]] constexpr message_counts one_more_sent(message_counts counts) noexcept {
    return {counts.received, counts.sent + 1};
}
[[nodiscard]] constexpr message_counts one_more_received(message_counts counts) noexcept {
    return {counts.received + 1, counts.sent};
}

// A trying write of a word, guarded by the cell of the receiver.  To a
// crashed receiver it writes nothing and reports the word taken, so the
// inner handle steps on.  `receiver_cell` is null for a reliable third
// role, whose crash this endpoint does not watch.  `is_written` says
// whether the transport took the word.
template <typename Resource, typename Transport>
[[nodiscard]] constexpr auto watched_word_write(const CrashCellCore* receiver_cell, Transport& transport,
                                                bool& is_written) noexcept {
    return [receiver_cell, &transport, &is_written](Resource& resource, std::size_t& word) noexcept(
               std::is_nothrow_invocable_v<Transport&, Resource&, std::size_t&>) -> bool {
        if (receiver_cell != nullptr && receiver_cell->has_crashed()) return true;
        is_written = std::invoke(transport, resource, word);
        return is_written;
    };
}

}  // namespace detail::crash_transport

// ── The door of the crash session ────────────────────────────────────
//
// The one class that builds a crash-watched handle and steps it.  The
// decorator befriends this class and no other, and no class template, so
// no specialization of the decorator reaches the constructor or the inner
// handle of another.  The class is final, and it is defined in this
// header, so a second definition in a translation unit that includes the
// header is a redefinition error.
//
// Each public member either states the whole gate of mint_crash_session
// (open), or takes a live decorator and does one complete step on it: it
// moves the message through the inner handle, and it wraps the successor.
// The decorator forwards each step here, so a direct call is the same
// operation as the method.  No object of the class exists.
class CrashSessionDoor final : ::foundation::NoObject<CrashSessionDoor> {
    // Wraps the successor of `from` with the counts after the step, and
    // records where it stands inside a message.
    template <typename NextPosition = detail::crash_transport::between_messages, typename Next, typename Handle,
              typename Self, typename Peer, typename Reliable, typename Ctx, typename Position>
    [[nodiscard]] static constexpr auto wrap_(const CrashWatched<Handle, Self, Peer, Reliable, Ctx, Position>& from,
                                              Next next, detail::crash_transport::message_counts counts) noexcept {
        return CrashWatched<Next, Self, Peer, Reliable, Ctx, NextPosition>{
            std::move(next), *from.peer_cell_, *from.own_cell_, from.third_cell_, from.ctx_, counts};
    }

    // The counts after one more message that this endpoint finished to
    // Receiver.  The cell of the pair of this endpoint and Receiver counts
    // the message too.  A report on either pair cell of this endpoint fences
    // it, so a report before this count stops the process (the witness of a
    // crash, in the head of this header).  The count of the cell of the
    // receiver reads its fence, and the other pair cell tells its report.
    // Only a message to the peer is in the counts of the decorator.
    template <typename Receiver, typename Handle, typename Self, typename Peer, typename Reliable, typename Ctx,
              typename Position>
    [[nodiscard]] static detail::crash_transport::message_counts
    one_more_sent_(const CrashWatched<Handle, Self, Peer, Reliable, Ctx, Position>& watched) noexcept {
        constexpr bool is_to_peer = std::is_same_v<Receiver, Peer>;
        CrashCellCore* const counted = is_to_peer ? watched.own_cell_ : watched.third_cell_;
        // The mint took the writer of the third pair when the protocol sends
        // to a third role, so a message to that role has its cell.
        CRUCIBLE_FATAL_INVARIANT(counted != nullptr);
        const CrashCellCore* const other = is_to_peer ? watched.third_cell_ : watched.own_cell_;
        const bool is_counted_before_a_report = counted->count_one_sent_();
        if (!is_counted_before_a_report || (other != nullptr && other->has_crashed())) [[unlikely]]
            detail::crash_transport::abort_on_reported_endpoint();
        if constexpr (is_to_peer) {
            return detail::crash_transport::one_more_sent(watched.counts_);
        } else {
            return watched.counts_;
        }
    }

    // The cell that tells whether Receiver crashed: the cell of the peer,
    // or null for a third role.  The crash walk admits a third role only
    // when it is reliable, so this endpoint watches no crash of it.
    template <typename Receiver, typename Handle, typename Self, typename Peer, typename Reliable, typename Ctx,
              typename Position>
    [[nodiscard]] static constexpr const CrashCellCore*
    receiver_cell_(const CrashWatched<Handle, Self, Peer, Reliable, Ctx, Position>& watched) noexcept {
        if constexpr (std::is_same_v<Receiver, Peer>) {
            return watched.peer_cell_;
        } else {
            return nullptr;
        }
    }

    // The cell that a writer names, taken out of the writer.  A writer that
    // a move emptied names no cell and stops the process.
    template <typename Sender, typename Receiver>
    [[nodiscard]] static CrashCellCore& take_cell_(CrashWriter<Sender, Receiver>& writer) noexcept {
        CrashCellCore* const cell = std::exchange(writer.cell_, nullptr);
        if (cell == nullptr) [[unlikely]]
            detail::crash_transport::abort_on_moved_writer();
        return *cell;
    }

public:
    // mint_crash_session: opens the plain handle of Proto, and puts around
    // it the decorator that reads the cell of the pair of the peer and this
    // endpoint, and counts into the cell of the pair of this endpoint and
    // each receiver.
    template <typename Proto, typename Self, typename Peer, typename Reliable, AbandonmentPolicy Policy, typename Ctx,
              typename Resource, typename Third>
        requires CtxFitsCrashSession<Ctx, Proto, Self, Peer, Reliable, Resource, Third>
    [[nodiscard]] static auto open(Ctx const& ctx, Resource resource, const CrashCell<Peer, Self>& peer_cell,
                                   CrashWriter<Self, Peer> own, CrashWriter<Self, Third> third,
                                   MintSite site) noexcept {
        CrashCellCore& own_cell = take_cell_(own);
        CrashCellCore* third_cell = nullptr;
        if constexpr (!std::is_same_v<Third, NoThirdRole>) third_cell = &take_cell_(third);
        auto inner = mint_session<Proto, Policy, Ctx, Resource>(ctx, std::forward<Resource>(resource), site);
        return CrashWatched<decltype(inner), Self, Peer, Reliable, Ctx>{std::move(inner), peer_cell, own_cell,
                                                                        third_cell, ctx};
    }

    template <typename Proto, typename Self, typename Peer, typename Reliable, AbandonmentPolicy Policy, typename Ctx,
              typename Resource, typename Third>
    static void open(Ctx const&, Resource, const CrashCell<Peer, Self>&&, CrashWriter<Self, Peer>,
                     CrashWriter<Self, Third>, MintSite) =
        delete("[Crash_Cell_Temporary] the detector cell is a temporary.  The decorator keeps its address, so the "
               "cell must outlive every handle of the session.");

    // crash(): stops the endpoint and reports it through each of its pair
    // cells, so each receiver that watches it learns of the crash.
    template <typename Handle, typename Self, typename Peer, typename Reliable, typename Ctx, typename Position>
    [[nodiscard]] static typename Handle::resource_type
    crash(CrashWatched<Handle, Self, Peer, Reliable, Ctx, Position>&& watched, CrashCause cause) noexcept {
        CRUCIBLE_PRE(static_cast<std::uint8_t>(cause) <= crash_cause_top_value);
        using Resource = typename Handle::resource_type;
        Resource resource = std::forward<Resource>(watched.inner_.resource());
        std::move(watched.inner_).detach(detach_reason::LocalCrashStop{});
        static_cast<void>(watched.own_cell_->publish_(cause));
        if (watched.third_cell_ != nullptr) static_cast<void>(watched.third_cell_->publish_(cause));
        return std::forward<Resource>(resource);
    }

    // ── send ─────────────────────────────────────────────────────────
    //
    // The decorator sends through the inner handle with a stop test that
    // reads the cell of the receiver before each try of the write.  When the
    // receiver has crashed, the payload goes back to the caller as
    // `undelivered`, so a token in it is not lost.
    template <typename Handle, typename Self, typename Peer, typename Reliable, typename Ctx, typename Position,
              typename Transport>
        requires is_send_v<typename Handle::protocol> && (!is_keyed_step_v<typename Handle::protocol>)
              && TryWrite<Transport, typename Handle::resource_type, typename Handle::protocol::message_type>
    [[nodiscard]] static constexpr auto send(CrashWatched<Handle, Self, Peer, Reliable, Ctx, Position>&& watched,
                                             detail::sent_value_t<typename Handle::protocol::message_type> value,
                                             Transport transport) {
        using T = typename Handle::protocol::message_type;
        using Resource = typename Handle::resource_type;
        // The payload of a keyed message goes to the role that the message
        // names, and each other payload goes to the channel peer.
        using Receiver = typename detail::crash_transport::payload_receiver<Position, Peer>::type;
        const CrashCellCore* const receiver_cell = receiver_cell_<Receiver>(watched);
        // The send with a stop test makes plain calls, and the closure gives
        // each shape of a trying write one.
        auto write = [transport = std::move(transport)](Resource& resource, T& payload) mutable noexcept(
                         std::is_nothrow_invocable_v<Transport&, Resource&, T&>) -> bool {
            return std::invoke(transport, resource, payload);
        };
        auto sent = HandleFactory::send(
            std::move(watched.inner_), std::move(value), std::move(write),
            [receiver_cell]() noexcept { return receiver_cell != nullptr && receiver_cell->has_crashed(); });
        // A message counts once it is whole: a bare message at its write, and
        // the message of a label at the write of its payload.  A payload that
        // did not go is no message.  It counts in the cell of the pair of
        // this endpoint and its receiver.
        const detail::crash_transport::message_counts counts =
            sent.kept.is_some() ? watched.counts_ : one_more_sent_<Receiver>(watched);
        auto wrapped = wrap_(watched, std::move(sent.next), counts);
        return CrashSend<decltype(wrapped), T>{std::move(wrapped), std::move(sent.kept)};
    }

    // A keyed Send writes its label word.  The successor then stands at the
    // value step, inside the message.  The word to a crashed peer is lost,
    // and `undelivered` then holds the word.  The message type of the step
    // is a tag that names the label, such as PeerMsg, and a unit can name
    // it with no definition, so the result does not hold it.
    template <typename Handle, typename Self, typename Peer, typename Reliable, typename Ctx, typename Position,
              typename Transport>
        requires is_send_v<typename Handle::protocol> && is_keyed_step_v<typename Handle::protocol>
              && TryWrite<Transport, typename Handle::resource_type, std::size_t>
    [[nodiscard]] static constexpr auto send(CrashWatched<Handle, Self, Peer, Reliable, Ctx, Position>&& watched,
                                             Transport transport) {
        using P = typename Handle::protocol;
        using Receiver = detail::crash_transport::keyed_receiver_t<P, Peer>;
        bool is_written = false;
        auto next = std::move(watched.inner_)
                        .send(detail::crash_transport::watched_word_write<typename Handle::resource_type>(
                            receiver_cell_<Receiver>(watched), transport, is_written));
        // A word with a value is half of its message, and the send of the
        // value counts the message.  A word with no value is the whole
        // message, and it counts in the cell of the pair of this endpoint and
        // its receiver.
        const detail::crash_transport::message_counts counts =
            (is_written && !keyed_step_has_value_v<P>) ? one_more_sent_<Receiver>(watched) : watched.counts_;
        auto wrapped = [&] {
            if constexpr (keyed_step_has_value_v<P>) {
                return wrap_<detail::crash_transport::payload_to_send<Receiver>>(watched, std::move(next), counts);
            } else {
                return wrap_(watched, std::move(next), counts);
            }
        }();
        ::fixy::Option<std::size_t> undelivered =
            is_written ? ::fixy::Option<std::size_t>{} : ::fixy::Option<std::size_t>::some(step_wire_word_v<P>);
        return CrashSend<decltype(wrapped), std::size_t>{std::move(wrapped), std::move(undelivered)};
    }

    // ── select ───────────────────────────────────────────────────────
    template <std::size_t I, typename Handle, typename Self, typename Peer, typename Reliable, typename Ctx,
              typename Position, typename Transport>
        requires is_select_v<typename Handle::protocol>
              && TryWrite<Transport, typename Handle::resource_type, std::size_t>
    [[nodiscard]] static constexpr auto select(CrashWatched<Handle, Self, Peer, Reliable, Ctx, Position>&& watched,
                                               Transport transport) {
        using P = typename Handle::protocol;
        using Branch = std::tuple_element_t<I, typename P::branches_tuple>;
        // The label of a keyed branch goes to the role that the branch names,
        // and a positional label goes to the channel peer.
        using Receiver = typename decltype([] {
            if constexpr (is_keyed_choice_v<P>) {
                return std::type_identity<detail::crash_transport::keyed_receiver_t<Branch, Peer>>{};
            } else {
                return std::type_identity<Peer>{};
            }
        }())::type;
        bool is_written = false;
        auto next = std::move(watched.inner_)
                        .template select<I>(detail::crash_transport::watched_word_write<typename Handle::resource_type>(
                            receiver_cell_<Receiver>(watched), transport, is_written));
        using Next = decltype(next);
        constexpr bool is_inside_message = [] {
            if constexpr (is_keyed_choice_v<P>) {
                return keyed_step_has_value_v<Branch>;
            } else {
                return is_send_v<typename Next::protocol>;
            }
        }();
        // A label with a payload to go is half of its message, and the send
        // of the payload counts the message.  A label alone is the whole
        // message, and it counts in the cell of the pair of this endpoint and
        // its receiver.
        const detail::crash_transport::message_counts counts =
            (is_written && !is_inside_message) ? one_more_sent_<Receiver>(watched) : watched.counts_;
        if constexpr (is_inside_message) {
            return wrap_<detail::crash_transport::payload_to_send<Receiver>>(watched, std::move(next), counts);
        } else {
            return wrap_(watched, std::move(next), counts);
        }
    }

    // ── recv ─────────────────────────────────────────────────────────
    //
    // A read is a polling read, fixy::Option<T>(Resource&).  A read of
    // another shape waits inside the transport, where no crash is seen, so
    // each reception refuses it.
    template <typename Handle, typename Self, typename Peer, typename Reliable, typename Ctx, typename Position,
              typename Read>
        requires is_recv_v<typename Handle::protocol> && (!is_keyed_step_v<typename Handle::protocol>)
              && (!is_crash_payload_v<typename Handle::protocol::message_type>)
    [[nodiscard]] static constexpr auto recv(CrashWatched<Handle, Self, Peer, Reliable, Ctx, Position>&& watched,
                                             Read read) {
        using T = typename Handle::protocol::message_type;
        using Resource = typename Handle::resource_type;
        static_assert(PollRead<Read, Resource, T>,
                      "fixy::session::diagnostic [Crash_Read_Must_Poll]: recv(): the read is not a polling read, "
                      "fixy::Option<T>(Resource&).  A read of another shape waits inside the transport, where no "
                      "crash is seen, so a dead peer blocks it for ever.  Return the payload when one is queued, and "
                      "no value otherwise.");
        using Sender = typename detail::crash_transport::payload_sender<Position, Peer>::type;
        ::fixy::Option<T> payload = detail::crash_transport::await_payload<Position, Peer, Sender>(
            read, watched.inner_.resource(), *watched.peer_cell_, watched.counts_.received,
            watched.inner_.watch_endpoint());
        auto [value, next] =
            std::move(watched.inner_)
                .recv([&payload](Resource&) noexcept(std::is_nothrow_move_constructible_v<T>) -> ::fixy::Option<T> {
                    return std::move(payload);
                });
        // A bare reception is a new message.  The payload of a label is part
        // of the label's message, which the branch counted.
        constexpr bool is_new_message = std::is_same_v<Position, detail::crash_transport::between_messages>;
        return std::pair{
            std::move(value),
            wrap_(watched, std::move(next),
                  is_new_message ? detail::crash_transport::one_more_received(watched.counts_) : watched.counts_)};
    }

    // A keyed Recv reads its label word.  The successor then stands at the
    // value step, inside the message, and its recv reads the value.
    template <typename Handle, typename Self, typename Peer, typename Reliable, typename Ctx, typename Position,
              typename Poll>
        requires is_recv_v<typename Handle::protocol> && is_keyed_step_v<typename Handle::protocol>
    [[nodiscard]] static constexpr auto recv(CrashWatched<Handle, Self, Peer, Reliable, Ctx, Position>&& watched,
                                             Poll poll) {
        using P = typename Handle::protocol;
        using Resource = typename Handle::resource_type;
        static_assert(PollRead<Poll, Resource, std::size_t>,
                      "fixy::session::diagnostic [Crash_Read_Must_Poll]: recv(): the read of a keyed message is not "
                      "a polling read, fixy::Option<std::size_t>(Resource&).  A read of another shape waits inside "
                      "the transport, where no crash is seen, so a dead peer blocks it for ever.");
        // The word and the value come from the role that the message names.
        using Sender = detail::crash_transport::keyed_sender_t<P, Peer>;
        const ::fixy::Option<std::size_t> word = detail::crash_transport::await_payload<Position, Peer, Sender>(
            poll, watched.inner_.resource(), *watched.peer_cell_, watched.counts_.received,
            watched.inner_.watch_endpoint());
        auto next =
            std::move(watched.inner_).recv([&word](Resource&) noexcept -> ::fixy::Option<std::size_t> { return word; });
        // The count holds the messages of the watched peer only, because the
        // report of the peer counts only those.
        const detail::crash_transport::message_counts counts =
            std::is_same_v<Sender, Peer> ? detail::crash_transport::one_more_received(watched.counts_)
                                         : watched.counts_;
        if constexpr (keyed_step_has_value_v<P>) {
            return wrap_<detail::crash_transport::payload_to_receive<Sender>>(watched, std::move(next), counts);
        } else {
            return wrap_(watched, std::move(next), counts);
        }
    }

    // A crash branch head gives the crash record.  No transport runs.
    template <typename Handle, typename Self, typename Peer, typename Reliable, typename Ctx, typename Position>
        requires is_recv_v<typename Handle::protocol> && is_crash_payload_v<typename Handle::protocol::message_type>
    [[nodiscard]] static constexpr auto recv(CrashWatched<Handle, Self, Peer, Reliable, Ctx, Position>&& watched) {
        using Record = typename Handle::protocol::message_type;
        using Resource = typename Handle::resource_type;
        static_assert(std::is_same_v<typename Record::peer, Peer>,
                      "fixy::session::diagnostic [Crash_Branch_Unwatched]: this crash branch names a role other "
                      "than the watched peer, so no detector can have triggered it.");
        const CrashCause cause = watched.peer_cell_->crash_cause().value_or(CrashCause::Unknown);
        auto [record, next] = std::move(watched.inner_).recv([cause](Resource&) noexcept -> ::fixy::Option<Record> {
            return ::fixy::some(Record{cause});
        });
        return std::pair{record, wrap_(watched, std::move(next), watched.counts_)};
    }

    // ── branch ───────────────────────────────────────────────────────
    //
    // The index of the branch decides where its handle stands inside the
    // message, and not the type of the handle.  In a keyed choice, one
    // branch can stand at the value step Recv<U, K> while another branch
    // with a payload of void continues with the same Recv<U, K> as a new
    // message.
    template <typename Handle, typename Self, typename Peer, typename Reliable, typename Ctx, typename Position,
              typename Poll, typename Handler>
        requires is_offer_v<typename Handle::protocol>
    static constexpr auto branch(CrashWatched<Handle, Self, Peer, Reliable, Ctx, Position>&& watched, Poll poll,
                                 Handler handler) {
        using P = typename Handle::protocol;
        static_assert(PollRead<Poll, typename Handle::resource_type, std::size_t>,
                      "fixy::session::diagnostic [Crash_Read_Must_Poll]: branch(): the read of the word is not a "
                      "polling read, fixy::Option<std::size_t>(Resource&).  A read of another shape waits inside "
                      "the transport, where no crash is seen, so a dead peer blocks it for ever.  Return the word "
                      "when a message is queued, and no value otherwise.");
        using OfferSender =
            std::conditional_t<std::is_same_v<offer_sender_t<P>, AnonymousPeer>, Peer, offer_sender_t<P>>;
        constexpr std::size_t message_branches = detail::crash_transport::message_branch_count<P>::value;
        constexpr bool is_watched = !reliable_set_contains_v<Reliable, OfferSender>;
        static_assert(!is_watched || std::is_same_v<OfferSender, Peer>,
                      "fixy::session::diagnostic [Crash_Sender_Unwatched]: this Offer has an unreliable sender "
                      "that is not the watched peer.");
        // The report of the peer counts the messages of the peer only.  So
        // only an Offer of the peer counts its word, and only such an Offer
        // reads the report.  A reliable third role sends with no regard to
        // the crash of the peer.
        constexpr bool is_from_peer = std::is_same_v<OfferSender, Peer>;

        // A word of the peer is one more message received.  The crash branch
        // receives none: it runs because the count of the report was reached.
        const auto take_word = [&watched, &handler](std::size_t word) {
            if (!detail::crash_transport::names_message_branch<P>(word)) [[unlikely]]
                detail::crash_transport::abort_on_crash_label(word, message_branches);
            const detail::crash_transport::message_counts counts =
                is_from_peer ? detail::crash_transport::one_more_received(watched.counts_) : watched.counts_;
            // The inner handle takes the word that the poll read, and enters
            // the label branch that it names.  A positional branch that opens
            // with a reception, and a keyed branch whose payload is not void,
            // still wait for the rest of the message of the label.
            return std::move(watched.inner_)
                .branch_indexed(
                    [word](typename Handle::resource_type&) noexcept -> ::fixy::Option<std::size_t> {
                        return ::fixy::some(word);
                    },
                    [&watched, &handler, counts]<std::size_t I>(std::integral_constant<std::size_t, I>,
                                                                auto branch_handle) {
                        using Head = typename decltype(branch_handle)::protocol;
                        constexpr bool is_inside_message = [] {
                            if constexpr (is_keyed_choice_v<P>) {
                                return keyed_step_has_value_v<std::tuple_element_t<I, typename P::branches_tuple>>;
                            } else if constexpr (is_recv_v<Head>) {
                                return !is_crash_payload_v<typename Head::message_type>;
                            } else {
                                return false;
                            }
                        }();
                        if constexpr (is_inside_message) {
                            return std::invoke(handler, wrap_<detail::crash_transport::payload_to_receive<OfferSender>>(
                                                            watched, std::move(branch_handle), counts));
                        } else {
                            return std::invoke(handler, wrap_(watched, std::move(branch_handle), counts));
                        }
                    });
        };
        auto& resource = watched.inner_.resource();
        ::fixy::Option<std::size_t> word{};
        {
            // The wait ends before the handler runs, so the watch never shows
            // this thread waiting while the handler steps the branch.
            std::optional<watch::wait_scope> wait;
            for (;;) {
                // Read the report before the poll.  A word that arrives while
                // the count of the report is not reached was queued before the
                // crash, and it wins.  Once the count is reached, the queue is
                // empty, and a word that the transport still reports was never
                // sent.
                if constexpr (is_from_peer) {
                    const std::optional<CrashWitness> witness = watched.peer_cell_->witness();
                    if (detail::crash_transport::queue_is_drained(witness, watched.counts_.received)) break;
                }
                // A read of another shape already failed the assertion above.
                // The read stays out of the body, so the build gives one error.
                if constexpr (PollRead<Poll, typename Handle::resource_type, std::size_t>) {
                    word = std::invoke(poll, resource);
                }
                if (word.is_some()) break;
                if (!wait) wait.emplace(watched.inner_.watch_endpoint());
                wait->poll();
            }
        }
        if (word.is_some()) return take_word(std::move(word).expect("the poll read a word"));
        if constexpr (is_watched) {
            // The crash branch is no label, so no word names it.  The handle
            // enters it through recover, with the context of the mint.
            constexpr std::size_t crash_branch = detail::crash::crash_branch_position(^^P, ^^Peer);
            return std::invoke(
                handler,
                wrap_(watched, HandleFactory::template recover<crash_branch>(watched.ctx_, std::move(watched.inner_)),
                      watched.counts_));
        } else {
            detail::crash_transport::abort_on_reliable_peer_crash();
        }
    }
};

template <typename Handle, typename Self, typename Peer, typename Reliable, typename Ctx, typename Position>
class [[nodiscard]] CrashWatched {
    static_assert(is_reliable_set<Reliable>::value,
                  "fixy::session::diagnostic [Crash_Reliable_Set_Required]: CrashWatched<H, Self, Peer, Reliable>: "
                  "Reliable must be a ReliableSet<Roles...>.");

    Handle inner_;
    // The cell of the pair of the peer and this endpoint.  The session reads
    // the crash of the peer and the count of its messages there.
    const CrashCellCore* peer_cell_;
    // The cell of the pair of this endpoint and the peer.  The session
    // counts its messages to the peer there, and its crash reports there.
    // The mint took the one writer of the cell, so no other session counts
    // into it.
    CrashCellCore* own_cell_;
    // The cell of the pair of this endpoint and the third role that the
    // protocol sends to, or null when it sends to no third role.  The mint
    // took its one writer too.
    CrashCellCore* third_cell_;
    // The context of the mint.  The door enters a crash branch with it.
    [[no_unique_address]] Ctx ctx_;
    detail::crash_transport::message_counts counts_{};

    // The one friend.  The door builds each decorator and does each step,
    // so no specialization of this template reaches another.
    friend class CrashSessionDoor;

    constexpr CrashWatched(Handle inner, const CrashCellCore& peer_cell, CrashCellCore& own_cell,
                           CrashCellCore* third_cell, const Ctx& ctx,
                           detail::crash_transport::message_counts counts = {}) noexcept
        : inner_{std::move(inner)},
          peer_cell_{&peer_cell},
          own_cell_{&own_cell},
          third_cell_{third_cell},
          ctx_{ctx},
          counts_{counts} {}

public:
    using handle_type = Handle;
    using context_type = Ctx;
    using protocol = typename Handle::protocol;
    using resource_type = typename Handle::resource_type;
    using loop_ctx = typename Handle::loop_ctx;
    using abandonment_policy = typename Handle::abandonment_policy;
    using self_role = Self;
    using peer_role = Peer;
    using reliable_set = Reliable;

    // The row-hash shape of a discipline carrier.  The identity names the
    // roles, the reliable set and the position inside a message, and the
    // inner handle folds its protocol and its Resource.
    using row_discipline = ::fixy::row_discipline::crash_watched<Self, Peer, Reliable, Position>;
    using row_payload = Handle;

    constexpr CrashWatched(CrashWatched&&) noexcept = default;
    constexpr CrashWatched& operator=(CrashWatched&&) noexcept = default;
    CrashWatched(const CrashWatched&) = delete("a crash-watched handle is linear, like the handle it wraps");
    CrashWatched& operator=(const CrashWatched&) = delete("a crash-watched handle is linear, like the handle it wraps");
    ~CrashWatched() = default;

    [[nodiscard]] bool peer_has_crashed() const noexcept { return peer_cell_->has_crashed(); }

    // The messages this session received from the peer and sent to it, a
    // label with the payload of its branch counted once.
    [[nodiscard]] constexpr std::uint64_t messages_received() const noexcept { return counts_.received; }
    [[nodiscard]] constexpr std::uint64_t messages_sent() const noexcept { return counts_.sent; }

    // ── send ─────────────────────────────────────────────────────────
    //
    // Transport is a trying write, bool(Resource&, T&).  It returns false,
    // and leaves the payload as it was, while the queue of the peer has no
    // room or is closed.  The inner handle then waits through the watch and
    // tries again, and the decorator checks the peer before each try.  When
    // the peer has crashed, the payload goes back to the caller as
    // `undelivered`, so a token in it is not lost.  The crash check and the
    // write are two steps, and the peer can crash between them, so only a
    // write that refuses a closed queue keeps a token out of a queue that no
    // one reads.  A declared write waits inside the transport, where no
    // crash is seen, so the decorator refuses it.
    // The value is the value of a marker, and the step takes the token from
    // the slots of the inner handle (the slot form of fixy/session/Flow.h).
    template <typename Transport, typename P = protocol>
        requires is_send_v<P> && (!is_keyed_step_v<P>) && TryWrite<Transport, resource_type, typename P::message_type>
    [[nodiscard]] constexpr auto send(detail::sent_value_t<typename P::message_type> value, Transport transport) && {
        return CrashSessionDoor::send(std::move(*this), std::move(value), std::move(transport));
    }

    template <typename Transport, typename P = protocol>
        requires is_send_v<P> && (!is_keyed_step_v<P>)
                  && (!TryWrite<Transport, resource_type, typename P::message_type>)
    void send(detail::sent_value_t<typename P::message_type>, Transport) && = delete(
        "[Crash_Write_Must_Try] the write of a crash session is a trying write, bool(Resource&, T&), that returns "
        "false while the queue of the peer has no room or is closed.  A write of another shape waits inside the "
        "transport, where no crash is seen, or puts the payload into a queue that no one reads.");

    // A keyed Send is one message: its label word, and then the value of
    // its payload when the payload is not void.  This step writes the word,
    // and the transport is a trying write of the word.  The successor then
    // stands at the value step, inside the message, and its send moves the
    // value.  The word to a crashed peer is lost, and `undelivered` then
    // holds the word.
    template <typename Transport, typename P = protocol>
        requires is_send_v<P> && is_keyed_step_v<P> && TryWrite<Transport, resource_type, std::size_t>
    [[nodiscard]] constexpr auto send(Transport transport) && {
        return CrashSessionDoor::send(std::move(*this), std::move(transport));
    }

    template <typename Transport, typename P = protocol>
        requires is_send_v<P> && is_keyed_step_v<P> && (!TryWrite<Transport, resource_type, std::size_t>)
    void send(Transport) && = delete("[Crash_Write_Must_Try] the write of a word in a crash session is a trying write, "
                                     "bool(Resource&, std::size_t).  A write of another shape waits inside the "
                                     "transport, where no crash is seen.");

    // ── select ───────────────────────────────────────────────────────
    //
    // A positional branch that opens with a send carries the payload of
    // the label, so the successor stands inside that message until the
    // send.  A keyed branch is its label word and then the value of its
    // payload, so the successor stands inside the message until the value
    // goes.  A keyed branch whose payload is void is its word alone, and
    // the successor stands past it.
    template <std::size_t I, typename Transport, typename P = protocol>
        requires is_select_v<P> && TryWrite<Transport, resource_type, std::size_t>
    [[nodiscard]] constexpr auto select(Transport transport) && {
        return CrashSessionDoor::template select<I>(std::move(*this), std::move(transport));
    }

    template <std::size_t I, typename Transport, typename P = protocol>
        requires is_select_v<P> && (!TryWrite<Transport, resource_type, std::size_t>)
    void select(Transport) && = delete("[Crash_Write_Must_Try] the write of a label in a crash session is a trying "
                                       "write, bool(Resource&, std::size_t).  A write of another shape waits inside "
                                       "the transport, where no crash is seen.");

    // ── recv ─────────────────────────────────────────────────────────
    //
    // Read is a polling read, fixy::Option<T>(Resource&).  It returns the
    // payload when one from the sender is queued, and no value otherwise.
    // Between reads the decorator waits through the watch and checks the
    // peer, so a crash ends the wait.  A declared read would wait inside the
    // transport, where no crash is seen, so the decorator refuses it.
    //
    // A payload queued before the crash wins.  After the crash nothing more
    // can come, and the protocol gives this reception no crash branch.  The
    // mint admits a bare reception only from a reliable peer, and the
    // payload of a label travels with it, so the wait ends with
    // Crash_Of_Reliable_Peer or with Crash_Splits_A_Message.
    template <typename Read, typename P = protocol>
        requires is_recv_v<P> && (!is_keyed_step_v<P>) && (!is_crash_payload_v<typename P::message_type>)
    [[nodiscard]] constexpr auto recv(Read read) && {
        return CrashSessionDoor::recv(std::move(*this), std::move(read));
    }

    // A keyed Recv is one message: its label word, and then the value of
    // its payload when the payload is not void.  Poll is a polling read of
    // the word, and the wait is the wait of the recv above.  The successor
    // then stands at the value step, inside the message, and its recv reads
    // the value.
    template <typename Poll, typename P = protocol>
        requires is_recv_v<P> && is_keyed_step_v<P>
    [[nodiscard]] constexpr auto recv(Poll poll) && {
        return CrashSessionDoor::recv(std::move(*this), std::move(poll));
    }

    // A crash branch head gives the crash record.  No transport runs.
    template <typename P = protocol>
        requires is_recv_v<P> && is_crash_payload_v<typename P::message_type>
    [[nodiscard]] constexpr auto recv() && {
        return CrashSessionDoor::recv(std::move(*this));
    }

    // ── branch ───────────────────────────────────────────────────────
    //
    // Poll is a polling read of a word, fixy::Option<std::size_t>(Resource&).
    // It returns a wire word when a message from the peer is queued, and no
    // value otherwise.  Between polls the decorator waits through the watch
    // and checks the peer.  A declared read would wait inside the transport,
    // where no crash is seen, so the decorator refuses it.
    //
    // The decorator polls the word and gives it to branch_indexed of the
    // inner handle, which enters the branch that the word names
    // (branch_of_wire_word in fixy/session/Transport.h).  A crash branch is
    // no label, so no word names it.  On a crash the decorator enters the
    // crash branch through HandleFactory::recover, with the context of the
    // mint, and no word is read.  A positional branch that
    // opens with a reception waits for the payload of the label, from the
    // sender of the Offer.  A keyed branch whose payload is not void waits
    // for the value of its payload, from the same sender.
    //
    // The index of the branch decides where its handle stands inside the
    // message, and not the type of the handle.  In a keyed choice, one
    // branch can stand at the value step Recv<U, K> while another branch
    // with a payload of void continues with the same Recv<U, K> as a new
    // message.
    template <typename Poll, typename Handler, typename P = protocol>
        requires is_offer_v<P>
    constexpr auto branch(Poll poll, Handler handler) && {
        return CrashSessionDoor::branch(std::move(*this), std::move(poll), std::move(handler));
    }

    // ── close ────────────────────────────────────────────────────────
    template <typename P = protocol>
        requires is_terminal_state_v<P>
    [[nodiscard]] constexpr resource_type close() && {
        return std::move(inner_).close();
    }

    // ── crash ────────────────────────────────────────────────────────
    //
    // Stops the local endpoint and reports it through the cell of this
    // endpoint, which the peer watches.  The report carries the count that
    // this session wrote into the cell, so the peer takes its crash branch
    // once every counted message has arrived.  The endpoint is then at the
    // runtime type Stop, where no operation exists, so the call gives back
    // the Resource instead of a handle.  Rule r-↯ does not apply to a
    // process that has already ended, so End has no crash().  A crash never
    // falls inside one message: after a positional label whose branch opens
    // with a send, or after the label word of a keyed message with a value,
    // the payload goes first.
    template <typename P = protocol>
        requires(!is_terminal_state_v<P>)
    [[nodiscard]] resource_type crash(CrashCause cause) && {
        static_assert(!reliable_set_contains_v<Reliable, Self>,
                      "fixy::session::diagnostic [Crash_Of_Reliable_Role]: crash(): the local role is in the "
                      "reliable set.  Rule r-↯ lets only a role outside the reliable set crash.  Remove the "
                      "role from the reliable set, or do not crash it.");
        static_assert(!detail::crash_transport::is_payload_to_send_v<Position>,
                      "fixy::session::diagnostic [Crash_Splits_A_Message]: crash(): this endpoint sent a label, "
                      "and the payload of that label has not gone: the payload that opens its branch, or the "
                      "value of a keyed message.  The label and the payload are one message, so a crash here "
                      "delivers half of it, and the peer waits in a position with no crash branch.  Send the "
                      "payload first, then crash.");
        return CrashSessionDoor::crash(std::move(*this), cause);
    }

    template <typename Reason>
        requires DetachReason<Reason>
    constexpr void detach(Reason reason) && noexcept {
        std::move(inner_).detach(reason);
    }

    [[nodiscard]] constexpr resource_type& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const resource_type& resource() const& noexcept { return inner_.resource(); }
};

// ── The mint ─────────────────────────────────────────────────────────

// The mint takes the cell of the pair of the peer and this endpoint, which
// it reads, and the one writer of the cell of each pair of this endpoint
// and a receiver, which it counts into: the peer, and the third role that
// the protocol sends to.  A protocol that sends to no third role takes no
// third writer.  The body reads the writers at run time, so the mint is not
// constexpr.
template <typename Proto, typename Self, typename Peer, typename Reliable = NoReliableRoles,
          AbandonmentPolicy Policy = DefaultAbandonmentPolicy, typename Ctx, typename Resource,
          typename Third = NoThirdRole>
    requires CtxFitsCrashSession<Ctx, Proto, Self, Peer, Reliable, Resource, Third>
[[nodiscard]] auto mint_crash_session(Ctx const& ctx, Resource resource, const CrashCell<Peer, Self>& peer_cell,
                                      CrashWriter<Self, Peer> own, CrashWriter<Self, Third> third = {},
                                      MintSite site = {}) noexcept {
    return CrashSessionDoor::open<Proto, Self, Peer, Reliable, Policy>(ctx, std::forward<Resource>(resource), peer_cell,
                                                                       std::move(own), std::move(third), site);
}

// The decorator keeps the cell's address, so a temporary cell would
// dangle before the first step.
template <typename Proto, typename Self, typename Peer, typename Reliable = NoReliableRoles,
          AbandonmentPolicy Policy = DefaultAbandonmentPolicy, typename Ctx, typename Resource,
          typename Third = NoThirdRole>
void mint_crash_session(Ctx const&, Resource, const CrashCell<Peer, Self>&&, CrashWriter<Self, Peer>,
                        CrashWriter<Self, Third> = {}, MintSite = {}) =
    delete("[Crash_Cell_Temporary] mint_crash_session: the detector cell is a temporary.  The decorator keeps its "
           "address, so the cell must outlive every handle of the session.");

}  // namespace fixy::session

namespace fixy::session::detail::crash_transport::armed_witness {
struct Alice {};
struct Bob {};
struct Msg {};
using Guarded = Offer<Recv<Msg, End>, Recv<Crash<Bob>, End>>;
using Bare = Recv<Msg, End>;
}  // namespace fixy::session::detail::crash_transport::armed_witness

template <>
struct foundation::contracts::armed_cell<::fixy::session::is_crash_session_admissible> {
    using accepts =
        witnesses<::fixy::session::CrashSession<::fixy::session::detail::crash_transport::armed_witness::Guarded,
                                                ::fixy::session::detail::crash_transport::armed_witness::Alice,
                                                ::fixy::session::detail::crash_transport::armed_witness::Bob,
                                                ::fixy::session::ReliableSet<>>>;
    using refuses =
        witnesses<int,
                  ::fixy::session::CrashSession<::fixy::session::detail::crash_transport::armed_witness::Bare,
                                                ::fixy::session::detail::crash_transport::armed_witness::Alice,
                                                ::fixy::session::detail::crash_transport::armed_witness::Bob,
                                                ::fixy::session::ReliableSet<>>,
                  ::fixy::session::CrashSession<::fixy::session::detail::crash_transport::armed_witness::Guarded,
                                                ::fixy::session::detail::crash_transport::armed_witness::Bob,
                                                ::fixy::session::detail::crash_transport::armed_witness::Bob,
                                                ::fixy::session::ReliableSet<>>>;
};
