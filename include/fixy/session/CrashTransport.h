#pragma once

// Crash transport: one decorator that runs a session handle under the
// crash-stop semantics of fixy/session/Crash.h.
//
// CrashWatched<Handle, Self, Peer, Reliable> wraps any session handle.
// Each operation forwards to the handle and wraps the successor, so the
// crash semantics hold at every step.  The operations follow LMCS 2025
// Fig. 4:
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
// Every read and every poll returns std::optional.  A transport that
// returns the value itself waits inside the transport, where no crash is
// seen, so the exact return type refuses it.
//
// The detector is a PeerCrashCell.  A CrashReporter writes it, and each
// cell has one reporter: the Keeper's failure detector, or the endpoint
// that crashes and reports itself.  The decorator only reads the cell.
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
// The transport cannot write the witness.  It gets only the Resource, the
// report is fixed once it is published, and only the one reporter of the
// cell can publish it.  An endpoint that crashes reports its own count,
// which its handle kept, so the count needs no trust in the caller.
//
// ── What the ported source did wrong ────────────────────────────────
//
// crucible/bridges/CrashTransport.h wrapped each head in its own
// specialization, six in all.  On a crash it detached the handle and
// returned an error, so no declared crash branch ever ran.  Its Offer
// arm could not receive a label, it had no arm for delegation or
// checkpoints, and its stop_class_compatible walk admitted an unknown
// combinator through a true primary.  Here one class handles every head,
// the crash branch is what runs on a crash, and every walk that gates
// the mint has a false primary.  The crash class is no longer in the
// type, so no counterpart of stop_class_compatible is needed.
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
// its branch together: a split shows as Crash_Splits_A_Message.
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
#include <fixy/session/Handle.h>

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
#include <optional>
#include <source_location>
#include <tuple>
#include <type_traits>
#include <utility>

namespace fixy::session {

// ── The crash detector cell ──────────────────────────────────────────

// The number of messages that one endpoint sent to its peer on one
// session.  A label and the payload of its branch count as one message.
enum class MessageCount : std::uint64_t {};

// What a crash report says: how the peer stopped, and how many messages
// it sent before it stopped.
struct CrashWitness {
    CrashCause cause = CrashCause::Unknown;
    MessageCount messages_sent{};
};

class CrashReporter;

// One word of state on its own cache line: the cause in the top byte,
// the message count in the low 56 bits, or all ones while the peer is
// alive.  A cause is at most crash_cause_top_value, so no report is all
// ones.  The report is one compare-and-swap, so the first report wins
// whole, and a reader never sees the cause of one report beside the count
// of another.
class PeerCrashCell : public ::foundation::Pinned<PeerCrashCell> {
    static constexpr std::uint64_t alive_state = ~std::uint64_t{0};
    static constexpr unsigned count_bits = 56;
    static constexpr std::uint64_t count_mask = (std::uint64_t{1} << count_bits) - 1;

    alignas(64) std::atomic<std::uint64_t> state_{alive_state};
    // Set when the one reporter of this cell is minted.
    std::atomic<bool> has_reporter_{false};

    friend class CrashReporter;

    // Returns true when this report is the first.
    bool publish_(CrashWitness witness) noexcept {
        const std::uint64_t packed = (std::uint64_t{static_cast<std::uint8_t>(witness.cause)} << count_bits)
                                   | std::to_underlying(witness.messages_sent);
        std::uint64_t expected = alive_state;
        return state_.compare_exchange_strong(expected, packed, std::memory_order_acq_rel, std::memory_order_acquire);
    }

public:
    // The largest message count a report can carry.
    static constexpr std::uint64_t max_message_count = count_mask;

    constexpr PeerCrashCell() noexcept = default;

    [[nodiscard]] bool has_crashed() const noexcept { return state_.load(std::memory_order_acquire) != alive_state; }

    // The report, or no value while the peer is alive.  Only the reporter
    // writes the state, and its precondition keeps the cause inside
    // CrashCause and the count inside 56 bits, so the decode is total over
    // what can be read.
    [[nodiscard]] std::optional<CrashWitness> witness() const noexcept {
        const std::uint64_t state = state_.load(std::memory_order_acquire);
        if (state == alive_state) return std::nullopt;
        return CrashWitness{static_cast<CrashCause>(state >> count_bits), static_cast<MessageCount>(state & count_mask)};
    }

    [[nodiscard]] std::optional<CrashCause> crash_cause() const noexcept {
        const std::optional<CrashWitness> report = witness();
        if (!report) return std::nullopt;
        return report->cause;
    }
};

namespace detail::crash_transport {

[[noreturn]] [[gnu::cold, gnu::noinline]] inline void abort_on_second_reporter() noexcept {
    std::fprintf(stderr,
                 "fixy::session: diagnostic [Crash_Reporter_Twice]: a crash reporter was minted for a cell that "
                 "already has one.  A cell has one author, so that the count it publishes has one source.  Give the "
                 "one reporter to the failure detector, or to the endpoint that reports its own crash.\n");
    std::abort();
}

}  // namespace detail::crash_transport

// The one author of the reports of one cell.  Only mint_crash_reporter
// builds it, and each cell has one, so a transport that holds the cell
// cannot write a report: it would need the reporter, and the reporter is
// held by the failure detector or by the endpoint that crashes.  A report
// consumes the reporter, because a peer crashes once.
//
// The move constructor is user-provided, so the class is not trivially
// copyable and std::bit_cast cannot build one.  It has no trivial
// constructor, so it is not an implicit-lifetime type and no lifetime
// start over bytes can make one.
class [[nodiscard]] CrashReporter {
    PeerCrashCell* cell_ = nullptr;

    explicit CrashReporter(PeerCrashCell& cell) noexcept : cell_{&cell} {}

    friend CrashReporter mint_crash_reporter(PeerCrashCell& cell) noexcept;

public:
    CrashReporter(CrashReporter&& other) noexcept : cell_{std::exchange(other.cell_, nullptr)} {}
    CrashReporter(const CrashReporter&) = delete("a cell has one reporter, so its count has one source");
    CrashReporter& operator=(const CrashReporter&) = delete("a cell has one reporter, so its count has one source");
    CrashReporter& operator=(CrashReporter&&) = delete("a reporter names one cell for its whole life");
    ~CrashReporter() = default;

    // Publishes the crash: its cause, and the number of messages that the
    // crashed peer sent on the session.  Returns true when this is the
    // first report of the cell.
    bool report(CrashCause cause, MessageCount messages_sent) && noexcept {
        CRUCIBLE_PRE(cell_ != nullptr);
        CRUCIBLE_PRE(static_cast<std::uint8_t>(cause) <= crash_cause_top_value);
        CRUCIBLE_PRE(std::to_underlying(messages_sent) <= PeerCrashCell::max_message_count);
        PeerCrashCell* const cell = std::exchange(cell_, nullptr);
        return cell->publish_(CrashWitness{cause, messages_sent});
    }

private:
    [[nodiscard]] static bool claim_(PeerCrashCell& cell) noexcept {
        return !cell.has_reporter_.exchange(true, std::memory_order_acq_rel);
    }
};

// Mints the one reporter of a cell.  A second mint for the same cell
// ends the process with Crash_Reporter_Twice.
[[nodiscard]] inline CrashReporter mint_crash_reporter(PeerCrashCell& cell) noexcept {
    if (!CrashReporter::claim_(cell)) [[unlikely]]
        detail::crash_transport::abort_on_second_reporter();
    return CrashReporter{cell};
}

// A const cell is the survivor's view of the detector.  It gives no right
// to report.
void mint_crash_reporter(const PeerCrashCell&) noexcept =
    delete("[Crash_Reporter_From_Const_Cell] a const cell is a view of the detector, and a view gives no right to "
           "report a crash.  Mint the reporter from the cell that the failure detector owns.");

namespace detach_reason {

// The local endpoint stopped under crash-stop semantics.  The peer
// learns of it through its detector.
struct LocalCrashStop : tag_base {};

}  // namespace detach_reason

// ── The sender watch ─────────────────────────────────────────────────
//
// One cell watches one role.  An Offer whose sender is unreliable and is
// not the watched peer has a crash branch that no detector can trigger,
// so the mint refuses it.

namespace detail::crash_transport {

template <typename P, typename Peer, typename Reliable>
struct is_every_sender_watched : std::false_type {};

template <typename OfferSender, typename Peer, typename Reliable, typename... Bs>
inline constexpr bool offer_watched_v =
    (std::is_same_v<OfferSender, Peer> || reliable_set_contains_v<Reliable, OfferSender>)
    && (is_every_sender_watched<Bs, Peer, Reliable>::value && ...);

template <typename Peer, typename Reliable>
struct is_every_sender_watched<End, Peer, Reliable> : std::true_type {};
template <typename Peer, typename Reliable>
struct is_every_sender_watched<Continue, Peer, Reliable> : std::true_type {};
template <typename T, typename K, typename Peer, typename Reliable>
struct is_every_sender_watched<Send<T, K>, Peer, Reliable> : is_every_sender_watched<K, Peer, Reliable> {};
template <typename T, typename K, typename Peer, typename Reliable>
struct is_every_sender_watched<Recv<T, K>, Peer, Reliable> : is_every_sender_watched<K, Peer, Reliable> {};
template <typename... Bs, typename Peer, typename Reliable>
struct is_every_sender_watched<Select<Bs...>, Peer, Reliable>
    : std::bool_constant<(is_every_sender_watched<Bs, Peer, Reliable>::value && ...)> {};
template <typename... Bs, typename Peer, typename Reliable>
struct is_every_sender_watched<Offer<Bs...>, Peer, Reliable> : std::bool_constant<offer_watched_v<Peer, Peer, Reliable, Bs...>> {
};
template <typename Role, typename... Bs, typename Peer, typename Reliable>
struct is_every_sender_watched<Offer<Sender<Role>, Bs...>, Peer, Reliable>
    : std::bool_constant<offer_watched_v<Role, Peer, Reliable, Bs...>> {};
// The note of a Select names the endpoint itself, which sends and is not
// watched, so the walk reads the branches alone.
template <typename Role, typename... Bs, typename Peer, typename Reliable>
struct is_every_sender_watched<Select<Sender<Role>, Bs...>, Peer, Reliable> : is_every_sender_watched<Select<Bs...>, Peer, Reliable> {};
template <typename B, typename Peer, typename Reliable>
struct is_every_sender_watched<Loop<B>, Peer, Reliable> : is_every_sender_watched<B, Peer, Reliable> {};

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
    std::fprintf(stderr,
                 "fixy::session: diagnostic [Crash_Of_Reliable_Peer]: the peer crashed, and this endpoint "
                 "counts it reliable, so the reception has no crash branch and would wait for ever.  The two "
                 "endpoints disagree about the reliable set.  Give both the same set, or remove the peer "
                 "from it and add the crash branch.\n");
    std::abort();
}

// The peer sent a label and crashed before the payload of its branch.  A
// label and that payload are one message, so the transport split it, and
// the branch that the label entered has no crash branch.
[[noreturn]] [[gnu::cold, gnu::noinline]] inline void abort_on_split_message() noexcept {
    std::fprintf(stderr,
                 "fixy::session: diagnostic [Crash_Splits_A_Message]: the peer crashed after a label and "
                 "before the payload of its branch.  The label and the payload are one message, so the "
                 "transport delivered half of one.  Deliver a label and the payload of its branch together.\n");
    std::abort();
}

// Where a handle stands inside one message.  A label and the payload that
// heads its branch are one message (fixy/session/Crash.h), so a handle
// that holds the label and not yet the payload is inside a message.  A
// keyed label word and the value of its payload are one message too, so
// a handle at the value step of a keyed message is inside a message
// (fixy/session/Handle.h).  A keyed message with a payload of void is its
// label word alone.
struct between_messages {};

// This endpoint picked a label whose branch opens with a send, or it sent
// the label word of a keyed message with a value.  The payload still has
// to go.
struct payload_to_send {};

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
// The counts travel from each handle to the next.
struct message_counts {
    std::uint64_t received = 0;
    std::uint64_t sent = 0;
};

// True when the peer has crashed and every message that its report counts
// has arrived.  The queue from the peer is then empty, which is the side
// condition of rule r-rcv-⊙, and a message past this point was never sent.
[[nodiscard]] inline bool queue_is_drained(const std::optional<CrashWitness>& witness, std::uint64_t received) noexcept {
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
// check: the mint admits such a role only when it is reliable.
//
// The first read that finds nothing opens a watch::wait_scope on the
// endpoint of the session, so the watch sees the wait and checks it
// against the priority order.  A read that finds its payload at once
// never touches the watch.
template <typename Position, typename Peer, typename Reliable, typename Read, typename Resource>
[[nodiscard]] constexpr auto await_payload(Read& read, Resource& resource, const PeerCrashCell& cell,
                                           std::uint64_t received, watch::endpoint_id endpoint) {
    using Sender = typename payload_sender<Position, Peer>::type;
    constexpr bool is_new_message = std::is_same_v<Position, between_messages>;
    std::optional<watch::wait_scope> wait;
    for (;;) {
        if constexpr (!std::is_same_v<Sender, Peer>) {
            if (auto payload = std::invoke(read, resource)) return payload;
        } else if constexpr (is_new_message) {
            if (queue_is_drained(cell.witness(), received)) abort_on_reliable_peer_crash();
            if (auto payload = std::invoke(read, resource)) return payload;
        } else {
            if (auto payload = std::invoke(read, resource)) return payload;
            if (cell.has_crashed()) {
                if (auto payload = std::invoke(read, resource)) return payload;
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
// branch, in the reading of fixy/session/Handle.h.  Message branches
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
concept CrashSessionAdmissible = is_reliable_set<Reliable>::value && !std::is_same_v<Self, Peer>
                              && WellFormedRunnableProtocol<Proto>
                              && PermissionFlowCloses<Proto, ::foundation::permissions::EmptyPermSet>
                              && is_crash_well_formed_v<Proto>
                              && every_reception_handles_crash_v<Proto, Peer, Reliable>
                              && detail::crash_transport::is_every_sender_watched<Proto, Peer, Reliable>::value
                              && detail::crash::is_delegation_free<Proto>::value;

// The same question as a one-argument trait, so that it can hold an
// armed cell.
template <typename Proto, typename Self, typename Peer, typename Reliable>
struct CrashSession {};

template <typename Q>
struct is_crash_session_admissible : std::false_type {};
template <typename Proto, typename Self, typename Peer, typename Reliable>
struct is_crash_session_admissible<CrashSession<Proto, Self, Peer, Reliable>>
    : std::bool_constant<CrashSessionAdmissible<Proto, Self, Peer, Reliable>> {};

// ── The decorator ────────────────────────────────────────────────────

template <typename Handle, typename Self, typename Peer, typename Reliable,
          typename Position = detail::crash_transport::between_messages>
class CrashWatched;

// The result of a send.  `undelivered` holds the payload when the peer
// had crashed before the send, or when the transport refused it at the
// write.  It is empty when the transport took the payload.
template <typename Next, typename T>
struct [[nodiscard]] CrashSend {
    Next next;
    std::optional<T> undelivered;
};

// The door of the crash mint, which stands after the mint.
class CrashSessionDoor;

template <typename Handle, typename Self, typename Peer, typename Reliable, typename Position>
class [[nodiscard]] CrashWatched {
    static_assert(is_reliable_set<Reliable>::value,
                  "fixy::session::diagnostic [Crash_Reliable_Set_Required]: CrashWatched<H, Self, Peer, Reliable>: "
                  "Reliable must be a ReliableSet<Roles...>.");

    Handle inner_;
    const PeerCrashCell* peer_cell_;
    detail::crash_transport::message_counts counts_{};

    template <typename, typename, typename, typename, typename>
    friend class CrashWatched;

    // The first decorator of a session comes only from this door.
    friend class CrashSessionDoor;

    constexpr CrashWatched(Handle inner, const PeerCrashCell& cell,
                           detail::crash_transport::message_counts counts = {}) noexcept
        : inner_{std::move(inner)}, peer_cell_{&cell}, counts_{counts} {}

    // Wraps the successor with the counts after the step, and records where
    // it stands inside a message.
    template <typename NextPosition = detail::crash_transport::between_messages, typename Next>
    [[nodiscard]] constexpr auto wrap_(Next next, detail::crash_transport::message_counts counts) const noexcept {
        return CrashWatched<Next, Self, Peer, Reliable, NextPosition>{std::move(next), *peer_cell_, counts};
    }

    // The counts after a step that moved one more message the given way.
    [[nodiscard]] constexpr detail::crash_transport::message_counts one_more_sent_() const noexcept {
        return {counts_.received, counts_.sent + 1};
    }
    [[nodiscard]] constexpr detail::crash_transport::message_counts one_more_received_() const noexcept {
        return {counts_.received + 1, counts_.sent};
    }

    // Calls `enter` for the label branch at `index`, with the index as a
    // constant.  Each branch gives one result type, as the handler of
    // SessionHandle<Offer>::branch must.  Complexity: linear in the label
    // branches.
    template <typename Enter, std::size_t... Is>
    static constexpr auto enter_label_(std::size_t index, const Enter& enter, std::index_sequence<Is...>) {
        using Result = decltype(enter(std::integral_constant<std::size_t, 0>{}));
        if constexpr (std::is_void_v<Result>) {
            static_cast<void>(((index == Is ? (enter(std::integral_constant<std::size_t, Is>{}), true) : false) || ...));
        } else {
            std::optional<Result> result;
            static_cast<void>(
                ((index == Is ? (result.emplace(enter(std::integral_constant<std::size_t, Is>{})), true) : false)
                 || ...));
            if (!result) [[unlikely]]
                std::abort();
            return std::move(*result);
        }
    }

    // A trying write of a word, guarded by the peer cell.  To a crashed
    // peer it writes nothing and reports the word taken, so the inner handle
    // steps on.  `is_written` says whether the transport took the word.
    template <typename Transport>
    [[nodiscard]] constexpr auto watched_word_write_(Transport& transport, bool& is_written) const noexcept {
        return [this, &transport, &is_written](resource_type& resource, std::size_t& word) noexcept(
                   std::is_nothrow_invocable_v<Transport&, resource_type&, std::size_t&>) -> bool {
            if (peer_cell_->has_crashed()) return true;
            is_written = std::invoke(transport, resource, word);
            return is_written;
        };
    }

public:
    using handle_type = Handle;
    using protocol = typename Handle::protocol;
    using resource_type = typename Handle::resource_type;
    using loop_ctx = typename Handle::loop_ctx;
    using abandonment_policy = typename Handle::abandonment_policy;
    using self_role = Self;
    using peer_role = Peer;
    using reliable_set = Reliable;

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
    template <typename Transport, typename P = protocol>
        requires is_send_v<P> && (!is_keyed_step_v<P>) && TryWrite<Transport, resource_type, typename P::message_type>
    [[nodiscard]] constexpr auto send(typename P::message_type value, Transport transport) && {
        using T = typename P::message_type;
        std::optional<T> undelivered;
        constexpr bool is_nothrow =
            std::is_nothrow_invocable_v<Transport&, resource_type&, T&> && std::is_nothrow_move_constructible_v<T>;
        auto next = std::move(inner_).send(std::move(value), [&](resource_type& resource, T& payload) noexcept(is_nothrow)
                                                                  -> bool {
            if (peer_cell_->has_crashed()) {
                undelivered.emplace(std::move(payload));
                return true;
            }
            return std::invoke(transport, resource, payload);
        });
        // The payload of a label is part of the label's message, which the
        // select counted.  A payload that did not go is no message.
        constexpr bool is_payload_of_label = std::is_same_v<Position, detail::crash_transport::payload_to_send>;
        const detail::crash_transport::message_counts counts =
            (is_payload_of_label || undelivered.has_value()) ? counts_ : one_more_sent_();
        auto wrapped = wrap_(std::move(next), counts);
        return CrashSend<decltype(wrapped), T>{std::move(wrapped), std::move(undelivered)};
    }

    template <typename Transport, typename P = protocol>
        requires is_send_v<P> && (!is_keyed_step_v<P>)
              && (!TryWrite<Transport, resource_type, typename P::message_type>)
    void send(typename P::message_type, Transport) && = delete(
        "[Crash_Write_Must_Try] the write of a crash session is a trying write, bool(Resource&, T&), that returns "
        "false while the queue of the peer has no room or is closed.  A write of another shape waits inside the "
        "transport, where no crash is seen, or puts the payload into a queue that no one reads.");

    // A keyed Send is one message: its label word, and then the value of
    // its payload when the payload is not void.  This step writes the word,
    // and the transport is a trying write of the word.  The successor then
    // stands at the value step, inside the message, and its send moves the
    // value.  The word to a crashed peer is lost, and `undelivered` then
    // holds the empty message.
    template <typename Transport, typename P = protocol>
        requires is_send_v<P> && is_keyed_step_v<P> && TryWrite<Transport, resource_type, std::size_t>
    [[nodiscard]] constexpr auto send(Transport transport) && {
        using T = typename P::message_type;
        bool is_written = false;
        auto next = std::move(inner_).send(watched_word_write_(transport, is_written));
        const detail::crash_transport::message_counts counts = is_written ? one_more_sent_() : counts_;
        auto wrapped = [&] {
            if constexpr (keyed_step_has_value_v<P>) {
                return wrap_<detail::crash_transport::payload_to_send>(std::move(next), counts);
            } else {
                return wrap_(std::move(next), counts);
            }
        }();
        std::optional<T> undelivered;
        if (!is_written) undelivered.emplace();
        return CrashSend<decltype(wrapped), T>{std::move(wrapped), std::move(undelivered)};
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
        bool is_written = false;
        auto next = std::move(inner_).template select<I>(watched_word_write_(transport, is_written));
        const detail::crash_transport::message_counts counts = is_written ? one_more_sent_() : counts_;
        using Next = decltype(next);
        constexpr bool is_inside_message = [] {
            if constexpr (is_keyed_choice_v<P>) {
                return keyed_step_has_value_v<std::tuple_element_t<I, typename P::branches_tuple>>;
            } else {
                return is_send_v<typename Next::protocol>;
            }
        }();
        if constexpr (is_inside_message) {
            return wrap_<detail::crash_transport::payload_to_send>(std::move(next), counts);
        } else {
            return wrap_(std::move(next), counts);
        }
    }

    template <std::size_t I, typename Transport, typename P = protocol>
        requires is_select_v<P> && (!TryWrite<Transport, resource_type, std::size_t>)
    void select(Transport) && = delete("[Crash_Write_Must_Try] the write of a label in a crash session is a trying "
                                       "write, bool(Resource&, std::size_t).  A write of another shape waits inside "
                                       "the transport, where no crash is seen.");

    // ── recv ─────────────────────────────────────────────────────────
    //
    // Read is a polling read, std::optional<T>(Resource&).  It returns the
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
        using T = typename P::message_type;
        static_assert(PollRead<Read, resource_type, T>,
                      "fixy::session::diagnostic [Crash_Read_Must_Poll]: recv(): the read is not a polling read, "
                      "std::optional<T>(Resource&).  A read of another shape waits inside the transport, where no "
                      "crash is seen, so a dead peer blocks it for ever.  Return the payload when one is queued, and "
                      "no value otherwise.");
        std::optional<T> payload = detail::crash_transport::await_payload<Position, Peer, Reliable>(
            read, inner_.resource(), *peer_cell_, counts_.received, inner_.watch_endpoint());
        auto [value, next] = std::move(inner_).recv(
            [&payload](resource_type&) noexcept(std::is_nothrow_move_constructible_v<T>) -> std::optional<T> {
                return std::move(payload);
            });
        // A bare reception is a new message.  The payload of a label is part
        // of the label's message, which the branch counted.
        constexpr bool is_new_message = std::is_same_v<Position, detail::crash_transport::between_messages>;
        return std::pair{std::move(value), wrap_(std::move(next), is_new_message ? one_more_received_() : counts_)};
    }

    // A keyed Recv is one message: its label word, and then the value of
    // its payload when the payload is not void.  Poll is a polling read of
    // the word, and the wait is the wait of the recv above.  The successor
    // then stands at the value step, inside the message, and its recv reads
    // the value.
    template <typename Poll, typename P = protocol>
        requires is_recv_v<P> && is_keyed_step_v<P>
    [[nodiscard]] constexpr auto recv(Poll poll) && {
        static_assert(PollRead<Poll, resource_type, std::size_t>,
                      "fixy::session::diagnostic [Crash_Read_Must_Poll]: recv(): the read of a keyed message is not "
                      "a polling read, std::optional<std::size_t>(Resource&).  A read of another shape waits inside "
                      "the transport, where no crash is seen, so a dead peer blocks it for ever.");
        const std::optional<std::size_t> word = detail::crash_transport::await_payload<Position, Peer, Reliable>(
            poll, inner_.resource(), *peer_cell_, counts_.received, inner_.watch_endpoint());
        auto next = std::move(inner_).recv([&word](resource_type&) noexcept -> std::optional<std::size_t> { return word; });
        if constexpr (keyed_step_has_value_v<P>) {
            return wrap_<detail::crash_transport::payload_to_receive<Peer>>(std::move(next), one_more_received_());
        } else {
            return wrap_(std::move(next), one_more_received_());
        }
    }

    // A crash branch head gives the crash record.  No transport runs.
    template <typename P = protocol>
        requires is_recv_v<P> && is_crash_payload_v<typename P::message_type>
    [[nodiscard]] constexpr auto recv() && {
        using Record = typename P::message_type;
        static_assert(std::is_same_v<typename Record::peer, Peer>,
                      "fixy::session::diagnostic [Crash_Branch_Unwatched]: this crash branch names a role other "
                      "than the watched peer, so no detector can have triggered it.");
        const CrashCause cause = peer_cell_->crash_cause().value_or(CrashCause::Unknown);
        auto [record, next] =
            std::move(inner_).recv([cause](resource_type&) noexcept -> std::optional<Record> { return Record{cause}; });
        return std::pair{record, wrap_(std::move(next), counts_)};
    }

    // ── branch ───────────────────────────────────────────────────────
    //
    // Poll is a polling read of a word, std::optional<std::size_t>(Resource&).
    // It returns a wire word when a message from the peer is queued, and no
    // value otherwise.  Between polls the decorator waits through the watch
    // and checks the peer.  A declared read would wait inside the transport,
    // where no crash is seen, so the decorator refuses it.
    //
    // The decorator reads the word, finds the branch that it names
    // (branch_of_wire_word in fixy/session/Handle.h), and enters that
    // branch of the inner handle with pick_local.  A crash branch is no
    // label, so no word names it.  On a crash the decorator enters the crash
    // branch with pick_local, and no word is read.  A positional branch that
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
        static_assert(PollRead<Poll, resource_type, std::size_t>,
                      "fixy::session::diagnostic [Crash_Read_Must_Poll]: branch(): the read of the word is not a "
                      "polling read, std::optional<std::size_t>(Resource&).  A read of another shape waits inside "
                      "the transport, where no crash is seen, so a dead peer blocks it for ever.  Return the word "
                      "when a message is queued, and no value otherwise.");
        using OfferSender = std::conditional_t<std::is_same_v<offer_sender_t<P>, AnonymousPeer>, Peer, offer_sender_t<P>>;
        constexpr std::size_t message_branches = detail::crash_transport::message_branch_count<P>::value;
        constexpr bool is_watched = !reliable_set_contains_v<Reliable, OfferSender>;
        static_assert(!is_watched || std::is_same_v<OfferSender, Peer>,
                      "fixy::session::diagnostic [Crash_Sender_Unwatched]: this Offer has an unreliable sender "
                      "that is not the watched peer.");

        // A word is one more message received.  The crash branch receives
        // none: it runs because the count of the report was reached.
        detail::crash_transport::message_counts counts = counts_;
        // Enters label branch I.  A positional branch that opens with a
        // reception, and a keyed branch whose payload is not void, still
        // wait for the rest of the message of the label.
        const auto enter = [this, &handler, &counts]<std::size_t I>(std::integral_constant<std::size_t, I>) {
            auto branch_handle = std::move(inner_).template pick_local<I>();
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
                                                std::move(branch_handle), counts));
            } else {
                return std::invoke(handler, wrap_(std::move(branch_handle), counts));
            }
        };
        const auto take_word = [this, &enter, &counts](std::size_t word) {
            if (!detail::crash_transport::names_message_branch<P>(word)) [[unlikely]]
                detail::crash_transport::abort_on_crash_label(word, message_branches);
            counts = one_more_received_();
            return enter_label_(branch_of_wire_word<P>(word), enter, std::make_index_sequence<message_branches>{});
        };
        resource_type& resource = inner_.resource();
        std::optional<std::size_t> word;
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
                const std::optional<CrashWitness> witness = peer_cell_->witness();
                if (detail::crash_transport::queue_is_drained(witness, counts_.received)) break;
                word = std::invoke(poll, resource);
                if (word) break;
                if (!wait) wait.emplace(inner_.watch_endpoint());
                wait->poll();
            }
        }
        if (word) return take_word(*word);
        if constexpr (is_watched) {
            return std::invoke(handler,
                               wrap_(std::move(inner_).template pick_local<crash_branch_index_v<P, Peer>>(), counts));
        } else {
            detail::crash_transport::abort_on_reliable_peer_crash();
        }
    }

    // ── close ────────────────────────────────────────────────────────
    template <typename P = protocol>
        requires is_terminal_state_v<P>
    [[nodiscard]] constexpr resource_type close() && {
        return std::move(inner_).close();
    }

    // ── crash ────────────────────────────────────────────────────────
    //
    // Stops the local endpoint and tells the peer through `announce`, the
    // reporter of the cell that the peer watches.  The report carries the
    // number of messages this endpoint sent, which its handles counted, so
    // the peer takes its crash branch once every one of them has arrived.
    // The endpoint is then at the runtime type Stop, where no operation
    // exists, so the call gives back the Resource instead of a handle.
    // Rule r-↯ does not apply to a process that has already ended, so End
    // has no crash().  A crash never falls inside one message: after a
    // positional label whose branch opens with a send, or after the label
    // word of a keyed message with a value, the payload goes first.
    template <typename P = protocol>
        requires(!is_terminal_state_v<P>)
    [[nodiscard]] resource_type crash(CrashCause cause, CrashReporter&& announce) && {
        static_assert(!reliable_set_contains_v<Reliable, Self>,
                      "fixy::session::diagnostic [Crash_Of_Reliable_Role]: crash(): the local role is in the "
                      "reliable set.  Rule r-↯ lets only a role outside the reliable set crash.  Remove the "
                      "role from the reliable set, or do not crash it.");
        static_assert(!std::is_same_v<Position, detail::crash_transport::payload_to_send>,
                      "fixy::session::diagnostic [Crash_Splits_A_Message]: crash(): this endpoint sent a label, "
                      "and the payload of that label has not gone: the payload that opens its branch, or the "
                      "value of a keyed message.  The label and the payload are one message, so a crash here "
                      "delivers half of it, and the peer waits in a position with no crash branch.  Send the "
                      "payload first, then crash.");
        resource_type resource = std::forward<resource_type>(inner_.resource());
        std::move(inner_).detach(detach_reason::LocalCrashStop{});
        static_cast<void>(std::move(announce).report(cause, static_cast<MessageCount>(counts_.sent)));
        return std::forward<resource_type>(resource);
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

template <typename Proto, typename Self, typename Peer, typename Reliable = NoReliableRoles,
          AbandonmentPolicy Policy = DefaultAbandonmentPolicy, typename Resource>
    requires CrashSessionAdmissible<Proto, Self, Peer, Reliable> && SessionResource<Resource>
[[nodiscard]] constexpr auto mint_crash_session(Resource resource, const PeerCrashCell& peer_cell,
                                                std::source_location loc = std::source_location::current()) noexcept {
    return detail::late_door_t<CrashSessionDoor, Proto>::template open_<Proto, Self, Peer, Reliable, Policy>(
        std::forward<Resource>(resource), peer_cell, loc);
}

// The decorator keeps the cell's address, so a temporary cell would
// dangle before the first step.
template <typename Proto, typename Self, typename Peer, typename Reliable = NoReliableRoles,
          AbandonmentPolicy Policy = DefaultAbandonmentPolicy, typename Resource>
void mint_crash_session(Resource, const PeerCrashCell&&, std::source_location = std::source_location::current()) =
    delete("[Crash_Cell_Temporary] mint_crash_session: the detector cell is a temporary.  The decorator keeps its "
           "address, so the cell must outlive every handle of the session.");

// ── The door of the crash mint ───────────────────────────────────────
//
// mint_crash_session gets the decorator through this class.  The member
// of the class is private and static.  The one friend of the class is
// the mint, which does the admission check of a crash session before it
// calls the member.  The member does that check again, opens the plain
// handle with mint_session_handle, and puts the decorator around it.  No
// other scope can put a handle under crash-stop semantics.  The class is
// final, and no object of it exists.
//
// A friend declaration of a constrained function template must give the
// same constraint, and it cannot have a default argument.  For this
// reason, the class is after the mint.  The mint names the class through
// detail::late_door_t.
class CrashSessionDoor final {
    CrashSessionDoor() = delete("the crash door holds static members only, and no object of it exists");
    CrashSessionDoor(const CrashSessionDoor&) = delete("the crash door holds static members only");
    CrashSessionDoor& operator=(const CrashSessionDoor&) = delete("the crash door holds static members only");
    CrashSessionDoor(CrashSessionDoor&&) = delete("the crash door holds static members only");
    CrashSessionDoor& operator=(CrashSessionDoor&&) = delete("the crash door holds static members only");
    constexpr ~CrashSessionDoor() noexcept {}

    template <typename Proto, typename Self, typename Peer, typename Reliable, AbandonmentPolicy Policy,
              typename Resource>
        requires CrashSessionAdmissible<Proto, Self, Peer, Reliable> && SessionResource<Resource>
    friend constexpr auto mint_crash_session(Resource resource, const PeerCrashCell& peer_cell,
                                             std::source_location loc) noexcept;

    // Opens the plain handle of Proto, and puts around it the decorator
    // that reads the cell of the watched peer.
    template <typename Proto, typename Self, typename Peer, typename Reliable, AbandonmentPolicy Policy,
              typename Resource>
    [[nodiscard]] static constexpr auto open_(Resource resource, const PeerCrashCell& peer_cell,
                                              std::source_location loc) noexcept {
        static_assert(CrashSessionAdmissible<Proto, Self, Peer, Reliable>,
                      "fixy::session::diagnostic [Crash_Session_Refused]: the crash door accepts only a session "
                      "that mint_crash_session accepts.");
        auto inner = mint_session_handle<Proto, Resource, Policy>(std::forward<Resource>(resource), loc);
        return CrashWatched<decltype(inner), Self, Peer, Reliable>{std::move(inner), peer_cell};
    }
};

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
