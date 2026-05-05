#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::safety::proto — L8 crash-stop extensions (SEPLOG-I5, #347)
//
// Adds first-class CRASH SEMANTICS to the session-type framework:
//
//   * Stop_g<CrashClass C>   — terminal combinator representing a
//                              crashed endpoint with a type-visible
//                              CrashLattice grade.  Stop is the
//                              backward-compatible alias
//                              Stop_g<CrashClass::Abort>.  Each
//                              Stop_g<C> remains terminal and
//                              self-dual; Stop_g<C1> <= Stop_g<C2>
//                              follows CrashLattice::leq(C1, C2).
//
//   * Crash<PeerTag>         — payload marker used in Offer<> branches
//                              to signal "peer PeerTag has crashed".
//                              A crash-handling branch of an Offer is
//                              any Recv<Crash<PeerTag>, RecoveryBody>.
//
//   * UnavailableQueue<PeerTag> — queue-level marker for BHYZ23's ⊘
//                              (queue to a crashed recipient; subsequent
//                              sends are dropped).  Shipped as a type
//                              here; runtime semantics arrive with
//                              L3 SessionQueue.h (#344).
//
//   * ReliableSet<Roles...>  — phantom type-list of roles assumed not
//                              to crash within the protocol's scope.
//                              Matches BSYZ22's reliability set R.
//
//   * is_reliable_v<R, RoleTag>               membership test
//   * has_crash_branch_for_peer_v<Offer, P>   does the Offer have a
//                                              Recv<Crash<P>, _> branch?
//   * is_terminal_state_v<P>                  End | Stop
//
// Framework integration:
//
//   * is_stop_v<P>                            shape trait
//   * dual_of<Stop_g<C>>      = Stop_g<C>     self-dual
//   * compose<Stop_g<C>, Q>   = Stop_g<C>     bottom preserved
//     compose × Delegate<Stop, K> rules ship in SessionDelegate.h
//     via the GAPS-048 specialisations: delegate-of-Stop collapses
//     to Stop rather than entering K.
//   * is_well_formed<Stop_g<C>, L> = true     any LoopCtx
//   * is_subtype_sync<Stop_g<C>, U> = true    bottom for non-Stop U
//   * is_subtype_sync<Stop_g<C1>, Stop_g<C2>> = CrashLattice order
//   * SessionHandle<Stop_g<C>, Res, LoopCtx>  terminal handle with
//                                              close() method
//
// ─── BSYZ22 / BHYZ23 semantics ─────────────────────────────────────
//
// BSYZ22 (sync crash-stop) and BHYZ23 (async crash-stop) formalise the
// failure model where participants may abruptly stop but surviving
// peers detect and handle the crash via explicit crash branches.
// Links between live participants remain reliable; Byzantine
// behaviour is out of scope.
//
// Key semantic rules:
//
// (1) A participant p crashing transitions its local protocol to Stop:
//     [GR-✂]   ... , (s[p] : T) →_crash ... , (s[p] : Stop)
//
// (2) Any queue whose recipient is p becomes ⊘ (UnavailableQueue<p>);
//     subsequent sends are silently dropped:
//     [GR-⊘]   σ[q, p] →_crash ⊘
//     [GR-✂m]  σ · m(v) →_crash σ    (when recipient is crashed)
//
// (3) Crash detection drives peers that receive from p via an Offer<>
//     into a Crash<p> branch:
//     [GR-offer] q at Offer<... Recv<Crash<p>, Tcrash> ...>
//                           →_crash q at Tcrash
//
// Stop being the SUBTYPE BOTTOM (Stop ⩽ T for any T) captures the
// "crashed endpoint vacuously inhabits any protocol" intuition:
// nothing will ever receive from Stop, so Stop is compatible with
// every context that expects some T.
//
// ─── What this ships; what's deferred ─────────────────────────────
//
// Shipped:
//   * Stop as first-class combinator (dual/compose/WF/subtype/handle)
//   * Crash<Peer> payload marker + trait for detecting crash branches
//   * ReliableSet<Roles...> + membership trait
//   * UnavailableQueue<Peer> type (no runtime machinery yet)
//   * is_terminal_state_v<P>
//   * Full self-tests for every operation
//
// Deferred (needs later layers):
//   * has_mandatory_crash_branches_v<Γ, R>  — Γ-level enforcement
//     that every Offer in every entry's local type that receives
//     from an unreliable peer has a Crash<Peer> branch.  Requires
//     sender-role annotations that only arrive with L4 SessionGlobal.h
//     (#339).  Per-Offer trait has_crash_branch_for_peer_v is
//     shipped; the Γ-level aggregate will come once L4 ships.
//   * Runtime Stop transition (handle mutates on peer-crash detect)
//     — requires runtime integration with CNTP Layer 1 completion
//     errors + SWIM confirmed-dead signals; out of scope for L8.
//   * UnavailableQueue runtime semantics — come with L3
//     SessionQueue.h (#344).
//
// ─── References ───────────────────────────────────────────────────
//
//   Barwell-Scalas-Yoshida-Zhou 2022, "Generalised Multiparty Session
//     Types with Crash-Stop Failures" (CONCUR 2022) — synchronous
//     formulation; Stop type, Crash labels, reliability set R, the
//     mpstk-crash-stop verifier.
//   Barwell-Hou-Yoshida-Zhou 2023, "Designing Asynchronous Multiparty
//     Protocols with Crash-Stop Failures" (LMCS journal version) —
//     async formulation with unavailable queue ⊘ and en-route-with-
//     crash.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/algebra/lattices/CrashLattice.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionSubtype.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto {

using ::crucible::algebra::lattices::CrashClass;
using ::crucible::algebra::lattices::CrashLattice;

// ═════════════════════════════════════════════════════════════════════
// ── Stop_g: terminal crashed-endpoint combinator ───────────────────
// ═════════════════════════════════════════════════════════════════════
//
// A participant whose local type is Stop has crashed — no further
// protocol obligations.  Peers detect the crash via the associated
// queue's transition to UnavailableQueue or via an Offer's Crash<>
// branch firing.

template <CrashClass C>
struct Stop_g {
    static constexpr CrashClass crash_class = C;
};

using Stop = Stop_g<CrashClass::Abort>;

// ─── Shape trait ──────────────────────────────────────────────────

template <typename P>
struct is_stop : std::false_type {};

template <CrashClass C>
struct is_stop<Stop_g<C>> : std::true_type {};

template <typename P>
inline constexpr bool is_stop_v = is_stop<P>::value;

// ─── Terminal-state utility ──────────────────────────────────────
//
// Two combinators represent the END of a session: End (normal) and
// Stop (crashed).  Session.h declares is_terminal_state<P> as a
// trait with the End base case; we specialise it here so Stop is
// ALSO classified as terminal.  SessionHandleBase<Proto>'s
// destructor check uses this trait to decide whether a handle
// destruction at state Proto is safe (no abort) or is abandonment
// (fire in debug).

template <CrashClass C>
struct is_terminal_state<Stop_g<C>> : std::true_type {};

// ═════════════════════════════════════════════════════════════════════
// ── dual_of<Stop> = Stop (self-dual) ───────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Both ends of a channel see Stop when the channel has crashed; the
// dual of a crashed participant is itself crashed.

template <CrashClass C>
struct dual_of<Stop_g<C>> { using type = Stop_g<C>; };

// ═════════════════════════════════════════════════════════════════════
// ── compose<Stop, Q> = Stop (bottom preserved) ─────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Sequencing Q onto a Stop session does NOT splice Q in — a crashed
// endpoint does not continue executing.  Distinct from compose<End, Q>
// = Q (normal termination DOES advance into Q).

template <CrashClass C, typename Q>
struct compose<Stop_g<C>, Q> { using type = Stop_g<C>; };

// ═════════════════════════════════════════════════════════════════════
// ── is_well_formed<Stop, LoopCtx> = true ───────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <CrashClass C, typename LoopCtx>
struct is_well_formed<Stop_g<C>, LoopCtx> : std::true_type {};

// ═════════════════════════════════════════════════════════════════════
// ── is_subtype_sync<Stop, U> = true (Stop is the bottom) ───────────
// ═════════════════════════════════════════════════════════════════════
//
// BSYZ22 Prop 3.4: Stop ⩽ T for any T.  A crashed endpoint vacuously
// inhabits any protocol — nothing will ever receive from Stop, so
// replacing any T with Stop is safe from the peer's perspective.
//
// Rationale on specialisation ordering:
//   * Primary is_subtype_sync<T, U> returns false.
//   * This spec pins T=Stop and matches any U, so it is strictly
//     more specialised than primary on the first argument.  C++'s
//     partial-ordering rule picks it over primary for every query
//     is_subtype_sync<Stop, U>.
//   * Existing specs (Send/Recv/Select/Offer/Loop) all require BOTH
//     arguments to be specific shapes — none matches when T=Stop,
//     so no ambiguity with this spec.
//   * Reverse direction (T ⩽ Stop for T ≠ Stop) is NOT shipped.
//     Primary returns false, which is the correct semantics: Stop
//     is the bottom, not the top.  (Reflexive Stop ⩽ Stop is true
//     via this same spec with U=Stop.)

template <CrashClass C, typename U>
struct is_subtype_sync<Stop_g<C>, U> : std::true_type {};

template <CrashClass C1, CrashClass C2>
struct is_subtype_sync<Stop_g<C1>, Stop_g<C2>>
    : std::bool_constant<CrashLattice::leq(C1, C2)> {};

// ═════════════════════════════════════════════════════════════════════
// ── SessionHandle<Stop, Resource, LoopCtx> — terminal handle ───────
// ═════════════════════════════════════════════════════════════════════
//
// Structurally mirrors SessionHandle<End, ...>: terminal state, linear,
// close() consumes the handle and returns the resource.  Different
// semantic meaning (crashed vs normal termination) is conveyed by the
// protocol state in the type; users distinguish via is_stop_v<P> or
// if constexpr branches.
//
// No post-crash operations are exposed — the handle is terminal.  A
// user who wants to restart the channel establishes a new session
// (mint_channel) rather than advancing this handle.

template <CrashClass C, typename Resource, typename LoopCtx>
class [[nodiscard]] SessionHandle<Stop_g<C>, Resource, LoopCtx>
    : public SessionHandleBase<Stop_g<C>,
                               SessionHandle<Stop_g<C>, Resource, LoopCtx>>
{
    Resource resource_;

    template <typename P, typename R, typename L>
    friend class SessionHandle;

    template <typename P, typename R>
    friend constexpr auto mint_session_handle(R r) noexcept;

    template <typename R, typename Res, typename L>
    friend constexpr auto detail::step_to_next(Res) noexcept;

public:
    using protocol      = Stop_g<C>;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;
    static constexpr CrashClass crash_class = C;

    constexpr explicit SessionHandle(
        Resource r,
        std::source_location loc = std::source_location::current())
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Stop_g<C>,
                            SessionHandle<Stop_g<C>, Resource, LoopCtx>>{loc}
        , resource_{std::move(r)} {}

    constexpr SessionHandle(SessionHandle&&) noexcept            = default;
    constexpr SessionHandle& operator=(SessionHandle&&) noexcept = default;
    ~SessionHandle()                                             = default;

    // Consume the handle and yield the Resource.  Same shape as End's
    // close(); the semantic difference (crash vs normal) is carried
    // by the Stop protocol state, not by the method name.  Stop is a
    // terminal state (is_terminal_state<Stop> == true), so the
    // inherited destructor skips the abandoned-protocol check even
    // without a close() call — but close() remains the correct way
    // to release the Resource in callers that need it back.
    [[nodiscard]] constexpr Resource close() &&
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
    {
        this->mark_consumed_();
        return std::move(resource_);
    }

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return resource_; }
};

// ═════════════════════════════════════════════════════════════════════
// ── Crash<PeerTag> — payload marker for crash-handling branches ────
// ═════════════════════════════════════════════════════════════════════
//
// A crash branch of an Offer is syntactically any Recv<Crash<Peer>, K>:
// when the peer PeerTag crashes, the Offer's dispatch logic fires the
// branch with the Crash<Peer> payload and the surviving participant
// executes K to recover.
//
// Crash<PeerTag> is a PAYLOAD type, not a combinator — it appears
// inside Recv's message slot.  The trait has_crash_branch_for_peer_v
// below searches an Offer's branches for this shape.

template <typename PeerTag>
struct Crash {
    using peer = PeerTag;
};

template <typename T>
struct is_crash : std::false_type {};

template <typename PeerTag>
struct is_crash<Crash<PeerTag>> : std::true_type {};

template <typename T>
inline constexpr bool is_crash_v = is_crash<T>::value;

// ═════════════════════════════════════════════════════════════════════
// ── UnavailableQueue<PeerTag> — queue-level crash marker ───────────
// ═════════════════════════════════════════════════════════════════════
//
// BHYZ23's ⊘: the queue into a crashed recipient transitions to
// UnavailableQueue; subsequent sends are silently dropped.
//
// Shipped as a type; runtime machinery (queue-state transitions under
// peer-death) arrives with L3 SessionQueue.h (#344).  The type exists
// now so downstream headers can reference it without a forward-decl.

template <typename PeerTag>
struct UnavailableQueue {
    using peer = PeerTag;
};

// ═════════════════════════════════════════════════════════════════════
// ── ReliableSet<Roles...> — phantom type-list of reliable roles ────
// ═════════════════════════════════════════════════════════════════════
//
// Roles in the set are ASSUMED not to crash within the protocol's
// scope.  Matches BSYZ22's reliability set R ⊆ Roles.  Typical
// choices in Crucible:
//
//   R = {Canopy-leader}        for Raft-dependent protocols
//   R = ∅                      for purely peer-to-peer collectives
//   R = {all participants}     for within-Keeper channels
//
// Participants in R need not appear in Crash<> branches of peers.
// Participants NOT in R must appear in Crash<> branches of every peer
// that receives from them (the Γ-level aggregate check; deferred to
// L4 — see file header).

template <typename... Roles>
struct ReliableSet {
    static constexpr std::size_t size = sizeof...(Roles);
};

// Convenience: ReliableSet<> is "no roles assumed reliable".
using UnreliableAll = ReliableSet<>;

// Membership: is a specific role in the reliable set?
template <typename R, typename RoleTag>
struct is_reliable;

template <typename... Roles, typename RoleTag>
struct is_reliable<ReliableSet<Roles...>, RoleTag>
    : std::bool_constant<(std::is_same_v<Roles, RoleTag> || ...)> {};

template <typename R, typename RoleTag>
inline constexpr bool is_reliable_v = is_reliable<R, RoleTag>::value;

// ═════════════════════════════════════════════════════════════════════
// ── has_crash_branch_for_peer_v<OfferType, PeerTag> ────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Does the Offer<Branches...> have a branch of the form
// Recv<Crash<PeerTag>, _>?  Fold-expression disjunction across the
// branches, O(|branches|) compile-time instantiations.
//
// Use at the boundary of protocol declarations involving unreliable
// peers:
//
//     template <typename Peer>
//     void serve_with_crash_support(auto offer_handle) {
//         static_assert(has_crash_branch_for_peer_v<
//                         typename decltype(offer_handle)::protocol,
//                         Peer>,
//             "Offer must have a Recv<Crash<Peer>, _> branch to "
//             "safely dispatch on peer crash");
//         ...
//     }
//
// Per-Offer trait.  The Γ-level aggregate — "for every Offer in every
// entry of Γ that receives from any peer P ∉ R, there must be a
// Recv<Crash<P>, _> branch" — is deferred; see file-header note.

namespace detail::crash {

template <typename Branch, typename PeerTag>
struct is_crash_branch_for : std::false_type {};

// A crash branch is any Recv whose payload is Crash<PeerTag> for the
// requested peer.  The continuation K is arbitrary.
template <typename PeerTag, typename K>
struct is_crash_branch_for<Recv<Crash<PeerTag>, K>, PeerTag>
    : std::true_type {};

}  // namespace detail::crash

template <typename OfferType, typename PeerTag>
struct has_crash_branch_for_peer : std::false_type {};

template <typename... Branches, typename PeerTag>
struct has_crash_branch_for_peer<Offer<Branches...>, PeerTag>
    : std::bool_constant<
          (detail::crash::is_crash_branch_for<Branches, PeerTag>::value || ...)
      > {};

// Sender-annotated Offer (#367): the Crash<PeerTag> branch is only
// REQUIRED when the Offer's declared sender equals PeerTag.  An
// Offer<Sender<Alice>, ...> doesn't need a Crash<Bob> branch — Bob
// isn't the one sending the choice, so Bob's crash doesn't affect
// this Offer's reception.  When sender ≠ PeerTag the trait is
// vacuously true.
//
// When sender == PeerTag the trait falls through to the ordinary
// per-branch fold, excluding the Sender<Role> tag from the search.
template <typename Role, typename... Branches, typename PeerTag>
struct has_crash_branch_for_peer<Offer<Sender<Role>, Branches...>, PeerTag>
    : std::bool_constant<
          !std::is_same_v<Role, PeerTag> ||
          (detail::crash::is_crash_branch_for<Branches, PeerTag>::value || ...)
      > {};

template <typename OfferType, typename PeerTag>
inline constexpr bool has_crash_branch_for_peer_v =
    has_crash_branch_for_peer<OfferType, PeerTag>::value;

// ═════════════════════════════════════════════════════════════════════
// ── assert_has_crash_branch_for<Offer, Peer>() ─────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// One-line consteval assertion for call sites demanding a specific
// crash-handling contract.  Fires the diagnostic at the call site
// (not deep in a template instantiation).

template <typename OfferType, typename PeerTag>
consteval void assert_has_crash_branch_for() noexcept {
    static_assert(has_crash_branch_for_peer_v<OfferType, PeerTag>,
        "crucible::session::diagnostic [CrashBranch_Missing]: "
        "assert_has_crash_branch_for: OfferType lacks a "
        "Recv<Crash<PeerTag>, _> branch for the specified unreliable "
        "peer.  A session receiving from an unreliable peer MUST "
        "handle that peer's crash — add a Recv<Crash<Peer>, "
        "RecoveryBody> branch to the Offer<>.  If the peer IS "
        "reliable, add its role to ReliableSet<> and skip the crash "
        "branch.");
}

// ═════════════════════════════════════════════════════════════════════
// ── every_offer_has_crash_branch_for_peer_v<Proto, Peer>   (#368) ──
// ═════════════════════════════════════════════════════════════════════
//
// `has_crash_branch_for_peer_v<Offer, Peer>` checks ONE Offer node.
// The aggregate "every Offer reachable in the protocol tree Proto has
// a Recv<Crash<Peer>, _> branch" is the discipline an unreliable-peer
// session actually needs — a single-Offer check is a local property
// that a one-Offer-past-the-first-one violation can hide.
//
// This walker recurses through the local-protocol combinator family:
//
//   End / Stop / Continue      — no Offer nodes, trivially OK
//   Send<T, K>                 — recurse into K
//   Recv<T, K>                 — recurse into K
//   Select<Bs...>              — recurse into each Bs (our choice;
//                                each branch is a proto, not an
//                                immediate-crash-relevant Offer)
//   Offer<Bs...>               — check THIS Offer has a Crash<Peer>
//                                branch AND recurse into each Bs'
//                                continuation (branches are
//                                Recv<Msg, K>; walk K)
//   Loop<B>                    — recurse into B
//   Delegate<T, K>             — recurse into K only (the delegated
//                                protocol T is executed BY THE
//                                RECIPIENT; that's a separate check
//                                against the recipient's reliability
//                                model, not this walker's job)
//   Accept<T, K>               — same as Delegate: recurse into K
//
// The walker forms the building block for the deferred Γ-level trait
// (`has_mandatory_crash_branches_v<Γ, R>`) — which aggregates this
// per-entry check across a typing context.  See SessionCrash.h file
// header's "Deferred" notes.
//
// Paired with `CrashWatchedHandle<Proto, R, PeerTag>` (#400): the
// handle's runtime peek transitions to CrashEvent on peer-crash, and
// `every_offer_has_crash_branch_for_peer_v<Proto, PeerTag>` statically
// proves the protocol has somewhere to DISPATCH that crash.  Phase 2
// auto-dispatch (noted in CrashTransport.h §"What's deferred")
// becomes sound once this predicate holds.

namespace detail::crash {

template <typename Proto, typename PeerTag>
struct all_offers_have_crash_branch;

// Terminals — no Offer nodes downstream.
template <typename PeerTag>
struct all_offers_have_crash_branch<End, PeerTag> : std::true_type {};

template <CrashClass C, typename PeerTag>
struct all_offers_have_crash_branch<Stop_g<C>, PeerTag> : std::true_type {};

template <typename PeerTag>
struct all_offers_have_crash_branch<Continue, PeerTag> : std::true_type {};

// Send / Recv — recurse into the continuation.
template <typename T, typename K, typename PeerTag>
struct all_offers_have_crash_branch<Send<T, K>, PeerTag>
    : all_offers_have_crash_branch<K, PeerTag> {};

template <typename T, typename K, typename PeerTag>
struct all_offers_have_crash_branch<Recv<T, K>, PeerTag>
    : all_offers_have_crash_branch<K, PeerTag> {};

// Select — recurse into every branch.  The branches are OUR choice,
// no crash-branch obligation on Select itself.
template <typename... Bs, typename PeerTag>
struct all_offers_have_crash_branch<Select<Bs...>, PeerTag>
    : std::bool_constant<
          (all_offers_have_crash_branch<Bs, PeerTag>::value && ...)
      > {};

// Offer — THE load-bearing case.  This Offer must itself have a
// Crash<PeerTag> branch AND every downstream continuation must be
// crash-safe too.  Each branch is typically Recv<Msg, K>; walking
// into K checks crash-safety of the continuation.
template <typename... Bs, typename PeerTag>
struct all_offers_have_crash_branch<Offer<Bs...>, PeerTag>
    : std::bool_constant<
          has_crash_branch_for_peer_v<Offer<Bs...>, PeerTag> &&
          (all_offers_have_crash_branch<Bs, PeerTag>::value && ...)
      > {};

// Sender-annotated Offer (#367): the per-Offer crash-branch check
// is delegated to `has_crash_branch_for_peer_v`, which returns true
// vacuously when the Offer's sender ≠ PeerTag.  The recursion still
// walks every downstream continuation — an Offer<Sender<Alice>, ...>
// might wrap a downstream Offer<Sender<Bob>, ...> that DOES need a
// Crash<Bob> branch.  The tag itself is not walked (it's type-level
// metadata, not a combinator).
template <typename Role, typename... Bs, typename PeerTag>
struct all_offers_have_crash_branch<Offer<Sender<Role>, Bs...>, PeerTag>
    : std::bool_constant<
          has_crash_branch_for_peer_v<Offer<Sender<Role>, Bs...>, PeerTag> &&
          (all_offers_have_crash_branch<Bs, PeerTag>::value && ...)
      > {};

// Loop — recurse into body.  The same body runs every iteration; one
// walk is sufficient (no need to unroll).
template <typename B, typename PeerTag>
struct all_offers_have_crash_branch<Loop<B>, PeerTag>
    : all_offers_have_crash_branch<B, PeerTag> {};

}  // namespace detail::crash

// Public surface: boolean predicate + consteval assertion.
template <typename Proto, typename PeerTag>
inline constexpr bool every_offer_has_crash_branch_for_peer_v =
    detail::crash::all_offers_have_crash_branch<Proto, PeerTag>::value;

template <typename Proto, typename PeerTag>
consteval void assert_every_offer_has_crash_branch_for() noexcept {
    static_assert(every_offer_has_crash_branch_for_peer_v<Proto, PeerTag>,
        "crucible::session::diagnostic [CrashBranch_Missing_In_Tree]: "
        "assert_every_offer_has_crash_branch_for: at least one Offer<> "
        "in the protocol tree lacks a Recv<Crash<PeerTag>, _> branch "
        "for the specified unreliable peer.  Every Offer reachable "
        "from an unreliable-peer session MUST handle that peer's "
        "crash — add a Recv<Crash<Peer>, RecoveryBody> branch to the "
        "offending Offer<>.  If the peer IS reliable along this path, "
        "add its role to ReliableSet<> so the static check knows to "
        "skip crash-branch enforcement.");
}

// ═════════════════════════════════════════════════════════════════════
// ── Framework self-test static_asserts ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Verify every Stop / Crash / ReliableSet operation at compile time.
// Regressions to the metafunctions above fail at the first TU that
// includes this header.

#ifdef CRUCIBLE_SESSION_SELF_TESTS
namespace detail::crash::crash_self_test {

// Fixture tags
struct Alice {};
struct Bob   {};
struct Carol {};

// Fixture payloads
struct Msg  {};
struct Ack  {};

// ─── Stop shape traits ────────────────────────────────────────────

static_assert( is_stop_v<Stop>);
static_assert( is_stop_v<Stop_g<CrashClass::Throw>>);
static_assert(Stop::crash_class == CrashClass::Abort);
static_assert(Stop_g<CrashClass::NoThrow>::crash_class
              == CrashClass::NoThrow);
static_assert(!is_stop_v<End>);
static_assert(!is_stop_v<Send<int, End>>);
static_assert(!is_stop_v<Loop<Send<int, Continue>>>);

static_assert( is_terminal_state_v<End>);
static_assert( is_terminal_state_v<Stop>);
static_assert( is_terminal_state_v<Stop_g<CrashClass::NoThrow>>);
static_assert(!is_terminal_state_v<Send<int, End>>);
static_assert(!is_terminal_state_v<Loop<Send<int, Continue>>>);
static_assert(!is_terminal_state_v<Continue>);

// ─── Stop is a valid head (unchanged from Session.h's definition) ─

static_assert(is_head_v<Stop>);
static_assert(is_head_v<End>);

// ─── dual(Stop) = Stop (self-dual) ────────────────────────────────

static_assert(std::is_same_v<dual_of_t<Stop>, Stop>);
static_assert(std::is_same_v<
    dual_of_t<Stop_g<CrashClass::ErrorReturn>>,
    Stop_g<CrashClass::ErrorReturn>>);
// Involution under dual.
static_assert(std::is_same_v<dual_of_t<dual_of_t<Stop>>, Stop>);

// ─── compose<Stop, Q> = Stop (bottom preserved) ───────────────────

static_assert(std::is_same_v<compose_t<Stop, End>, Stop>);
static_assert(std::is_same_v<compose_t<Stop, Send<int, End>>, Stop>);
static_assert(std::is_same_v<
    compose_t<Stop, Loop<Send<int, Continue>>>,
    Stop>);
static_assert(std::is_same_v<
    compose_t<Stop_g<CrashClass::Throw>, Send<int, End>>,
    Stop_g<CrashClass::Throw>>);

// Compose does NOT reach through Send's continuation when that
// continuation is End — End is replaced, not preserved.  But when the
// continuation is Stop, Stop survives:
static_assert(std::is_same_v<
    compose_t<Send<int, Stop>, Recv<bool, End>>,
    Send<int, Stop>>);

// Same for Offer/Select branches that end in Stop.
static_assert(std::is_same_v<
    compose_t<Offer<Recv<int, Stop>, Recv<bool, End>>, Send<int, End>>,
    Offer<Recv<int, Stop>, Recv<bool, Send<int, End>>>>);

// ─── Well-formedness ──────────────────────────────────────────────

static_assert(is_well_formed_v<Stop>);
static_assert(is_well_formed_v<Stop_g<CrashClass::NoThrow>>);
// Stop inside a Loop is well-formed (terminal branch of a choice).
static_assert(is_well_formed_v<Loop<Select<Send<int, Continue>, Stop>>>);

// ─── is_subtype_sync<Stop, U> = true (Stop is the bottom) ─────────

static_assert(is_subtype_sync_v<Stop, End>);
static_assert(is_subtype_sync_v<Stop, Stop>);
static_assert(is_subtype_sync_v<Stop, Send<int, End>>);
static_assert(is_subtype_sync_v<Stop, Recv<int, End>>);
static_assert(is_subtype_sync_v<Stop, Select<End>>);
static_assert(is_subtype_sync_v<Stop, Offer<End, End>>);
static_assert(is_subtype_sync_v<Stop, Loop<Send<int, Continue>>>);

// Stop_g participates in CrashLattice order when compared to another
// Stop_g.  Bare Stop is the Abort-grade alias.
static_assert(is_subtype_sync_v<
    Stop_g<CrashClass::Abort>, Stop_g<CrashClass::NoThrow>>);
static_assert(is_subtype_sync_v<
    Stop_g<CrashClass::Throw>, Stop_g<CrashClass::ErrorReturn>>);
static_assert(!is_subtype_sync_v<
    Stop_g<CrashClass::NoThrow>, Stop_g<CrashClass::Abort>>);
static_assert(!is_subtype_sync_v<
    Stop_g<CrashClass::ErrorReturn>, Stop_g<CrashClass::Throw>>);

// Reverse direction: T ⩽ Stop is FALSE for T ≠ Stop.
static_assert(!is_subtype_sync_v<End, Stop>);
static_assert(!is_subtype_sync_v<Send<int, End>, Stop>);
static_assert(!is_subtype_sync_v<Loop<Send<int, Continue>>, Stop>);

// Stop is equivalent only to itself.
static_assert( equivalent_sync_v<Stop, Stop>);
static_assert(!equivalent_sync_v<Stop, End>);
static_assert(!equivalent_sync_v<End,  Stop>);

// Strict subtype: Stop < T for any T ≠ Stop.
static_assert( is_strict_subtype_sync_v<Stop, End>);
static_assert( is_strict_subtype_sync_v<Stop, Send<int, End>>);
static_assert(!is_strict_subtype_sync_v<Stop, Stop>);  // not strict vs itself

// ─── Crash<PeerTag> ───────────────────────────────────────────────

static_assert( is_crash_v<Crash<Alice>>);
static_assert(!is_crash_v<Msg>);
static_assert(!is_crash_v<Stop>);
static_assert(!is_crash_v<End>);

// Peer tag extraction.
static_assert(std::is_same_v<typename Crash<Alice>::peer, Alice>);
static_assert(std::is_same_v<typename Crash<Bob>::peer,   Bob>);

// ─── ReliableSet ──────────────────────────────────────────────────

using NoneReliable = ReliableSet<>;
using AliceReliable = ReliableSet<Alice>;
using AliceAndBob   = ReliableSet<Alice, Bob>;
using Everyone      = ReliableSet<Alice, Bob, Carol>;

static_assert(NoneReliable::size   == 0);
static_assert(AliceReliable::size  == 1);
static_assert(AliceAndBob::size    == 2);
static_assert(Everyone::size       == 3);

static_assert(std::is_same_v<UnreliableAll, NoneReliable>);

static_assert(!is_reliable_v<NoneReliable,  Alice>);
static_assert( is_reliable_v<AliceReliable, Alice>);
static_assert(!is_reliable_v<AliceReliable, Bob>);
static_assert( is_reliable_v<AliceAndBob,   Alice>);
static_assert( is_reliable_v<AliceAndBob,   Bob>);
static_assert(!is_reliable_v<AliceAndBob,   Carol>);
static_assert( is_reliable_v<Everyone,      Carol>);

// ─── has_crash_branch_for_peer_v ──────────────────────────────────

// Normal offer with no crash branches.
using NormalOffer = Offer<Recv<Msg, End>, Recv<Ack, End>>;
static_assert(!has_crash_branch_for_peer_v<NormalOffer, Alice>);
static_assert(!has_crash_branch_for_peer_v<NormalOffer, Bob>);

// Offer with a crash branch for Alice.
using AliceCrashOffer = Offer<
    Recv<Msg,          End>,
    Recv<Crash<Alice>, End>>;
static_assert( has_crash_branch_for_peer_v<AliceCrashOffer, Alice>);
static_assert(!has_crash_branch_for_peer_v<AliceCrashOffer, Bob>);

// Offer with crash branches for both Alice and Bob.
using BothCrashOffer = Offer<
    Recv<Msg,          End>,
    Recv<Crash<Alice>, End>,
    Recv<Crash<Bob>,   End>>;
static_assert( has_crash_branch_for_peer_v<BothCrashOffer, Alice>);
static_assert( has_crash_branch_for_peer_v<BothCrashOffer, Bob>);
static_assert(!has_crash_branch_for_peer_v<BothCrashOffer, Carol>);

// Empty Offer has no crash branches.
static_assert(!has_crash_branch_for_peer_v<Offer<>, Alice>);

// Non-Offer types default to false (the primary template).
static_assert(!has_crash_branch_for_peer_v<End, Alice>);
static_assert(!has_crash_branch_for_peer_v<Send<int, End>, Alice>);
static_assert(!has_crash_branch_for_peer_v<Select<Recv<Crash<Alice>, End>>, Alice>);
// ^ Select is a DIFFERENT combinator; crash-branches are meaningful
//   only in Offer (peer-driven choice).  A Select<Recv<Crash<...>, _>>
//   would mean "I (the deciding party) choose to emit a crash signal"
//   which is nonsensical — crashes are detected, not chosen.

// ─── assert helpers compile ───────────────────────────────────────

consteval bool check_assert_crash_branch() {
    assert_has_crash_branch_for<AliceCrashOffer, Alice>();
    assert_has_crash_branch_for<BothCrashOffer,  Bob>();
    return true;
}
static_assert(check_assert_crash_branch());

// ─── Combined protocol: crash-handled request-response ────────────
//
// A realistic protocol where the server may crash during handling.
// Client's local view:
//   Send Req, then Offer:
//       receive Resp → End (success)
//       receive Crash<Server> → End (recover)

using CrashHandledClient = Send<Msg, Offer<
    Recv<Ack,            End>,
    Recv<Crash<Alice>,   End>>>;

using CrashHandledServer = dual_of_t<CrashHandledClient>;

// Dual correctly flips Send→Recv and Offer→Select; Crash<Alice>
// payload is NOT dualised (it's a value type, not a session type).
static_assert(std::is_same_v<
    CrashHandledServer,
    Recv<Msg, Select<
        Send<Ack,            End>,
        Send<Crash<Alice>,   End>>>>);

// Involution.
static_assert(std::is_same_v<
    dual_of_t<dual_of_t<CrashHandledClient>>,
    CrashHandledClient>);

// Well-formed.
static_assert(is_well_formed_v<CrashHandledClient>);
static_assert(is_well_formed_v<CrashHandledServer>);

// Client's Offer has a crash branch for Alice.
using ClientOffer = typename CrashHandledClient::next;
static_assert(has_crash_branch_for_peer_v<ClientOffer, Alice>);
static_assert(!has_crash_branch_for_peer_v<ClientOffer, Bob>);

}  // namespace detail::crash::crash_self_test
#endif  // CRUCIBLE_SESSION_SELF_TESTS

}  // namespace crucible::safety::proto
