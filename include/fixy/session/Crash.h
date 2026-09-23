#pragma once

// Crash-stop failures for session protocols.
//
// The model is crash-stop.  A participant can halt at any point and
// never recovers.  A survivor detects the halt only when it tries to
// receive from the halted peer.  Links between live participants stay
// reliable.  Byzantine behaviour is out of scope.
//
// The authority for every rule here is Barwell, Hou, Yoshida and Zhou,
// "Crash-Stop Failures in Asynchronous Multiparty Session Types", LMCS
// 21(2), 2025.  The liveness definition comes from the technical report
// of Barwell, Scalas, Yoshida and Zhou, "Generalised Multiparty Session
// Types with Crash-Stop Failures", arXiv 2207.02015, version of
// 2023-02-22.
//
// ── What the protocol level says ────────────────────────────────────
//
// A crash-handling branch of an Offer is Recv<Crash<Peer>, K>.  The
// survivor takes it when Peer has crashed and no message from Peer is
// left in the incoming queue (rule r-rcv-⊙, LMCS 2025 Fig. 4).  K is the
// recovery continuation.  A send to a crashed peer is lost, not failed
// (rule r-send-↯).
//
// The crash label is a pseudo-message.  These rules follow (LMCS 2025
// p. 10, Def. 4.3 on p. 11, Def. 4.4 on p. 12):
//
//   1. No endpoint can send it.  Send<Crash<P>, K> is ill-formed at
//      every position, in every session.
//   2. No choice can be a pure crash branch.  An Offer whose every
//      branch is a crash branch has no label the peer can send, so it
//      counts as an empty choice.
//   3. A reception from an unreliable sender has a crash branch for
//      that sender.  Projection puts one there.  A bare Recv from an
//      unreliable peer is therefore refused: it must be an Offer with a
//      crash branch.  A crash branch names the sender of its Offer,
//      because the crash label in the choice of q reports the crash of
//      q and of no other role.
//   4. A reception of the crash label outside an Offer is a singleton
//      crash choice, and the calculus forbids it.
//   5. A reception from a reliable sender has no crash branch for that
//      sender.  Projection adds none, and rule Sub-& forbids a subtype
//      to add one, so such a branch is untypable.
//
// Rules 1 and 2 hold in every session, through is_well_formed and
// is_empty_choice.  The two encoding rules below also hold in every
// session: the transition algebra refuses a label branch after a branch
// that is no label, and two branches that receive the same payload.
// Rule 4 holds for every protocol that is_crash_well_formed accepts.
// Rules 3 and 5 need the set of reliable roles, so they hold where a
// crash-aware session is minted (CrashTransport.h).
//
// Two more rules come from the encoding, not from the calculus.  The
// calculus names labels, and an Offer here numbers its branches:
//
//   6. The crash branches of an Offer come after its message branches.
//      The peer drops the crash branches when it takes the dual, so the
//      message branches must keep their indices on both sides.
//   7. An Offer has at most one crash branch for each peer, so that the
//      crash branch index is a function of the peer.
//
// A message branch of an Offer whose head is Recv<T, K> is one message:
// the label and the payload T travel together.  The crash branch covers
// that payload, so rule 3 continues at K and not at the head.  A
// crash-aware transport must deliver the label and the payload of a
// branch head as one message.
//
// Stop is the type of a crashed endpoint.  It is runtime syntax (LMCS
// 2025 Fig. 6, p. 9): a protocol written at design time never contains
// it, so is_well_formed refuses it and no handle is ever positioned at
// it.  An endpoint that crashes gives back its Resource instead.  Stop
// is terminal and self-dual.  It is NOT the bottom of the subtype order:
// the calculus has only stop ⩽ stop (rule Sub-stop, Def. 4.4).  A
// crashed endpoint cannot stand in for a live one, because the peer
// that receives from it is stuck unless that peer has a crash branch.
//
// ── What the crash cause is ─────────────────────────────────────────
//
// The calculus has no crash classes.  Crucible records how a peer
// stopped, because replay must tell an abort from an error return.
// That record is CrashCause, and it is metadata on the crash branch:
// the value the survivor receives through Recv<Crash<Peer>, K> carries
// it.  It is not in any type, it orders nothing, and no rule reads it.
// It is not a lattice and not a fixy atom.
//
// ── What liveness means under crashes ───────────────────────────────
//
// A liveness statement over a crash-aware session uses Def. 17 of the
// 2023-02-22 technical report (p. 12).  A non-crashing path is fair
// when
//
//   (1) an enabled message transmission eventually fires, and
//   (2) an enabled crash detection s[p]⊙q eventually fires.
//
// Clause (2) is missing from the published CONCUR 2022 version.  The
// report's footnote 2 says so.  Without it, a path that ignores a
// detected crash forever counts as fair.  The report was revised on
// 2026-08-24.  That revision was not compared with the 2023 version, so
// this header states the 2023 wording.  A path is live (Def. 18, p. 13)
// when every enabled send eventually fires and every enabled receive of
// a label other than crash eventually fires.  A receive that waits only
// for a crash detection need not fire.
//
// ── How the runtime uses this ───────────────────────────────────────
//
// The set of reliable roles comes from the deployment.  A role that
// Canopy replicates, for example a role backed by a consensus group, can
// be declared reliable, and then no crash branch is needed for it.  A
// role that runs on one Relay is unreliable, because the Relay can die.
//
// The Keeper's failure detector marks a peer crashed (PeerCrashCell in
// CrashTransport.h).  The survivor then runs the crash branch that the
// protocol declares.
//
// Recovery after a crash is outside the calculus.  No paper from 2022
// to 2026 gives a type system for crash-recover.  The intended path is
// a supervisor restart in the style of Neykova and Yoshida, "Let It
// Recover" (CC 2017): the recovery branch ends the session, and the
// Keeper starts a new session with a fresh endpoint.  No type in this
// header claims more than that.
//
// ── What the ported source did wrong ────────────────────────────────
//
// crucible/sessions/SessionCrash.h made Stop the bottom of the subtype
// order, carried the crash class as a type parameter ordered by a
// lattice, checked Offers only (a bare Recv from an unreliable peer
// passed), never read the reliable set, and let a Select carry a crash
// branch through duality.  Each of those contradicts the calculus.

#include <fixy/session/Payload.h>
#include <fixy/session/Protocol.h>

#include <foundation/contracts/Armed.h>

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

namespace fixy::session {

// ── Stop ─────────────────────────────────────────────────────────────

struct Stop {};

// Stop is runtime syntax, so no design-time protocol may contain it.  The
// registration below gives its dual, its terminal kind and its place in
// refinement.  This specialization keeps it out of every design-time
// protocol.
template <typename LoopCtx>
struct is_well_formed<Stop, LoopCtx> : std::false_type {};

template <typename P>
struct is_stop : std::bool_constant<std::is_same_v<P, Stop>> {};
template <VendorBackend V, typename P>
struct is_stop<VendorPinned<V, P>> : is_stop<P> {};

template <typename P>
inline constexpr bool is_stop_v = is_stop<P>::value;

// ── The crash label and its metadata ─────────────────────────────────

// How the stopped peer ended, as the detector saw it.  The byte values
// are the values of the crash lane in the session event log, so a log
// that crucible/sessions/SessionEventLog.h wrote decodes unchanged.  The
// old tree wrote 3 for a crash graded "no throw", which is a
// contradiction.  It decodes as Unknown.
enum class CrashCause : std::uint8_t {
    Abort = 0,
    Throw = 1,
    ErrorReturn = 2,
    Unknown = 3,
};

inline constexpr std::uint8_t crash_cause_top_value = static_cast<std::uint8_t>(CrashCause::Unknown);

// The value the survivor receives on a crash branch.  Peer is the
// crashed role.  The cause is the metadata above.
template <typename Peer>
struct Crash {
    using peer = Peer;
    CrashCause cause = CrashCause::Unknown;
};

template <typename T>
struct is_crash_payload : std::false_type {};
template <typename Peer>
struct is_crash_payload<Crash<Peer>> : std::true_type {};

template <typename T>
inline constexpr bool is_crash_payload_v = is_crash_payload<T>::value;

// ── The registrations ────────────────────────────────────────────────
//
// Stop is a terminal that absorbs a suffix: a crashed endpoint never
// resumes, so composition keeps it.  Its dual is itself, and refinement
// relates it only to itself (rule Sub-stop).
//
// The crash label is a payload that no endpoint sends (rule 1) and that
// is no label a peer can send (rule 2).  The first refuses the plain dual
// of an endpoint with crash branches, which is correct: the peer of such
// an endpoint is its crash dual (crash_dual_t below).  The second makes
// an Offer of crash branches only an empty choice, and gives rule Sub-&
// its two side conditions in refinement.

namespace combinators {

inline constexpr ::foundation::algebra::transition::combinator stop{
    .shape = ^^Stop,
    .kind = ::foundation::algebra::transition::shape_kind::terminal,
    .dual = ^^Stop,
    .absorbs_suffix = true};

inline constexpr ::foundation::algebra::transition::payload_rule crash_label{
    .shape = ^^Crash, .is_sendable = false, .is_label = false};

}  // namespace combinators

// A crash branch of an Offer.
template <typename B>
struct is_crash_branch : std::false_type {};
template <typename Peer, typename K>
struct is_crash_branch<Recv<Crash<Peer>, K>> : std::true_type {};

template <typename B>
inline constexpr bool is_crash_branch_v = is_crash_branch<B>::value;

namespace detail::crash {

template <typename... Bs>
inline constexpr bool every_branch_is_crash_v = (is_crash_branch_v<Bs> && ...);

}  // namespace detail::crash

// ── Reliable roles and unavailable queues ────────────────────────────

// Roles assumed never to crash inside the protocol's scope.  A peer in
// the set needs no crash branch, and may not have one.
template <typename... Roles>
struct ReliableSet {
    static constexpr std::size_t size = sizeof...(Roles);
};

using NoReliableRoles = ReliableSet<>;

template <typename T>
struct is_reliable_set : std::false_type {};
template <typename... Roles>
struct is_reliable_set<ReliableSet<Roles...>> : std::true_type {};

namespace detail::crash {

template <typename Reliable, typename Role>
struct reliable_contains;
template <typename... Roles, typename Role>
struct reliable_contains<ReliableSet<Roles...>, Role> : std::bool_constant<(std::is_same_v<Roles, Role> || ...)> {};

}  // namespace detail::crash

template <typename Reliable, typename Role>
inline constexpr bool reliable_set_contains_v = detail::crash::reliable_contains<Reliable, Role>::value;

// The queue into a crashed recipient (⊘ in LMCS 2025 Fig. 2).  A send
// into it is dropped rather than delivered (rule r-send-↯).  The type
// names the state for a runtime queue model.  CrashTransport.h does the
// drop.
template <typename Peer>
struct UnavailableQueue {
    using peer = Peer;
};

template <typename T>
struct is_unavailable_queue : std::false_type {};
template <typename Peer>
struct is_unavailable_queue<UnavailableQueue<Peer>> : std::true_type {};

// ── Crash well-formedness: rules 1, 2, 4, 6 and 7 ────────────────────
//
// The walk refuses a combinator it does not know.  A header that adds a
// combinator adds its specialization here, or every crash-aware session
// refuses a protocol that holds the combinator.

namespace detail::crash {

template <typename B>
struct branch_crash_peer {
    using type = void;
};
template <typename Peer, typename K>
struct branch_crash_peer<Recv<Crash<Peer>, K>> {
    using type = Peer;
};

template <typename B>
using branch_crash_peer_t = typename branch_crash_peer<B>::type;

template <typename P>
struct structure : std::false_type {};

template <typename B>
struct branch_structure : structure<B> {};
template <typename Peer, typename K>
struct branch_structure<Recv<Crash<Peer>, K>> : structure<K> {};

// Rule 6: no message branch follows a crash branch.
template <typename... Bs>
consteval bool crash_branches_trail() {
    constexpr bool crash_at[] = {false, is_crash_branch_v<Bs>...};
    bool crash_seen = false;
    for (std::size_t i = 1; i < std::size(crash_at); ++i) {
        if (crash_at[i]) {
            crash_seen = true;
        } else if (crash_seen) {
            return false;
        }
    }
    return true;
}

// Rule 7: one crash branch for each peer.
template <typename Peer, typename... Bs>
inline constexpr std::size_t crash_branches_for_v = (std::size_t{std::is_same_v<branch_crash_peer_t<Bs>, Peer>} + ...
                                                     + std::size_t{0});

template <typename... Bs>
inline constexpr bool crash_peers_distinct_v =
    ((!is_crash_branch_v<Bs> || crash_branches_for_v<branch_crash_peer_t<Bs>, Bs...> == 1) && ...);

template <typename... Bs>
struct offer_structure
    : std::bool_constant<(branch_structure<Bs>::value && ...) && !every_branch_is_crash_v<Bs...>
                         && crash_branches_trail<Bs...>() && crash_peers_distinct_v<Bs...>> {};

template <>
struct structure<End> : std::true_type {};
template <>
struct structure<Continue> : std::true_type {};
template <typename T, typename K>
struct structure<Send<T, K>> : std::bool_constant<!is_crash_payload_v<T> && structure<K>::value> {};
// Rule 4: a bare reception of the crash label is refused.
template <typename T, typename K>
struct structure<Recv<T, K>> : std::bool_constant<!is_crash_payload_v<T> && structure<K>::value> {};
template <typename... Bs>
struct structure<Select<Bs...>> : std::bool_constant<(structure<Bs>::value && ...)> {};
template <typename... Bs>
struct structure<Offer<Bs...>> : offer_structure<Bs...> {};
template <typename Role, typename... Bs>
struct structure<Offer<Sender<Role>, Bs...>> : offer_structure<Bs...> {};
template <typename B>
struct structure<Loop<B>> : structure<B> {};
template <VendorBackend V, typename P>
struct structure<VendorPinned<V, P>> : structure<P> {};

}  // namespace detail::crash

// True when P is well-formed and obeys rules 1, 2, 4, 6 and 7.  Rules 3
// and 5 need the reliable roles, which is_crash_covered reads.
template <typename P>
struct is_crash_well_formed : std::bool_constant<is_well_formed_v<P> && detail::crash::structure<P>::value> {};

template <typename P>
inline constexpr bool is_crash_well_formed_v = is_crash_well_formed<P>::value;

// ── Delegation ───────────────────────────────────────────────────────
//
// The crash-stop theory that these rules follow has no delegation (LMCS
// 2025, footnote 2 on p. 11).  A delegated endpoint has peers of its
// own, and no detector of this session watches them, so the crash of
// such a peer leaves the holder waiting for ever.  The coverage walk
// below does not look inside a payload.  A crash-aware session refuses
// each payload that payload_conveys_delegation_v of
// fixy/session/Payload.h accepts: the hand-off marker, a session handle
// or a pointer to one, and a carrier with content that the query cannot
// read.  Barwell, Scalas, Yoshida and Zhou (CONCUR 2022) type delegation
// with crashes, and this tree does not carry that system.

namespace detail::crash {

// The primary refuses, so a combinator this walk does not know is not
// admitted.
template <typename P>
struct delegation_free : std::false_type {};
template <>
struct delegation_free<End> : std::true_type {};
template <>
struct delegation_free<Continue> : std::true_type {};
template <typename T, typename K>
struct delegation_free<Send<T, K>> : std::bool_constant<!payload_conveys_delegation_v<T> && delegation_free<K>::value> {};
template <typename T, typename K>
struct delegation_free<Recv<T, K>> : std::bool_constant<!payload_conveys_delegation_v<T> && delegation_free<K>::value> {};
template <typename... Bs>
struct delegation_free<Select<Bs...>> : std::bool_constant<(delegation_free<Bs>::value && ...)> {};
template <typename... Bs>
struct delegation_free<Offer<Bs...>> : std::bool_constant<(delegation_free<Bs>::value && ...)> {};
template <typename Role, typename... Bs>
struct delegation_free<Offer<Sender<Role>, Bs...>> : std::bool_constant<(delegation_free<Bs>::value && ...)> {};
template <typename B>
struct delegation_free<Loop<B>> : delegation_free<B> {};
template <VendorBackend V, typename P>
struct delegation_free<VendorPinned<V, P>> : delegation_free<P> {};

}  // namespace detail::crash

// ── Crash coverage: rules 3 and 5 ────────────────────────────────────
//
// On a binary channel the sender of a Recv or of an unannotated Offer is
// the channel peer.  An Offer that names its sender with Sender<Role> is
// checked against that role.

namespace detail::crash {

template <typename Peer, typename... Bs>
inline constexpr bool offers_crash_branch_for_v = (std::is_same_v<branch_crash_peer_t<Bs>, Peer> || ...);

// Rule 5, per branch: a crash branch names an unreliable role.
template <typename Reliable, typename... Bs>
inline constexpr bool no_crash_branch_for_reliable_v =
    ((!is_crash_branch_v<Bs> || !reliable_contains<Reliable, branch_crash_peer_t<Bs>>::value) && ...);

// Rule 3, per branch: a crash branch names the sender of its Offer.
template <typename OfferSender, typename... Bs>
inline constexpr bool crash_branches_name_sender_v =
    ((!is_crash_branch_v<Bs> || std::is_same_v<branch_crash_peer_t<Bs>, OfferSender>) && ...);

template <typename P, typename Peer, typename Reliable>
struct coverage : std::false_type {};

// A message branch whose head is a reception is one message with the
// label, so the Offer's crash branch covers the head.
template <typename B, typename Peer, typename Reliable>
struct branch_coverage : coverage<B, Peer, Reliable> {};
template <typename T, typename K, typename Peer, typename Reliable>
struct branch_coverage<Recv<T, K>, Peer, Reliable> : coverage<K, Peer, Reliable> {};

template <typename OfferSender, typename Peer, typename Reliable, typename... Bs>
struct offer_coverage
    : std::bool_constant<(reliable_contains<Reliable, OfferSender>::value || offers_crash_branch_for_v<OfferSender, Bs...>)
                         && crash_branches_name_sender_v<OfferSender, Bs...>
                         && no_crash_branch_for_reliable_v<Reliable, Bs...>
                         && (branch_coverage<Bs, Peer, Reliable>::value && ...)> {};

template <typename Peer, typename Reliable>
struct coverage<End, Peer, Reliable> : std::true_type {};
template <typename Peer, typename Reliable>
struct coverage<Continue, Peer, Reliable> : std::true_type {};
template <typename T, typename K, typename Peer, typename Reliable>
struct coverage<Send<T, K>, Peer, Reliable> : coverage<K, Peer, Reliable> {};
// Rule 3: a bare reception needs a reliable sender.
template <typename T, typename K, typename Peer, typename Reliable>
struct coverage<Recv<T, K>, Peer, Reliable>
    : std::bool_constant<reliable_contains<Reliable, Peer>::value && coverage<K, Peer, Reliable>::value> {};
template <typename... Bs, typename Peer, typename Reliable>
struct coverage<Select<Bs...>, Peer, Reliable> : std::bool_constant<(coverage<Bs, Peer, Reliable>::value && ...)> {};
template <typename... Bs, typename Peer, typename Reliable>
struct coverage<Offer<Bs...>, Peer, Reliable> : offer_coverage<Peer, Peer, Reliable, Bs...> {};
template <typename Role, typename... Bs, typename Peer, typename Reliable>
struct coverage<Offer<Sender<Role>, Bs...>, Peer, Reliable> : offer_coverage<Role, Peer, Reliable, Bs...> {};
template <typename B, typename Peer, typename Reliable>
struct coverage<Loop<B>, Peer, Reliable> : coverage<B, Peer, Reliable> {};
template <VendorBackend V, typename P, typename Peer, typename Reliable>
struct coverage<VendorPinned<V, P>, Peer, Reliable> : coverage<P, Peer, Reliable> {};

}  // namespace detail::crash

// The question "does Proto, on a channel to Peer and with these
// reliable roles, handle each crash it can see and no crash it cannot".
// It is a type so that the predicate below takes one argument and can
// hold an armed cell.
template <typename Proto, typename Peer, typename Reliable>
struct CrashCoverage {};

template <typename Q>
struct is_crash_covered : std::false_type {};
template <typename Proto, typename Peer, typename... Roles>
struct is_crash_covered<CrashCoverage<Proto, Peer, ReliableSet<Roles...>>>
    : std::bool_constant<detail::crash::coverage<Proto, Peer, ReliableSet<Roles...>>::value> {};

template <typename Proto, typename Peer, typename Reliable>
inline constexpr bool every_reception_handles_crash_v = is_crash_covered<CrashCoverage<Proto, Peer, Reliable>>::value;

// ── Crash branch index ───────────────────────────────────────────────

namespace detail::crash {

template <typename Peer, typename... Bs>
consteval std::size_t crash_branch_index_of() {
    constexpr bool hits[] = {false, std::is_same_v<branch_crash_peer_t<Bs>, Peer>...};
    for (std::size_t i = 1; i < std::size(hits); ++i) {
        if (hits[i]) return i - 1;
    }
    return sizeof...(Bs);
}

template <typename OfferType, typename Peer>
struct crash_branch_index;
template <typename... Bs, typename Peer>
struct crash_branch_index<Offer<Bs...>, Peer>
    : std::integral_constant<std::size_t, crash_branch_index_of<Peer, Bs...>()> {
    static constexpr std::size_t branch_count = sizeof...(Bs);
};
template <typename Role, typename... Bs, typename Peer>
struct crash_branch_index<Offer<Sender<Role>, Bs...>, Peer>
    : std::integral_constant<std::size_t, crash_branch_index_of<Peer, Bs...>()> {
    static constexpr std::size_t branch_count = sizeof...(Bs);
};
template <VendorBackend V, typename P, typename Peer>
struct crash_branch_index<VendorPinned<V, P>, Peer> : crash_branch_index<P, Peer> {};

}  // namespace detail::crash

// True when the Offer has a crash branch for Peer.
template <typename OfferType, typename Peer>
inline constexpr bool offer_has_crash_branch_v = detail::crash::crash_branch_index<OfferType, Peer>::value
                                              < detail::crash::crash_branch_index<OfferType, Peer>::branch_count;

// The position, among the real branches, of the crash branch for Peer.
// A Sender tag is not a branch, so this is the index that branch() and
// pick_local() read.
template <typename OfferType, typename Peer>
inline constexpr std::size_t crash_branch_index_v = [] {
    static_assert(offer_has_crash_branch_v<OfferType, Peer>,
                  "fixy::session::diagnostic [Crash_Branch_Missing]: crash_branch_index_v<Offer, Peer>: the "
                  "Offer has no Recv<Crash<Peer>, K> branch.  Add one for the peer whose crash this "
                  "reception must survive, or declare the peer reliable.");
    return detail::crash::crash_branch_index<OfferType, Peer>::value;
}();

// ── Duality modulo crash branches ────────────────────────────────────
//
// The peer of an endpoint with crash branches cannot select the crash
// label, so its protocol is the dual of this endpoint with the crash
// branches removed.  Two endpoints are crash duals when their protocols
// agree after the crash branches of each side are removed.  Each side
// keeps its own recovery.  Rule 6 keeps the message branches at the same
// indices on both sides.

namespace detail::crash {

template <typename P>
struct erase;

template <typename P>
using erase_t = typename erase<P>::type;

template <typename B>
using kept_branch_t = std::conditional_t<is_crash_branch_v<B>, std::tuple<>, std::tuple<erase_t<B>>>;

template <typename Tuple, typename Prefix>
struct rebuild_offer;
template <typename... Bs>
struct rebuild_offer<std::tuple<Bs...>, void> {
    using type = Offer<Bs...>;
};
template <typename... Bs, typename Role>
struct rebuild_offer<std::tuple<Bs...>, Sender<Role>> {
    using type = Offer<Sender<Role>, Bs...>;
};

template <typename Prefix, typename... Bs>
using erased_offer_t =
    typename rebuild_offer<decltype(std::tuple_cat(std::declval<kept_branch_t<Bs>>()...)), Prefix>::type;

template <>
struct erase<End> {
    using type = End;
};
template <>
struct erase<Continue> {
    using type = Continue;
};
template <typename T, typename K>
struct erase<Send<T, K>> {
    using type = Send<T, erase_t<K>>;
};
template <typename T, typename K>
struct erase<Recv<T, K>> {
    using type = Recv<T, erase_t<K>>;
};
template <typename... Bs>
struct erase<Select<Bs...>> {
    using type = Select<erase_t<Bs>...>;
};
template <typename... Bs>
struct erase<Offer<Bs...>> {
    using type = erased_offer_t<void, Bs...>;
};
template <typename Role, typename... Bs>
struct erase<Offer<Sender<Role>, Bs...>> {
    using type = erased_offer_t<Sender<Role>, Bs...>;
};
template <typename B>
struct erase<Loop<B>> {
    using type = Loop<erase_t<B>>;
};
template <VendorBackend V, typename P>
struct erase<VendorPinned<V, P>> {
    using type = VendorPinned<V, erase_t<P>>;
};

}  // namespace detail::crash

template <typename P>
using erase_crash_t = detail::crash::erase_t<P>;

template <typename P>
using crash_dual_t = dual_of_t<erase_crash_t<P>>;

template <typename P1, typename P2>
inline constexpr bool is_crash_dual_v = is_dual_v<erase_crash_t<P1>, erase_crash_t<P2>>;

// ── Subtyping side conditions (LMCS 2025 Def. 4.4) ───────────────────
//
// A subtype relation over crash-aware protocols conjoins these at each
// pair of positions it compares:
//
//   Sub-stop  Stop relates only to Stop.
//   Sub-&     The subtype Offer may have more branches than the
//             supertype.  The supertype Offer is not a pure crash
//             choice, and a crash branch of the subtype has a crash
//             branch for the same peer in the supertype.
//
// No other pair of shapes carries a condition that is special to
// crashes.  The side conditions are not a subtype relation.  They are
// what a relation owes the calculus in addition to its own rules.

namespace detail::crash {

template <typename P>
struct offer_crash_peers {
    using type = std::tuple<>;
};
template <typename... Bs>
struct offer_crash_peers<Offer<Bs...>> {
    using type = decltype(std::tuple_cat(
        std::declval<std::conditional_t<is_crash_branch_v<Bs>, std::tuple<branch_crash_peer_t<Bs>>, std::tuple<>>>()...));
};
template <typename Role, typename... Bs>
struct offer_crash_peers<Offer<Sender<Role>, Bs...>> : offer_crash_peers<Offer<Bs...>> {};

template <typename Peer, typename Tuple>
struct tuple_holds;
template <typename Peer, typename... Ts>
struct tuple_holds<Peer, std::tuple<Ts...>> : std::bool_constant<(std::is_same_v<Peer, Ts> || ...)> {};

template <typename SubPeers, typename SuperPeers>
struct crash_peers_included;
template <typename... SubPs, typename SuperPeers>
struct crash_peers_included<std::tuple<SubPs...>, SuperPeers>
    : std::bool_constant<(tuple_holds<SubPs, SuperPeers>::value && ...)> {};

template <typename P>
struct pure_crash_offer : std::false_type {};
template <typename... Bs>
struct pure_crash_offer<Offer<Bs...>> : std::bool_constant<every_branch_is_crash_v<Bs...>> {};
template <typename Role, typename... Bs>
struct pure_crash_offer<Offer<Sender<Role>, Bs...>> : std::bool_constant<every_branch_is_crash_v<Bs...>> {};

template <typename Sub, typename Super>
consteval bool refinement_side_conditions_hold() {
    using SubInner = protocol_inner_t<Sub>;
    using SuperInner = protocol_inner_t<Super>;
    if constexpr (is_stop_v<SubInner> || is_stop_v<SuperInner>) {
        return is_stop_v<SubInner> && is_stop_v<SuperInner>;
    } else if constexpr (is_offer_v<SubInner> && is_offer_v<SuperInner>) {
        return crash_peers_included<typename offer_crash_peers<SubInner>::type,
                                    typename offer_crash_peers<SuperInner>::type>::value
            && !pure_crash_offer<SuperInner>::value;
    } else {
        return true;
    }
}

}  // namespace detail::crash

template <typename Sub, typename Super>
struct CrashRefinement {};

template <typename Q>
struct is_crash_refinement_admissible : std::false_type {};
template <typename Sub, typename Super>
struct is_crash_refinement_admissible<CrashRefinement<Sub, Super>>
    : std::bool_constant<detail::crash::refinement_side_conditions_hold<Sub, Super>()> {};

template <typename Sub, typename Super>
inline constexpr bool crash_refinement_admissible_v =
    is_crash_refinement_admissible<CrashRefinement<Sub, Super>>::value;

}  // namespace fixy::session

// ── Armed cells ──────────────────────────────────────────────────────

namespace fixy::session::detail::crash::armed_witness {
struct Alice {};
struct Bob {};
struct Msg {};
using AliceCrash = Recv<Crash<Alice>, End>;
using Guarded = Offer<Recv<Msg, End>, AliceCrash>;
using PureCrash = Offer<AliceCrash>;
using CrashFirst = Offer<AliceCrash, Recv<Msg, End>>;
using GuardedLoop = Loop<Offer<Recv<Msg, Continue>, AliceCrash>>;
}  // namespace fixy::session::detail::crash::armed_witness

template <>
struct foundation::contracts::armed_cell<::fixy::session::is_stop> {
    using accepts = witnesses<::fixy::session::Stop>;
    using refuses = witnesses<::fixy::session::End, ::fixy::session::Send<int, ::fixy::session::Stop>>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::session::is_crash_payload> {
    using accepts = witnesses<::fixy::session::Crash<::fixy::session::detail::crash::armed_witness::Alice>>;
    using refuses = witnesses<int, ::fixy::session::detail::crash::armed_witness::Alice>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::session::is_crash_branch> {
    using accepts = witnesses<::fixy::session::detail::crash::armed_witness::AliceCrash>;
    using refuses =
        witnesses<::fixy::session::Recv<int, ::fixy::session::End>,
                  ::fixy::session::Send<::fixy::session::Crash<::fixy::session::detail::crash::armed_witness::Alice>,
                                        ::fixy::session::End>>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::session::is_reliable_set> {
    using accepts = witnesses<::fixy::session::ReliableSet<>,
                              ::fixy::session::ReliableSet<::fixy::session::detail::crash::armed_witness::Alice>>;
    using refuses = witnesses<int, std::tuple<::fixy::session::detail::crash::armed_witness::Alice>>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::session::is_unavailable_queue> {
    using accepts = witnesses<::fixy::session::UnavailableQueue<::fixy::session::detail::crash::armed_witness::Alice>>;
    using refuses = witnesses<int, ::fixy::session::detail::crash::armed_witness::Alice>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::session::is_crash_well_formed> {
    using accepts = witnesses<::fixy::session::End, ::fixy::session::detail::crash::armed_witness::Guarded,
                              ::fixy::session::detail::crash::armed_witness::GuardedLoop>;
    using refuses =
        witnesses<::fixy::session::Stop,
                  ::fixy::session::Send<::fixy::session::Crash<::fixy::session::detail::crash::armed_witness::Alice>,
                                        ::fixy::session::End>,
                  ::fixy::session::detail::crash::armed_witness::PureCrash,
                  ::fixy::session::detail::crash::armed_witness::AliceCrash,
                  ::fixy::session::detail::crash::armed_witness::CrashFirst>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::session::is_crash_covered> {
    using accepts = witnesses<
        ::fixy::session::CrashCoverage<::fixy::session::detail::crash::armed_witness::Guarded,
                                       ::fixy::session::detail::crash::armed_witness::Alice,
                                       ::fixy::session::ReliableSet<>>,
        ::fixy::session::CrashCoverage<::fixy::session::Recv<int, ::fixy::session::End>,
                                       ::fixy::session::detail::crash::armed_witness::Alice,
                                       ::fixy::session::ReliableSet<::fixy::session::detail::crash::armed_witness::Alice>>>;
    using refuses = witnesses<
        int,
        ::fixy::session::CrashCoverage<::fixy::session::Recv<int, ::fixy::session::End>,
                                       ::fixy::session::detail::crash::armed_witness::Alice,
                                       ::fixy::session::ReliableSet<>>,
        ::fixy::session::CrashCoverage<::fixy::session::detail::crash::armed_witness::Guarded,
                                       ::fixy::session::detail::crash::armed_witness::Bob,
                                       ::fixy::session::ReliableSet<>>,
        ::fixy::session::CrashCoverage<::fixy::session::detail::crash::armed_witness::Guarded,
                                       ::fixy::session::detail::crash::armed_witness::Alice,
                                       ::fixy::session::ReliableSet<::fixy::session::detail::crash::armed_witness::Alice>>>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::session::is_crash_refinement_admissible> {
    using accepts = witnesses<::fixy::session::CrashRefinement<::fixy::session::Stop, ::fixy::session::Stop>,
                              ::fixy::session::CrashRefinement<::fixy::session::detail::crash::armed_witness::Guarded,
                                                               ::fixy::session::detail::crash::armed_witness::Guarded>>;
    using refuses = witnesses<
        int, ::fixy::session::CrashRefinement<::fixy::session::Stop, ::fixy::session::End>,
        ::fixy::session::CrashRefinement<
            ::fixy::session::detail::crash::armed_witness::Guarded,
            ::fixy::session::Offer<::fixy::session::Recv<::fixy::session::detail::crash::armed_witness::Msg,
                                                         ::fixy::session::End>>>,
        ::fixy::session::CrashRefinement<::fixy::session::detail::crash::armed_witness::Guarded,
                                         ::fixy::session::detail::crash::armed_witness::PureCrash>>;
};
