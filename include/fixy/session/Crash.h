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
// crucible/sessions/_SessionCrash.h made Stop the bottom of the subtype
// order, carried the crash class as a type parameter ordered by a
// lattice, checked Offers only (a bare Recv from an unreliable peer
// passed), never read the reliable set, and let a Select carry a crash
// branch through duality.  Each of those contradicts the calculus.

#include <fixy/session/Payload.h>
#include <fixy/session/Protocol.h>

#include <foundation/contracts/Armed.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <tuple>
#include <type_traits>
#include <vector>

namespace fixy::session {

// ── Stop ─────────────────────────────────────────────────────────────
//
// Stop and its registration stand in fixy/session/Protocol.h, under the
// seal of the registry.  The registration gives its dual, its terminal
// kind that absorbs a suffix, and its place in refinement, and it marks
// Stop as not plain, so no design-time protocol holds it.
//
// Each trait of this header is an alias, and each _v form is a concept, so
// no program can specialize one to change its answer.  The registry and
// the crash walk below give every answer.

template <typename P>
using is_stop = std::bool_constant<detail::head_is(^^P, ^^Stop)>;

template <typename P>
concept is_stop_v = detail::head_is(^^P, ^^Stop);

// ── The crash label and its metadata ─────────────────────────────────

// How the stopped peer ended, as the detector saw it.  The byte values
// are the values of the crash lane in the session event log, so a log
// that crucible/sessions/_SessionEventLog.h wrote decodes unchanged.  The
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

namespace detail::crash {

namespace tr = ::foundation::algebra::transition;

[[nodiscard]] consteval bool is_crash_payload_type(std::meta::info type) {
    return std::meta::is_type(std::meta::dealias(type)) && tr::shape_of(type) == ^^Crash;
}

}  // namespace detail::crash

template <typename T>
using is_crash_payload = std::bool_constant<detail::crash::is_crash_payload_type(^^T)>;

template <typename T>
concept is_crash_payload_v = detail::crash::is_crash_payload_type(^^T);

// ── The payload rule of the crash label ──────────────────────────────
//
// The crash label is a payload that no endpoint sends (rule 1) and that
// is no label a peer can send (rule 2).  The first refuses the plain dual
// of an endpoint with crash branches, which is correct: the peer of such
// an endpoint is its crash dual (crash_dual_t below).  The second makes
// an Offer of crash branches only an empty choice, and gives rule Sub-&
// its two side conditions in refinement.  The rule is crash_label in
// fixy/session/Protocol.h, because every registration stands under the
// seal of that header.

namespace detail::crash {

// A reception at the head of a branch: a plain input step whose payload
// is no protocol.
[[nodiscard]] consteval bool is_branch_reception(const tr::node& head) {
    return head.is_registered && head.entry.kind == tr::shape_kind::step
        && head.entry.direction == tr::polarity::input && head.entry.is_plain && !head.entry.payload_is_protocol;
}

// A crash branch of an Offer: a reception of the crash label, with no
// wrapper around it.
[[nodiscard]] consteval bool is_crash_branch_type(std::meta::info branch) {
    const tr::node head = tr::decompose(protocol_registry, branch);
    return is_branch_reception(head) && is_crash_payload_type(head.payload);
}

}  // namespace detail::crash

template <typename B>
using is_crash_branch = std::bool_constant<detail::crash::is_crash_branch_type(^^B)>;

template <typename B>
concept is_crash_branch_v = detail::crash::is_crash_branch_type(^^B);

// ── Reliable roles and unavailable queues ────────────────────────────

// Roles assumed never to crash inside the protocol's scope.  A peer in
// the set needs no crash branch, and may not have one.
template <typename... Roles>
struct ReliableSet {
    static constexpr std::size_t size = sizeof...(Roles);
};

using NoReliableRoles = ReliableSet<>;

namespace detail::crash {

[[nodiscard]] consteval bool is_reliable_set_type(std::meta::info type) {
    const std::meta::info dealiased = std::meta::dealias(type);
    return std::meta::is_type(dealiased) && std::meta::has_template_arguments(dealiased)
        && std::meta::template_of(dealiased) == ^^ReliableSet;
}

// True when `role` is a role of the reliable set.  A type that is no
// ReliableSet holds no role.  Complexity: linear in the roles of the set.
[[nodiscard]] consteval bool holds_role(std::meta::info reliable, std::meta::info role) {
    if (!is_reliable_set_type(reliable)) return false;
    for (const std::meta::info member : std::meta::template_arguments_of(std::meta::dealias(reliable))) {
        if (std::meta::dealias(member) == std::meta::dealias(role)) return true;
    }
    return false;
}

}  // namespace detail::crash

template <typename T>
using is_reliable_set = std::bool_constant<detail::crash::is_reliable_set_type(^^T)>;

template <typename Reliable, typename Role>
concept reliable_set_contains_v = detail::crash::holds_role(^^Reliable, ^^Role);

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

// ── The crash walk ───────────────────────────────────────────────────
//
// One fold over the protocol registry answers each crash question of a
// protocol.  The four answers of a node come from the answers of its
// children, so the walk visits each node once.
//
//   is_structured       Rules 1, 2, 4, 6 and 7.  They need no peer.
//   is_covered          Rules 3 and 5, against the channel peer and the
//                       reliable roles.
//   is_watched          Each role that the protocol names is the channel
//                       peer or reliable.  One cell watches one role, so a
//                       crash of another unreliable role reaches no
//                       detector, and its crash branch never runs.  The
//                       roles are the sender of each Offer and the role
//                       that the payload rule of a keyed step names: the
//                       sender of a keyed reception and the receiver of a
//                       keyed send.  Rule 3 makes each crash branch name
//                       the sender of its Offer.
//   is_delegation_free  No payload conveys a session endpoint.
//
// The walk refuses a combinator that the registry does not know, a
// combinator that is not plain, and a step whose payload is a protocol.
// Each wrapper passes to what it wraps, so a VendorPinned protocol gets
// the answers of its inner protocol.  On a binary channel the sender of a
// Recv or of an Offer with no note is the channel peer.
//
// ── Delegation ───────────────────────────────────────────────────────
//
// The crash-stop theory that these rules follow has no delegation (LMCS
// 2025, footnote 2 on p. 11).  A delegated endpoint has peers of its
// own, and no detector of this session watches them, so the crash of
// such a peer leaves the holder waiting for ever.  A crash-aware session
// refuses each payload that payload_conveys_delegation_v of
// fixy/session/Payload.h accepts: the hand-off marker, a session handle
// or a pointer to one, and a carrier with content that the query cannot
// read.  Barwell, Scalas, Yoshida and Zhou (CONCUR 2022) type delegation
// with crashes, and this tree does not carry that system.

namespace detail::crash {

struct verdict {
    bool is_structured = false;
    bool is_covered = false;
    bool is_watched = false;
    bool is_delegation_free = false;
};

inline constexpr verdict refused{};
inline constexpr verdict admitted{.is_structured = true, .is_covered = true, .is_watched = true,
                                  .is_delegation_free = true};

[[nodiscard]] consteval verdict both(const verdict& lhs, const verdict& rhs) {
    return {lhs.is_structured && rhs.is_structured, lhs.is_covered && rhs.is_covered,
            lhs.is_watched && rhs.is_watched, lhs.is_delegation_free && rhs.is_delegation_free};
}

// The channel of the walk: the peer that the cell watches and the set of
// reliable roles.
struct channel {
    std::meta::info peer{};
    std::meta::info reliable{};
};

// The role of a note Sender<Role>, or of a crash payload Crash<Peer>.
[[nodiscard]] consteval std::meta::info first_argument(std::meta::info type) {
    return std::meta::dealias(std::meta::template_arguments_of(std::meta::dealias(type))[0]);
}

// The role that the payload rule of a keyed step names, in either
// direction, or null.
[[nodiscard]] consteval std::meta::info named_role(const tr::node& step) {
    const tr::payload_lookup rule = tr::lookup_payload_rule(protocol_registry, step.payload);
    if (!rule.is_found || rule.entry.input_note == std::meta::info{}) return {};
    if (!std::meta::can_substitute(rule.entry.input_note, {step.payload})) return {};
    return first_argument(std::meta::substitute(rule.entry.input_note, {step.payload}));
}

[[nodiscard]] consteval bool is_watched_role(const channel& on, std::meta::info role) {
    return role == std::meta::info{} || std::meta::dealias(role) == std::meta::dealias(on.peer)
        || holds_role(on.reliable, role);
}

// A payload that the delegation query cannot read stops the build: the
// instantiation of payload_conveys_delegation_v names the component that
// it cannot read.  The walk then answers false, so the gate that reads it
// adds no second error to that one.
[[nodiscard]] consteval bool conveys_delegation(std::meta::info payload) {
    const ::fixy::session::detail::PayloadFacts facts = ::fixy::session::detail::payload_facts_of(payload);
    if (!facts.is_carrier_readable) {
        static_cast<void>(std::meta::extract<bool>(std::meta::substitute(^^payload_conveys_delegation_v, {payload})));
        return false;
    }
    return facts.carrier != DelegationCarrier::None;
}

// The answers of a plain step whose continuation has the answers `after`.
// A reception at the head of a message branch travels with the label of
// its Offer, so the Offer covers it and rule 3 continues after it.
[[nodiscard]] consteval verdict step_verdict(const tr::node& view, const verdict& after, const channel& on,
                                             bool travels_with_a_label) {
    const std::meta::info role = named_role(view);
    const std::meta::info sender = role == std::meta::info{} ? on.peer : role;
    const bool is_bare_reception = view.entry.direction == tr::polarity::input && !travels_with_a_label;
    verdict result{};
    result.is_structured = !is_crash_payload_type(view.payload) && after.is_structured;
    result.is_covered = (!is_bare_reception || holds_role(on.reliable, sender)) && after.is_covered;
    result.is_watched = is_watched_role(on, role) && after.is_watched;
    result.is_delegation_free = !conveys_delegation(view.payload) && after.is_delegation_free;
    return result;
}

struct crash_algebra {
    using result = verdict;
    using context = channel;

    template <class Child>
    consteval verdict terminal(const tr::node& view, context, const Child&) const {
        return view.entry.is_plain ? admitted : refused;
    }
    template <class Child>
    consteval verdict back(const tr::node& view, context, const Child&) const {
        return view.entry.is_plain ? admitted : refused;
    }
    // Rule 1 for a send and rule 4 for a bare reception: no step carries
    // the crash label.
    template <class Child>
    consteval verdict step(const tr::node& view, context on, const Child& child) const {
        if (!view.entry.is_plain || view.entry.payload_is_protocol) return refused;
        return step_verdict(view, child(view.next, on), on, false);
    }
    // A Sender note on a Select names the role that picks, which is the
    // endpoint itself, so the walk reads a noted Select as the Select of
    // its branches.
    template <class Child>
    consteval verdict choice(const tr::node& view, context on, const Child& child) const {
        if (!view.entry.is_plain) return refused;
        if (view.entry.direction != tr::polarity::input) {
            verdict answer = admitted;
            for (const std::meta::info branch : view.branches) answer = both(answer, child(branch, on));
            return answer;
        }
        return offer(view, on, child);
    }
    template <class Child>
    consteval verdict binder(const tr::node& view, context on, const Child& child) const {
        return view.entry.is_plain ? child(view.next, on) : refused;
    }
    template <class Child>
    consteval verdict wrapper(const tr::node& view, context on, const Child& child) const {
        return view.entry.is_plain ? child(view.next, on) : refused;
    }
    template <class Child>
    consteval verdict marker(const tr::node&, context, const Child&) const {
        return refused;
    }
    consteval verdict unregistered(const tr::node&, context) const { return refused; }

private:
    // An Offer.  Rule 2: it has a message branch.  Rule 6: its crash
    // branches trail.  Rule 7: one crash branch for each peer.  Rule 3: an
    // unreliable sender has a crash branch, and each crash branch names
    // the sender.  Rule 5: no crash branch names a reliable role.
    // Complexity: quadratic in the crash branches, linear in the others.
    template <class Child>
    static consteval verdict offer(const tr::node& view, const channel& on, const Child& child) {
        const std::meta::info sender =
            std::meta::dealias(view.annotation == std::meta::info{} ? on.peer : first_argument(view.annotation));
        verdict answer = admitted;
        std::vector<std::meta::info> crashed;
        bool has_message = false;
        bool trails = true;
        bool is_distinct = true;
        bool names_sender = true;
        bool spares_reliable = true;
        for (const std::meta::info branch : view.branches) {
            const tr::node head = tr::decompose(protocol_registry, branch);
            if (is_branch_reception(head) && is_crash_payload_type(head.payload)) {
                const std::meta::info peer = first_argument(head.payload);
                for (const std::meta::info seen : crashed) {
                    if (seen == peer) is_distinct = false;
                }
                crashed.push_back(peer);
                names_sender = names_sender && peer == sender;
                spares_reliable = spares_reliable && !holds_role(on.reliable, peer);
                answer = both(answer, child(head.next, on));
                continue;
            }
            has_message = true;
            trails = trails && crashed.empty();
            answer = both(answer, is_branch_reception(head) ? step_verdict(head, child(head.next, on), on, true)
                                                            : child(branch, on));
        }
        bool has_crash_for_sender = false;
        for (const std::meta::info peer : crashed) {
            if (peer == sender) has_crash_for_sender = true;
        }
        answer.is_structured = answer.is_structured && has_message && trails && is_distinct;
        answer.is_covered = answer.is_covered && (holds_role(on.reliable, sender) || has_crash_for_sender)
                         && names_sender && spares_reliable;
        answer.is_watched = answer.is_watched && is_watched_role(on, sender);
        return answer;
    }
};

// The answers for Proto on a channel to Peer with the reliable roles.
// Complexity: linear in the size of the protocol.
[[nodiscard]] consteval verdict verdict_of(std::meta::info proto, std::meta::info peer, std::meta::info reliable) {
    return tr::fold(protocol_registry, proto, crash_algebra{}, channel{peer, reliable});
}

// The structure needs no channel, so the walk takes the anonymous peer
// and no reliable role.
[[nodiscard]] consteval bool is_structured(std::meta::info proto) {
    return verdict_of(proto, ^^AnonymousPeer, ^^ReliableSet<>).is_structured;
}

// Each clause of the mint of a crash session reads one answer of the
// walk, so a refusal names the answer that failed.
template <typename Proto>
concept is_delegation_free_v = verdict_of(^^Proto, ^^AnonymousPeer, ^^ReliableSet<>).is_delegation_free;

template <typename Proto, typename Peer, typename Reliable>
concept is_every_sender_watched_v = verdict_of(^^Proto, ^^Peer, ^^Reliable).is_watched;

}  // namespace detail::crash

// True when P is well-formed and obeys rules 1, 2, 4, 6 and 7.  Rules 3
// and 5 need the reliable roles, which every_reception_handles_crash_v
// reads.
template <typename P>
concept is_crash_well_formed_v = is_well_formed_v<P> && detail::crash::is_structured(^^P);

template <typename P>
using is_crash_well_formed = std::bool_constant<is_crash_well_formed_v<P>>;

// ── Crash coverage: rules 3 and 5 ────────────────────────────────────

// True when Proto, on a channel to Peer and with the reliable roles,
// handles each crash it can see and no crash it cannot.
template <typename Proto, typename Peer, typename Reliable>
concept every_reception_handles_crash_v =
    detail::crash::is_reliable_set_type(^^Reliable) && detail::crash::verdict_of(^^Proto, ^^Peer, ^^Reliable).is_covered;

// The same question as one type, so that the predicate below takes one
// argument and can hold an armed cell.
template <typename Proto, typename Peer, typename Reliable>
struct CrashCoverage {};

namespace detail::crash {

// True when the question is a CrashCoverage over a reliable set, and the
// walk covers it.  Any other type answers false.
[[nodiscard]] consteval bool coverage_holds(std::meta::info question) {
    const std::meta::info asked = std::meta::dealias(question);
    if (!std::meta::is_type(asked) || !std::meta::has_template_arguments(asked)
        || std::meta::template_of(asked) != ^^CrashCoverage) {
        return false;
    }
    const std::vector<std::meta::info> parts = std::meta::template_arguments_of(asked);
    return is_reliable_set_type(parts[2]) && verdict_of(parts[0], parts[1], parts[2]).is_covered;
}

}  // namespace detail::crash

template <typename Q>
using is_crash_covered = std::bool_constant<detail::crash::coverage_holds(^^Q)>;

// ── Crash branch index ───────────────────────────────────────────────

namespace detail::crash {

// The position of the crash branch for Peer among the branches of the
// Offer under its wrappers, or the branch count when there is none.  A
// Sender note is not a branch, so this is the index that the handle
// reads.  Complexity: linear in the branches.
[[nodiscard]] consteval std::size_t crash_branch_position(std::meta::info offer, std::meta::info peer) {
    const tr::node view = tr::decompose(protocol_registry, tr::strip_wrappers(protocol_registry, offer));
    for (std::size_t index = 0; index < view.branches.size(); ++index) {
        const std::meta::info branch = view.branches[index];
        if (is_crash_branch_type(branch)
            && first_argument(tr::decompose(protocol_registry, branch).payload) == std::meta::dealias(peer)) {
            return index;
        }
    }
    return view.branches.size();
}

[[nodiscard]] consteval bool has_crash_branch(std::meta::info offer, std::meta::info peer) {
    const tr::node view = tr::decompose(protocol_registry, tr::strip_wrappers(protocol_registry, offer));
    return view.is_registered && view.entry.kind == tr::shape_kind::choice
        && view.entry.direction == tr::polarity::input && crash_branch_position(offer, peer) < view.branches.size();
}

}  // namespace detail::crash

// True when the Offer has a crash branch for Peer.
template <typename OfferType, typename Peer>
concept offer_has_crash_branch_v = detail::crash::has_crash_branch(^^OfferType, ^^Peer);

// The position, among the real branches, of the crash branch for Peer.
// The decorators read detail::crash::crash_branch_position, so a
// specialization of this spelling changes only what its author reads.
template <typename OfferType, typename Peer>
inline constexpr std::size_t crash_branch_index_v = [] {
    static_assert(offer_has_crash_branch_v<OfferType, Peer>,
                  "fixy::session::diagnostic [Crash_Branch_Missing]: crash_branch_index_v<Offer, Peer>: the "
                  "Offer has no Recv<Crash<Peer>, K> branch.  Add one for the peer whose crash this "
                  "reception must survive, or declare the peer reliable.");
    return detail::crash::crash_branch_position(^^OfferType, ^^Peer);
}();

// ── Erasure ──────────────────────────────────────────────────────────
//
// One walk erases a protocol for a plain handle.  The crash erasure drops
// the crash branches of each Offer, because the peer cannot select the
// crash label.  The checkpoint erasure of fixy/session/Checkpoint.h reads
// Commit<K> as K, and Roll and Abort as End.  Every other node keeps its
// shape, and the Sender note of a choice stays.  A node that the registry
// does not know erases to the null reflection, so the splice of the
// result stops the build.

namespace detail {

enum class erasure : std::uint8_t { crash_branches, checkpoints };

struct erase_algebra {
    using result = std::meta::info;
    using context = int;
    erasure removes = erasure::crash_branches;

    template <class Child>
    consteval std::meta::info terminal(const crash::tr::node& view, context, const Child&) const {
        if (removes == erasure::checkpoints && (view.entry.shape == ^^Roll || view.entry.shape == ^^Abort)) {
            return ^^End;
        }
        return view.type;
    }
    template <class Child>
    consteval std::meta::info back(const crash::tr::node& view, context, const Child&) const {
        return view.type;
    }
    template <class Child>
    consteval std::meta::info step(const crash::tr::node& view, context ctx, const Child& child) const {
        return std::meta::substitute(view.entry.shape, {view.payload, child(view.next, ctx)});
    }
    template <class Child>
    consteval std::meta::info choice(const crash::tr::node& view, context ctx, const Child& child) const {
        std::vector<std::meta::info> kept;
        for (const std::meta::info branch : view.branches) {
            const bool drops = removes == erasure::crash_branches
                            && view.entry.direction == crash::tr::polarity::input && crash::is_crash_branch_type(branch);
            if (!drops) kept.push_back(child(branch, ctx));
        }
        return std::meta::substitute(
            view.entry.shape, crash::tr::detail::choice_arguments(protocol_registry, view, view.entry.shape, kept));
    }
    template <class Child>
    consteval std::meta::info binder(const crash::tr::node& view, context ctx, const Child& child) const {
        return std::meta::substitute(view.entry.shape, {child(view.next, ctx)});
    }
    template <class Child>
    consteval std::meta::info wrapper(const crash::tr::node& view, context ctx, const Child& child) const {
        return std::meta::substitute(view.entry.shape, {view.value, child(view.next, ctx)});
    }
    template <class Child>
    consteval std::meta::info marker(const crash::tr::node& view, context ctx, const Child& child) const {
        if (removes == erasure::checkpoints) return child(view.next, ctx);
        return std::meta::substitute(view.entry.shape, {child(view.next, ctx)});
    }
    consteval std::meta::info unregistered(const crash::tr::node&, context) const { return {}; }
};

// Complexity: linear in the size of the protocol.
[[nodiscard]] consteval std::meta::info erased(std::meta::info proto, erasure removes) {
    return crash::tr::fold(protocol_registry, proto, erase_algebra{removes}, 0);
}

}  // namespace detail

// ── Duality modulo crash branches ────────────────────────────────────
//
// The peer of an endpoint with crash branches cannot select the crash
// label, so its protocol is the dual of this endpoint with the crash
// branches removed.  Two endpoints are crash duals when their protocols
// agree after the crash branches of each side are removed.  Each side
// keeps its own recovery.  Rule 6 keeps the message branches at the same
// indices on both sides.

template <typename P>
using erase_crash_t = typename[:detail::erased(^^P, detail::erasure::crash_branches):];

template <typename P>
using crash_dual_t = dual_of_t<erase_crash_t<P>>;

template <typename P1, typename P2>
concept is_crash_dual_v = is_dual_v<erase_crash_t<P1>, erase_crash_t<P2>>;

// ── Subtyping side conditions (LMCS 2025 Def. 4.4) ───────────────────
//
// The rules Sub-stop and Sub-& hold in the refinement of
// fixy/session/Subtype.h through the registry.  Stop is registered to
// refine only Stop.  The crash label is no label and is not sendable, so
// a subtype Offer adds no crash branch that its supertype lacks, and a
// pure crash Offer is an empty choice that refines nothing.

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

// The traits above are aliases, and the roster walk of
// foundation/contracts/Armed.h finds class templates only.  These
// assertions read their cells.
static_assert(::foundation::contracts::armed_cell_holds_v<::fixy::session::is_stop>);
static_assert(::foundation::contracts::armed_cell_holds_v<::fixy::session::is_crash_payload>);
static_assert(::foundation::contracts::armed_cell_holds_v<::fixy::session::is_crash_branch>);
static_assert(::foundation::contracts::armed_cell_holds_v<::fixy::session::is_reliable_set>);
static_assert(::foundation::contracts::armed_cell_holds_v<::fixy::session::is_crash_well_formed>);
static_assert(::foundation::contracts::armed_cell_holds_v<::fixy::session::is_crash_covered>);

// ── The walk, checked ────────────────────────────────────────────────
//
// Each answer of the walk, against a protocol that obeys and one that
// breaks the rule.  The keyed payloads are defined in
// fixy/session/Projection.h, so test/fixy/test_session_crash_stop.cpp
// checks the role of a keyed step.

namespace fixy::session::detail::crash::walk_self_test {
using armed_witness::Alice;
using armed_witness::Bob;
using armed_witness::Guarded;
using armed_witness::Msg;
template <typename Proto, typename Peer, typename Reliable>
inline constexpr verdict answers = verdict_of(^^Proto, ^^Peer, ^^Reliable);

// The watch reads the sender of an Offer.
static_assert(answers<Guarded, Alice, ReliableSet<>>.is_watched);
static_assert(!answers<Offer<Sender<Bob>, Recv<Msg, End>, Recv<Crash<Bob>, End>>, Alice, ReliableSet<>>.is_watched);
static_assert(answers<Offer<Sender<Bob>, Recv<Msg, End>>, Alice, ReliableSet<Bob>>.is_watched);

// A vendor pin passes each answer to the protocol it pins.
using Pinned = VendorPinned<VendorBackend::NV, Guarded>;
static_assert(answers<Pinned, Alice, ReliableSet<>>.is_structured && answers<Pinned, Alice, ReliableSet<>>.is_covered
              && answers<Pinned, Alice, ReliableSet<>>.is_watched && answers<Pinned, Alice, ReliableSet<>>.is_delegation_free);

// A step whose payload is a protocol, and a combinator that is not plain,
// fail every answer.
static_assert(!answers<Delegate<End, End>, Alice, ReliableSet<>>.is_delegation_free);
static_assert(!answers<Delegate<End, End>, Alice, ReliableSet<>>.is_structured);
static_assert(!answers<Commit<End>, Alice, ReliableSet<>>.is_structured);
static_assert(!answers<int, Alice, ReliableSet<>>.is_structured);
}  // namespace fixy::session::detail::crash::walk_self_test
