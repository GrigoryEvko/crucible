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
//                 caller, so a linear payload such as a permission is
//                 never lost silently.
//   branch        The decorator polls for a label.  If the peer has
//                 crashed and no message from the peer is left, it takes
//                 the crash branch (rule r-rcv-⊙).  A message that arrived
//                 before the crash is delivered first.
//   recv          A crash branch head Recv<Crash<Peer>, K> gives the
//                 crash record from the detector.  Any other reception
//                 calls the transport.  The mint proved that such a
//                 reception is from a reliable peer, or is the payload of
//                 a label that already arrived.
//   crash         The local endpoint stops (rule r-↯).  Only a role
//                 outside the reliable set may do this.  The peer learns
//                 of it through the cell that the call marks.
//
// The detector is a PeerCrashCell.  The Keeper's failure detector marks
// it.  A test marks it by hand.  The decorator only reads it.
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
// ── Known limits ────────────────────────────────────────────────────
//
// The crash check and the transport call are two steps.  If the peer
// crashes between them, the transport delivers into the queue of a dead
// peer, and the payload is lost with that queue (rule r-↯ makes the
// queue unavailable).  The decorator returns a payload only when the
// crash was visible before the send.  A transport that must not lose a
// linear payload hands queued payloads back when it learns of the crash.

#include <fixy/session/Crash.h>
#include <fixy/session/Handle.h>

#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Armed.h>
#include <foundation/contracts/Pre.h>

#include <atomic>
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

// One byte of state on its own cache line.  The first report wins, so a
// crash that two detectors report keeps the cause of the first.
class PeerCrashCell : public ::foundation::Pinned<PeerCrashCell> {
    static constexpr std::uint8_t alive_state = 0xFF;

    alignas(64) std::atomic<std::uint8_t> state_{alive_state};

public:
    constexpr PeerCrashCell() noexcept = default;

    // Records the crash.  Returns true when this call recorded it, and
    // false when an earlier report already had.
    bool mark_crashed(CrashCause cause) noexcept {
        CRUCIBLE_PRE(static_cast<std::uint8_t>(cause) <= crash_cause_top_value);
        std::uint8_t expected = alive_state;
        return state_.compare_exchange_strong(expected, static_cast<std::uint8_t>(cause), std::memory_order_acq_rel,
                                              std::memory_order_acquire);
    }

    [[nodiscard]] bool has_crashed() const noexcept { return state_.load(std::memory_order_acquire) != alive_state; }

    // The cause, or no value while the peer is alive.  Only mark_crashed
    // writes the state, and its precondition keeps every written value
    // inside CrashCause, so the cast is total over what can be read.
    [[nodiscard]] std::optional<CrashCause> crash_cause() const noexcept {
        const std::uint8_t state = state_.load(std::memory_order_acquire);
        if (state == alive_state) return std::nullopt;
        return static_cast<CrashCause>(state);
    }
};

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
struct senders_watched : std::false_type {};

template <typename OfferSender, typename Peer, typename Reliable, typename... Bs>
inline constexpr bool offer_watched_v =
    (std::is_same_v<OfferSender, Peer> || reliable_set_contains_v<Reliable, OfferSender>)
    && (senders_watched<Bs, Peer, Reliable>::value && ...);

template <typename Peer, typename Reliable>
struct senders_watched<End, Peer, Reliable> : std::true_type {};
template <typename Peer, typename Reliable>
struct senders_watched<Continue, Peer, Reliable> : std::true_type {};
template <typename T, typename K, typename Peer, typename Reliable>
struct senders_watched<Send<T, K>, Peer, Reliable> : senders_watched<K, Peer, Reliable> {};
template <typename T, typename K, typename Peer, typename Reliable>
struct senders_watched<Recv<T, K>, Peer, Reliable> : senders_watched<K, Peer, Reliable> {};
template <typename... Bs, typename Peer, typename Reliable>
struct senders_watched<Select<Bs...>, Peer, Reliable>
    : std::bool_constant<(senders_watched<Bs, Peer, Reliable>::value && ...)> {};
template <typename... Bs, typename Peer, typename Reliable>
struct senders_watched<Offer<Bs...>, Peer, Reliable> : std::bool_constant<offer_watched_v<Peer, Peer, Reliable, Bs...>> {
};
template <typename Role, typename... Bs, typename Peer, typename Reliable>
struct senders_watched<Offer<Sender<Role>, Bs...>, Peer, Reliable>
    : std::bool_constant<offer_watched_v<Role, Peer, Reliable, Bs...>> {};
template <typename B, typename Peer, typename Reliable>
struct senders_watched<Loop<B>, Peer, Reliable> : senders_watched<B, Peer, Reliable> {};

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
// The Offer has no crash branch to take, and a wait would last for ever.
[[noreturn]] [[gnu::cold, gnu::noinline]] inline void abort_on_reliable_peer_crash() noexcept {
    std::fprintf(stderr,
                 "fixy::session: diagnostic [Crash_Of_Reliable_Peer]: the peer crashed, and this endpoint "
                 "counts it reliable, so the Offer has no crash branch and would wait for ever.  The two "
                 "endpoints disagree about the reliable set.  Give both the same set, or remove the peer "
                 "from it and add the crash branch.\n");
    std::abort();
}

[[noreturn]] [[gnu::cold, gnu::noinline]] inline void abort_on_crash_label(std::size_t label,
                                                                          std::size_t message_branches) noexcept {
    std::fprintf(stderr,
                 "fixy::session: crash transport received label %zu, but the Offer has only %zu message "
                 "branches.  A label at or after the first crash branch is the crash pseudo-message, which "
                 "no peer can send.  The two endpoints disagree about the protocol.\n",
                 label, message_branches);
    std::abort();
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
                              && detail::crash_transport::senders_watched<Proto, Peer, Reliable>::value
                              && detail::crash::delegation_free<Proto>::value;

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

template <typename Handle, typename Self, typename Peer, typename Reliable>
class CrashWatched;

// The result of a send.  `undelivered` holds the payload when the peer
// had crashed before the send, and is empty when the transport took it.
template <typename Next, typename T>
struct [[nodiscard]] CrashSend {
    Next next;
    std::optional<T> undelivered;
};

namespace detail::crash_transport {

template <typename Self, typename Peer, typename Reliable, typename Handle>
[[nodiscard]] constexpr auto make_crash_watched(Handle inner, const PeerCrashCell& cell) noexcept
    -> CrashWatched<Handle, Self, Peer, Reliable>;

}  // namespace detail::crash_transport

template <typename Handle, typename Self, typename Peer, typename Reliable>
class [[nodiscard]] CrashWatched {
    static_assert(is_reliable_set<Reliable>::value,
                  "fixy::session::diagnostic [Crash_Reliable_Set_Required]: CrashWatched<H, Self, Peer, Reliable>: "
                  "Reliable must be a ReliableSet<Roles...>.");

    Handle inner_;
    const PeerCrashCell* peer_cell_;

    template <typename, typename, typename, typename>
    friend class CrashWatched;

    template <typename FSelf, typename FPeer, typename FReliable, typename FHandle>
    friend constexpr auto detail::crash_transport::make_crash_watched(FHandle, const PeerCrashCell&) noexcept
        -> CrashWatched<FHandle, FSelf, FPeer, FReliable>;

    constexpr CrashWatched(Handle inner, const PeerCrashCell& cell) noexcept
        : inner_{std::move(inner)}, peer_cell_{&cell} {}

    template <typename Next>
    [[nodiscard]] constexpr auto wrap_(Next next) const noexcept {
        return CrashWatched<Next, Self, Peer, Reliable>{std::move(next), *peer_cell_};
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

    // ── send ─────────────────────────────────────────────────────────
    template <typename Transport, typename P = protocol>
        requires is_send_v<P> && std::is_invocable_v<Transport, resource_type&, typename P::message_type&&>
    [[nodiscard]] constexpr auto send(typename P::message_type value, Transport transport) && {
        using T = typename P::message_type;
        std::optional<T> undelivered;
        constexpr bool is_nothrow =
            std::is_nothrow_invocable_v<Transport, resource_type&, T&&> && std::is_nothrow_move_constructible_v<T>;
        auto next = std::move(inner_).send(std::move(value), [&](resource_type& resource, T&& payload) noexcept(is_nothrow) {
            if (peer_cell_->has_crashed()) {
                undelivered.emplace(std::move(payload));
            } else {
                std::invoke(transport, resource, std::move(payload));
            }
        });
        return CrashSend<decltype(wrap_(std::move(next))), T>{wrap_(std::move(next)), std::move(undelivered)};
    }

    // ── select ───────────────────────────────────────────────────────
    template <std::size_t I, typename Transport, typename P = protocol>
        requires is_select_v<P> && std::is_invocable_v<Transport, resource_type&, std::size_t>
    [[nodiscard]] constexpr auto select(Transport transport) && {
        constexpr bool is_nothrow = std::is_nothrow_invocable_v<Transport, resource_type&, std::size_t>;
        auto next = std::move(inner_).template select<I>([&](resource_type& resource, std::size_t label) noexcept(is_nothrow) {
            if (!peer_cell_->has_crashed()) std::invoke(transport, resource, label);
        });
        return wrap_(std::move(next));
    }

    // ── recv ─────────────────────────────────────────────────────────
    template <typename Transport, typename P = protocol>
        requires is_recv_v<P> && (!is_crash_payload_v<typename P::message_type>)
              && std::is_invocable_r_v<typename P::message_type, Transport, resource_type&>
    [[nodiscard]] constexpr auto recv(Transport transport) && {
        auto [value, next] = std::move(inner_).recv(std::move(transport));
        return std::pair{std::move(value), wrap_(std::move(next))};
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
        auto [record, next] = std::move(inner_).recv([cause](resource_type&) noexcept { return Record{cause}; });
        return std::pair{record, wrap_(std::move(next))};
    }

    // ── branch ───────────────────────────────────────────────────────
    //
    // Poll has the signature std::optional<std::size_t>(Resource&).  It
    // returns a label when a message from the peer is queued, and no
    // value otherwise.  The decorator spins with a pause between polls.
    // A transport that expects long waits parks inside Poll.
    template <typename Poll, typename Handler, typename P = protocol>
        requires is_offer_v<P> && std::is_invocable_r_v<std::optional<std::size_t>, Poll, resource_type&>
    constexpr auto branch(Poll poll, Handler handler) && {
        using OfferSender = std::conditional_t<std::is_same_v<offer_sender_t<P>, AnonymousPeer>, Peer, offer_sender_t<P>>;
        constexpr std::size_t message_branches = detail::crash_transport::message_branch_count<P>::value;
        constexpr bool is_watched = !reliable_set_contains_v<Reliable, OfferSender>;
        static_assert(!is_watched || std::is_same_v<OfferSender, Peer>,
                      "fixy::session::diagnostic [Crash_Sender_Unwatched]: this Offer has an unreliable sender "
                      "that is not the watched peer.");

        const PeerCrashCell* cell = peer_cell_;
        constexpr bool is_nothrow = std::is_nothrow_invocable_v<Poll, resource_type&>;
        auto label_of = [&poll, cell](resource_type& resource) noexcept(is_nothrow) -> std::size_t {
            const auto checked = [](std::size_t label) noexcept {
                if (label >= message_branches) [[unlikely]]
                    detail::crash_transport::abort_on_crash_label(label, message_branches);
                return label;
            };
            for (;;) {
                if (const std::optional<std::size_t> label = std::invoke(poll, resource)) return checked(*label);
                if (cell->has_crashed()) {
                    // A message queued before the crash still wins.
                    if (const std::optional<std::size_t> label = std::invoke(poll, resource)) return checked(*label);
                    if constexpr (is_watched) {
                        return crash_branch_index_v<P, Peer>;
                    } else {
                        detail::crash_transport::abort_on_reliable_peer_crash();
                    }
                }
                CRUCIBLE_SPIN_PAUSE;
            }
        };
        return std::move(inner_).branch(label_of, [this, &handler](auto branch_handle) {
            return std::invoke(handler, wrap_(std::move(branch_handle)));
        });
    }

    // ── close ────────────────────────────────────────────────────────
    template <typename P = protocol>
        requires is_terminal_state_v<P>
    [[nodiscard]] constexpr resource_type close() && {
        return std::move(inner_).close();
    }

    // ── crash ────────────────────────────────────────────────────────
    //
    // Stops the local endpoint and tells the peer through `announce`,
    // the cell the peer watches.  The endpoint is then at the runtime
    // type Stop, where no operation exists, so the call gives back the
    // Resource instead of a handle.  Rule r-↯ does not apply to a
    // process that has already ended, so End has no crash().
    template <typename P = protocol>
        requires(!is_terminal_state_v<P>)
    [[nodiscard]] resource_type crash(CrashCause cause, PeerCrashCell& announce) && {
        static_assert(!reliable_set_contains_v<Reliable, Self>,
                      "fixy::session::diagnostic [Crash_Of_Reliable_Role]: crash(): the local role is in the "
                      "reliable set.  Rule r-↯ lets only a role outside the reliable set crash.  Remove the "
                      "role from the reliable set, or do not crash it.");
        resource_type resource = std::forward<resource_type>(inner_.resource());
        std::move(inner_).detach(detach_reason::LocalCrashStop{});
        announce.mark_crashed(cause);
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

namespace detail::crash_transport {

template <typename Self, typename Peer, typename Reliable, typename Handle>
[[nodiscard]] constexpr auto make_crash_watched(Handle inner, const PeerCrashCell& cell) noexcept
    -> CrashWatched<Handle, Self, Peer, Reliable> {
    return CrashWatched<Handle, Self, Peer, Reliable>{std::move(inner), cell};
}

}  // namespace detail::crash_transport

// ── The mint ─────────────────────────────────────────────────────────

template <typename Proto, typename Self, typename Peer, typename Reliable = NoReliableRoles,
          AbandonmentPolicy Policy = DefaultAbandonmentPolicy, typename Resource>
    requires CrashSessionAdmissible<Proto, Self, Peer, Reliable> && SessionResource<Resource>
[[nodiscard]] constexpr auto mint_crash_session(Resource resource, const PeerCrashCell& peer_cell,
                                                std::source_location loc = std::source_location::current()) noexcept {
    return detail::crash_transport::make_crash_watched<Self, Peer, Reliable>(
        mint_session_handle<Proto, Resource, Policy>(std::forward<Resource>(resource), loc), peer_cell);
}

// The decorator keeps the cell's address, so a temporary cell would
// dangle before the first step.
template <typename Proto, typename Self, typename Peer, typename Reliable = NoReliableRoles,
          AbandonmentPolicy Policy = DefaultAbandonmentPolicy, typename Resource>
void mint_crash_session(Resource, const PeerCrashCell&&, std::source_location = std::source_location::current()) =
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
