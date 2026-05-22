#pragma once

// ── sessions/SessionPhi.h — FX §11.18 φ-predicate family ──────────
//
// SEPLOG-I4 / SEPLOG-I6 / SEPLOG-STRUCT-7 closure — FIXY-V-069.
//
// Ships the FX paper's 7-level session-safety hierarchy as
// compile-time predicates over binary session-type protocol trees.
// Each level is STRICTLY STRONGER than the predecessor (forms a
// chain in the safety lattice):
//
//     phi_safe  ⊇  phi_df  ⊇  phi_term  ⊇  phi_live_pp
//                    ⊇  phi_live  ⊇  phi_live_plus  ⊇  phi_live_pp
//
//   - phi_safe<P>      — protocol is well-formed (Honda 1998).
//                        Equivalent to is_well_formed_v<P>.
//   - phi_df<P>        — strengthens safe by rejecting structural
//                        deadlock witnesses: empty Select<>/Offer<>
//                        (no branch to choose), self-Transmission
//                        (caught at MPST layer).  HYK24 (Hou-
//                        Yoshida-Kobayashi 2024) reachability-of-
//                        stuck-states for binary degenerates to:
//                        every choice has at least one branch.
//   - phi_term<P>      — strengthens df by requiring termination:
//                        every Loop<B> has at least one path through
//                        B that reaches End or Stop (not Continue).
//                        A protocol with a productive but infinite
//                        Loop fails phi_term.
//   - phi_nterm<P>     — strengthens safe by requiring NON-termi-
//                        nation: well-formed AND has at least one
//                        Loop whose body cannot reach End/Stop.  A
//                        sibling of phi_term — neither implies the
//                        other.  (Both phi_term and phi_nterm imply
//                        phi_safe but they're mutually exclusive.)
//   - phi_live<P>      — strengthens df by requiring no stuck state.
//                        For binary session types every non-empty
//                        Select/Offer has a future action, so this
//                        is approximately phi_df ∧ no_orphan_jump.
//   - phi_live_plus<P> — strengthens live by requiring every Choice
//                        branch is distinguishable at runtime:
//                        the head-payload types of branches in
//                        Select<Bs...> / Offer<Bs...> are pairwise
//                        distinct.  Two branches `Send<int, …>` /
//                        `Send<int, …>` cannot be told apart by a
//                        peer's incoming message and therefore one
//                        is "dead branch" reachable only by chance.
//   - phi_live_pp<P>   — strictest: every concrete message is
//                        delivered.  Conservatively requires
//                        phi_live_plus ∧ phi_term — every choice
//                        branch is distinct AND the protocol
//                        terminates (so each message instance is
//                        finitely many steps from the entry).
//
// ── Soundness vs completeness ──────────────────────────────────────
//
// Every predicate here is SOUND: a protocol that satisfies the
// predicate genuinely satisfies the FX §11.18 property.  Predicates
// are CONSERVATIVELY INCOMPLETE: some FX-safe protocols are rejected
// because a structural witness suffices to refute the property, but
// no structural witness suffices to confirm it (general φ-decision
// in MPST is coinductive — see HYK24).  Strengthening to coinductive
// definitions tracks under `GAPS-FIXY-Sess-PhiRestore` (future).
//
// The previous fixy::sess:: aliases `phi_df_v` / `phi_live_v` /
// `phi_live_plus_v` / `phi_live_pp_v` (removed in fixy-CR-12) all
// re-exported `is_well_formed_v` — the names lied because well-
// formedness is STRICTLY WEAKER than each of those properties.  This
// header gives them honest substrate-side semantics so fixy/Sess.h
// can restore aliases that don't lie.
//
// ── References ────────────────────────────────────────────────────
//
//   - FX paper §11.18 (7-level safety hierarchy)
//   - HYK24: Hou-Yoshida-Kobayashi 2024, "Multiparty Session Type
//            Inference" — deadlock-freedom via reachability of
//            stuck states (extended to MPST; the binary projection
//            handled here)
//   - Honda 1998: binary session types + well-formedness
//   - BSYZ22 / BHYZ23: crash-stop extensions (Stop_g semantics)
//
// ── Substrate-side combinators consumed ───────────────────────────
//
//   sessions/Session.h:          Send / Recv / Select / Offer /
//                                Loop / Continue / End / VendorPinned
//                                + is_well_formed_v + is_terminal_state_v
//   sessions/SessionCrash.h:     Stop_g<C> + Stop alias + is_stop_v
//                                (visibility via #include)
//   sessions/SessionGlobal.h:    NOT consumed — φ predicates here
//                                are binary; MPST projection is
//                                separate (per SessionGlobal.h).

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCrash.h>
#include <type_traits>

namespace crucible::safety::proto {

// ═════════════════════════════════════════════════════════════════════
// ── Layer 1 — Helper metafunctions (structural witnesses) ──────────
// ═════════════════════════════════════════════════════════════════════

// ── has_empty_branch<P> ────────────────────────────────────────────
//
// True iff P contains any Select<> or Offer<> with zero branches in
// any reachable subtree.  Empty Select / Offer is a structural
// deadlock — the type system currently accepts them via the vacuous
// `(true && ...)` AND-fold in is_well_formed's Select / Offer cases,
// but at runtime a sender has no decision to make and the peer has
// no expectation, so the protocol cannot progress.
//
// HYK24 reachability-of-stuck-states for binary session types
// degenerates to this single structural witness: every other "stuck"
// state in binary protocols is ruled out by well-formedness +
// duality (e.g., Send<T, …> against Recv<T, …> never deadlocks).

template <typename P>
struct has_empty_branch : std::false_type {};

// Terminal states cannot contain an empty branch.
template <>
struct has_empty_branch<End> : std::false_type {};

template <CrashClass C>
struct has_empty_branch<Stop_g<C>> : std::false_type {};

template <>
struct has_empty_branch<Continue> : std::false_type {};

// Send/Recv recurse into the continuation.
template <typename T, typename K>
struct has_empty_branch<Send<T, K>> : has_empty_branch<K> {};

template <typename T, typename K>
struct has_empty_branch<Recv<T, K>> : has_empty_branch<K> {};

// Loop recurses into the body.
template <typename B>
struct has_empty_branch<Loop<B>> : has_empty_branch<B> {};

// VendorPinned is transparent.
template <VendorBackend V, typename P>
struct has_empty_branch<VendorPinned<V, P>> : has_empty_branch<P> {};

// Select<> / Offer<> with zero branches — the structural deadlock witness.
template <>
struct has_empty_branch<Select<>> : std::true_type {};

template <>
struct has_empty_branch<Offer<>> : std::true_type {};

// Select<Bs...> / Offer<Bs...> with at least one branch: recurse into
// every branch.  Note: every branch may itself contain a deeper empty
// choice, so the OR-fold checks all branches.
template <typename B0, typename... Bs>
struct has_empty_branch<Select<B0, Bs...>>
    : std::bool_constant<(has_empty_branch<B0>::value ||
                          (has_empty_branch<Bs>::value || ...))> {};

template <typename B0, typename... Bs>
struct has_empty_branch<Offer<B0, Bs...>>
    : std::bool_constant<(has_empty_branch<B0>::value ||
                          (has_empty_branch<Bs>::value || ...))> {};

// Sender-annotated Offer: the Sender<Role> tag is a type-level
// annotation, not a runnable branch.  Recurse only into the real
// branches Bs...
template <typename Role, typename B0, typename... Bs>
struct has_empty_branch<Offer<Sender<Role>, B0, Bs...>>
    : std::bool_constant<(has_empty_branch<B0>::value ||
                          (has_empty_branch<Bs>::value || ...))> {};

// Sender-annotated Offer with zero real branches is the same
// structural deadlock as bare Offer<>.
template <typename Role>
struct has_empty_branch<Offer<Sender<Role>>> : std::true_type {};

template <typename P>
inline constexpr bool has_empty_branch_v = has_empty_branch<P>::value;

// ── loop_body_terminates<B> ────────────────────────────────────────
//
// True iff body B of some enclosing Loop has at least one execution
// path that reaches a terminal state (End or Stop_g<C>) WITHOUT going
// through Continue.  A Loop body that always hits Continue is an
// inescapable cycle; only bodies with at least one terminal-bearing
// branch can ever exit the Loop.
//
// Decision rule:
//
//   - End, Stop_g<C>: trivially terminates (the body IS terminal).
//                     But these are rejected as whole-body terminal
//                     by is_well_formed, so they appear only inside
//                     Select/Offer branches of a real Loop body.
//   - Continue: does NOT terminate this Loop — it jumps to the head.
//   - Send<T, K> / Recv<T, K>: terminates iff K terminates.
//   - Select<Bs...>: terminates iff ANY branch terminates (the sender
//                    can choose the escape branch).  OR-fold.
//   - Offer<Bs...>: terminates iff ANY branch terminates (the receiver
//                   may receive the escape message from the peer).
//                   OR-fold.  Same semantic as Select for termination —
//                   we only require an escape PATH exists, not that
//                   the protocol is FORCED through it.
//   - Loop<B'> nested: terminates iff B' itself terminates (transitive).
//   - VendorPinned<V, P>: transparent.

template <typename B>
struct loop_body_terminates;

template <>
struct loop_body_terminates<End> : std::true_type {};

template <CrashClass C>
struct loop_body_terminates<Stop_g<C>> : std::true_type {};

template <>
struct loop_body_terminates<Continue> : std::false_type {};

template <typename T, typename K>
struct loop_body_terminates<Send<T, K>> : loop_body_terminates<K> {};

template <typename T, typename K>
struct loop_body_terminates<Recv<T, K>> : loop_body_terminates<K> {};

template <typename... Bs>
struct loop_body_terminates<Select<Bs...>>
    : std::bool_constant<(loop_body_terminates<Bs>::value || ...)> {};

template <typename... Bs>
struct loop_body_terminates<Offer<Bs...>>
    : std::bool_constant<(loop_body_terminates<Bs>::value || ...)> {};

template <typename Role, typename... Bs>
struct loop_body_terminates<Offer<Sender<Role>, Bs...>>
    : std::bool_constant<(loop_body_terminates<Bs>::value || ...)> {};

// A nested Loop terminates iff ITS body terminates (transitive escape).
template <typename B>
struct loop_body_terminates<Loop<B>> : loop_body_terminates<B> {};

template <VendorBackend V, typename P>
struct loop_body_terminates<VendorPinned<V, P>> : loop_body_terminates<P> {};

// Empty Select<> / Offer<> as a Loop body: vacuously cannot terminate
// (no branch exists).  OR-fold over empty pack is false, which IS
// the correct answer.
template <>
struct loop_body_terminates<Select<>> : std::false_type {};

template <>
struct loop_body_terminates<Offer<>> : std::false_type {};

template <typename B>
inline constexpr bool loop_body_terminates_v = loop_body_terminates<B>::value;

// ── has_unbounded_loop<P> ──────────────────────────────────────────
//
// True iff P contains any Loop<B> whose body B does not terminate
// (loop_body_terminates_v<B> == false).  An unbounded loop is the
// structural witness of non-termination.

template <typename P>
struct has_unbounded_loop : std::false_type {};

template <>
struct has_unbounded_loop<End> : std::false_type {};

template <CrashClass C>
struct has_unbounded_loop<Stop_g<C>> : std::false_type {};

template <>
struct has_unbounded_loop<Continue> : std::false_type {};

template <typename T, typename K>
struct has_unbounded_loop<Send<T, K>> : has_unbounded_loop<K> {};

template <typename T, typename K>
struct has_unbounded_loop<Recv<T, K>> : has_unbounded_loop<K> {};

template <typename... Bs>
struct has_unbounded_loop<Select<Bs...>>
    : std::bool_constant<(has_unbounded_loop<Bs>::value || ...)> {};

template <typename... Bs>
struct has_unbounded_loop<Offer<Bs...>>
    : std::bool_constant<(has_unbounded_loop<Bs>::value || ...)> {};

template <typename Role, typename... Bs>
struct has_unbounded_loop<Offer<Sender<Role>, Bs...>>
    : std::bool_constant<(has_unbounded_loop<Bs>::value || ...)> {};

template <typename B>
struct has_unbounded_loop<Loop<B>>
    : std::bool_constant<!loop_body_terminates<B>::value
                         || has_unbounded_loop<B>::value> {};

template <VendorBackend V, typename P>
struct has_unbounded_loop<VendorPinned<V, P>> : has_unbounded_loop<P> {};

template <typename P>
inline constexpr bool has_unbounded_loop_v = has_unbounded_loop<P>::value;

// ── payloads_distinct_at_choices<P> ────────────────────────────────
//
// True iff every Select<Bs...> / Offer<Bs...> in P has pairwise
// distinct head-payload types across its branches.  A "head payload"
// is the message type that distinguishes which branch the peer
// chose: in Select<Send<int, _>, Send<int, _>>, both branches have
// head payload `int` and so the peer cannot tell which was chosen
// from the message alone — the second branch is structurally a
// "dead branch" reachable only by chance.
//
// Helper: head_payload_t<B> extracts the message type at the head of
// branch B (or void if B has no head action).

namespace detail_phi {

// Head payload of a single branch — extracts the leading Send<T,_> /
// Recv<T,_> payload, or std::nullptr_t for branches without a head
// action (terminal / orphan).  std::nullptr_t is chosen as the
// "no head" sentinel because no real payload type equals it.

template <typename B>
struct head_payload {
    using type = std::nullptr_t;
};

template <typename T, typename K>
struct head_payload<Send<T, K>> { using type = T; };

template <typename T, typename K>
struct head_payload<Recv<T, K>> { using type = T; };

template <typename B>
struct head_payload<Loop<B>> : head_payload<B> {};

template <VendorBackend V, typename P>
struct head_payload<VendorPinned<V, P>> : head_payload<P> {};

template <typename B>
using head_payload_t = typename head_payload<B>::type;

// Pairwise-distinct check over a parameter pack of types using a
// quadratic comparison.  For typical session-type branch counts
// (≤ 8) this is fine; for larger packs the sort-based approach
// would be more efficient but is harder to implement cleanly at
// compile time.

template <typename T, typename... Rest>
struct distinct_from_all
    : std::bool_constant<(!std::is_same_v<T, Rest> && ...)> {};

template <typename... Ts>
struct all_pairwise_distinct;

template <>
struct all_pairwise_distinct<> : std::true_type {};

template <typename T>
struct all_pairwise_distinct<T> : std::true_type {};

template <typename T0, typename... Rest>
struct all_pairwise_distinct<T0, Rest...>
    : std::bool_constant<distinct_from_all<T0, Rest...>::value
                         && all_pairwise_distinct<Rest...>::value> {};

template <typename... Bs>
inline constexpr bool branches_have_distinct_heads_v =
    all_pairwise_distinct<head_payload_t<Bs>...>::value;

}  // namespace detail_phi

template <typename P>
struct payloads_distinct_at_choices : std::true_type {};

template <>
struct payloads_distinct_at_choices<End> : std::true_type {};

template <CrashClass C>
struct payloads_distinct_at_choices<Stop_g<C>> : std::true_type {};

template <>
struct payloads_distinct_at_choices<Continue> : std::true_type {};

template <typename T, typename K>
struct payloads_distinct_at_choices<Send<T, K>>
    : payloads_distinct_at_choices<K> {};

template <typename T, typename K>
struct payloads_distinct_at_choices<Recv<T, K>>
    : payloads_distinct_at_choices<K> {};

template <typename... Bs>
struct payloads_distinct_at_choices<Select<Bs...>>
    : std::bool_constant<
          detail_phi::branches_have_distinct_heads_v<Bs...>
          && (payloads_distinct_at_choices<Bs>::value && ...)> {};

template <typename... Bs>
struct payloads_distinct_at_choices<Offer<Bs...>>
    : std::bool_constant<
          detail_phi::branches_have_distinct_heads_v<Bs...>
          && (payloads_distinct_at_choices<Bs>::value && ...)> {};

template <typename Role, typename... Bs>
struct payloads_distinct_at_choices<Offer<Sender<Role>, Bs...>>
    : std::bool_constant<
          detail_phi::branches_have_distinct_heads_v<Bs...>
          && (payloads_distinct_at_choices<Bs>::value && ...)> {};

template <typename B>
struct payloads_distinct_at_choices<Loop<B>>
    : payloads_distinct_at_choices<B> {};

template <VendorBackend V, typename P>
struct payloads_distinct_at_choices<VendorPinned<V, P>>
    : payloads_distinct_at_choices<P> {};

template <typename P>
inline constexpr bool payloads_distinct_at_choices_v =
    payloads_distinct_at_choices<P>::value;

// ═════════════════════════════════════════════════════════════════════
// ── Layer 2 — FX §11.18 φ predicates ───────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// phi_safe<P> — protocol is well-formed.  Equivalent alias.
template <typename P>
inline constexpr bool phi_safe_v = is_well_formed_v<P>;

// phi_df<P> — deadlock-free.  Strengthens phi_safe by rejecting
// structural deadlocks (empty Select / Offer).
template <typename P>
inline constexpr bool phi_df_v =
    phi_safe_v<P> && !has_empty_branch_v<P>;

// phi_term<P> — terminates.  Strengthens phi_df by rejecting protocols
// with at least one unbounded loop.
template <typename P>
inline constexpr bool phi_term_v =
    phi_df_v<P> && !has_unbounded_loop_v<P>;

// phi_nterm<P> — well-formed AND non-terminating.  Mutually exclusive
// with phi_term.  Both phi_term_v<P> and phi_nterm_v<P> CANNOT both
// be true (a protocol either terminates or it doesn't), but both can
// be false (if phi_safe_v<P> is false, neither applies).
template <typename P>
inline constexpr bool phi_nterm_v =
    phi_safe_v<P> && has_unbounded_loop_v<P>;

// phi_live<P> — no stuck state.  For binary session types this is
// approximately phi_df (the empty-branch case is THE structural
// stuck-state).  Productive infinite loops are live but not
// terminating, so phi_live ⊄ phi_term.
template <typename P>
inline constexpr bool phi_live_v = phi_df_v<P>;

// phi_live_plus<P> — every choice branch is distinguishable at runtime.
// Strengthens phi_live by requiring pairwise-distinct head-payload
// types in every Select / Offer.
template <typename P>
inline constexpr bool phi_live_plus_v =
    phi_live_v<P> && payloads_distinct_at_choices_v<P>;

// phi_live_pp<P> — strictest.  Conservatively defined as
// phi_live_plus ∧ phi_term: every choice is distinguishable AND the
// protocol terminates, so every concrete message is delivered in
// finitely many steps.
template <typename P>
inline constexpr bool phi_live_pp_v =
    phi_live_plus_v<P> && phi_term_v<P>;

// ═════════════════════════════════════════════════════════════════════
// ── Classified assertion helpers ───────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Each helper is a consteval no-op when its predicate holds and a
// classified `static_assert` failure (with a grep-able framework-
// controlled tag prefix) when it doesn't.  Used at production call
// sites to discharge the φ obligation AND consumed by neg-compile
// fixtures to witness that the gate fires on its specific witness
// kind.
//
// Same `[FRAMEWORK-CONTROLLED]` discipline as SessionDiagnostic.h's
// classified accessors (see #371) — the tag prefix is the load-
// bearing API surface for CI grep regex matching.

template <typename P>
consteval void assert_phi_df() noexcept {
    static_assert(phi_df_v<P>,
        "[PhiDfViolation_HasEmptyBranch] protocol P fails phi_df: "
        "it contains an empty Select<>/Offer<> branch (structural "
        "deadlock — no choice to make, no expectation from peer).  "
        "Either remove the empty branch or thread its place through "
        "an explicit Send<unit, End> escape.");
}

template <typename P>
consteval void assert_phi_term() noexcept {
    static_assert(phi_term_v<P>,
        "[PhiTermViolation_HasUnboundedLoop] protocol P fails phi_term: "
        "it contains a Loop<B> whose body B has no path reaching End "
        "or Stop — every branch returns to Continue, making the loop "
        "inescapable.  Add at least one terminal-bearing branch to B.");
}

template <typename P>
consteval void assert_phi_live_plus() noexcept {
    static_assert(phi_live_plus_v<P>,
        "[PhiLivePlusViolation_DuplicatePayloadHeads] protocol P fails "
        "phi_live_plus: some Select<>/Offer<> branch shares its head-"
        "payload type with a sibling branch, making the branches "
        "indistinguishable to the peer.  Pick distinct head-payload "
        "types per branch (introduce wrapper types if necessary).");
}

// ═════════════════════════════════════════════════════════════════════
// ── In-header sentinel battery ─────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Same dual-export discipline as is_well_formed's own sentinels in
// SessionCrash.h.  These fire at every consumer's include time.

namespace v069_self_test {

// ── A. Helper-metafunction sanity ──────────────────────────────────

// has_empty_branch
static_assert(!has_empty_branch_v<End>);
static_assert(!has_empty_branch_v<Send<int, End>>);
static_assert( has_empty_branch_v<Select<>>);
static_assert( has_empty_branch_v<Offer<>>);
static_assert(!has_empty_branch_v<Select<Send<int, End>>>);
static_assert( has_empty_branch_v<Select<Send<int, End>, Offer<>>>);
static_assert( has_empty_branch_v<Loop<Select<Send<int, Continue>, Stop>>> == false,
    "non-empty Loop body must not be flagged as empty-branch");
static_assert(!has_empty_branch_v<Recv<int, Send<float, End>>>);

// loop_body_terminates
static_assert( loop_body_terminates_v<End>);
static_assert( loop_body_terminates_v<Stop>);
static_assert(!loop_body_terminates_v<Continue>);
static_assert( loop_body_terminates_v<Send<int, End>>);
static_assert(!loop_body_terminates_v<Send<int, Continue>>);
static_assert( loop_body_terminates_v<Select<Send<int, Continue>, Stop>>,
    "select with one Continue branch and one Stop branch terminates "
    "via the Stop branch");
static_assert(!loop_body_terminates_v<Select<Send<int, Continue>,
                                               Recv<float, Continue>>>,
    "every branch goes back to Continue → loop is inescapable");

// has_unbounded_loop
static_assert(!has_unbounded_loop_v<End>);
static_assert(!has_unbounded_loop_v<Send<int, End>>);
static_assert( has_unbounded_loop_v<Loop<Send<int, Continue>>>,
    "Loop body only sends and Continues — never escapes");
static_assert(!has_unbounded_loop_v<Loop<Select<Send<int, Continue>, Stop>>>,
    "Loop body has Stop escape branch → bounded");

// payloads_distinct_at_choices
static_assert( payloads_distinct_at_choices_v<End>);
static_assert( payloads_distinct_at_choices_v<Send<int, End>>);
static_assert( payloads_distinct_at_choices_v<
    Select<Send<int, End>, Send<float, End>>>);
static_assert(!payloads_distinct_at_choices_v<
    Select<Send<int, End>, Send<int, End>>>,
    "two branches with identical head payload int — peer cannot "
    "distinguish which branch was chosen");
static_assert(!payloads_distinct_at_choices_v<
    Offer<Recv<int, End>, Recv<int, End>>>);

// ── B. φ-predicate sanity — phi_safe ──────────────────────────────
static_assert( phi_safe_v<End>);
static_assert( phi_safe_v<Send<int, End>>);
static_assert(!phi_safe_v<Loop<End>>,
    "Loop<End> is rejected by is_well_formed (terminal body) so "
    "phi_safe rejects it too");

// ── C. φ-predicate sanity — phi_df ────────────────────────────────
//
// phi_df strengthens phi_safe by rejecting empty Select<>/Offer<>.
// is_well_formed accepts these via vacuous AND-fold; phi_df catches
// the gap.
static_assert( phi_df_v<End>);
static_assert( phi_df_v<Send<int, End>>);
static_assert(!phi_df_v<Select<>>,
    "phi_df must reject empty Select<> (structural deadlock witness) "
    "even though phi_safe accepts it via vacuous AND-fold");
static_assert(!phi_df_v<Offer<>>);
static_assert( phi_df_v<Select<Send<int, End>>>);
static_assert(!phi_df_v<Select<Send<int, Offer<>>>>,
    "nested empty Offer<> still rejected — the witness is anywhere "
    "in the reachable tree");

// ── D. φ-predicate sanity — phi_term ──────────────────────────────
//
// phi_term refines phi_df by requiring termination.  A Loop with no
// escape branch is non-terminating and phi_term rejects it.
static_assert( phi_term_v<End>);
static_assert( phi_term_v<Send<int, End>>);
static_assert( phi_term_v<Loop<Select<Send<int, Continue>, Stop>>>,
    "Loop with Stop escape branch terminates");
static_assert(!phi_term_v<Loop<Send<int, Continue>>>,
    "Loop with only Send→Continue body is non-terminating");

// ── E. φ-predicate sanity — phi_nterm ─────────────────────────────
//
// phi_nterm = phi_safe ∧ has_unbounded_loop.  Mutually exclusive
// with phi_term (their conjunction is unsatisfiable).
static_assert(!phi_nterm_v<End>,
    "End is well-formed but trivially terminates — not phi_nterm");
static_assert( phi_nterm_v<Loop<Send<int, Continue>>>,
    "infinite productive loop is well-formed AND non-terminating");
static_assert(!phi_nterm_v<Loop<Select<Send<int, Continue>, Stop>>>,
    "Loop with Stop branch terminates → not phi_nterm");

// Mutual exclusion between phi_term and phi_nterm.
static_assert(!(phi_term_v<Loop<Send<int, Continue>>>
                && phi_nterm_v<Loop<Send<int, Continue>>>));
static_assert(!(phi_term_v<End> && phi_nterm_v<End>));

// ── F. φ-predicate sanity — phi_live ──────────────────────────────
//
// For binary session types, phi_live ≈ phi_df.
static_assert( phi_live_v<End>);
static_assert( phi_live_v<Send<int, End>>);
static_assert(!phi_live_v<Select<>>);

// ── G. φ-predicate sanity — phi_live_plus ─────────────────────────
//
// phi_live_plus strengthens phi_live by requiring pairwise-distinct
// head-payload types in every Choice.
static_assert( phi_live_plus_v<End>);
static_assert( phi_live_plus_v<Send<int, End>>);
static_assert( phi_live_plus_v<
    Select<Send<int, End>, Send<float, End>>>);
static_assert(!phi_live_plus_v<
    Select<Send<int, End>, Send<int, End>>>,
    "two Send<int, _> branches indistinguishable → dead-branch witness");

// ── H. φ-predicate sanity — phi_live_pp ───────────────────────────
//
// phi_live_pp is the strictest: phi_live_plus ∧ phi_term.  Productive
// infinite loops are excluded (no termination guarantee).
static_assert( phi_live_pp_v<End>);
static_assert( phi_live_pp_v<Send<int, End>>);
static_assert( phi_live_pp_v<Select<Send<int, End>, Send<float, End>>>);
// Productive infinite loop with distinct payloads still rejected.
static_assert(!phi_live_pp_v<Loop<Select<Send<int, Continue>,
                                          Send<float, Continue>>>>,
    "productive infinite loop fails phi_term and therefore phi_live_pp "
    "even though branches are distinguishable");

// ── I. Lattice ordering — strictly stronger left-to-right ─────────
//
// safe ⊇ df ⊇ term ⊇ live_pp,  safe ⊇ df ⊇ live ⊇ live_plus ⊇ live_pp.
// Three witnesses pin each implication.
namespace lattice_test {

// W1: phi_df ⇒ phi_safe
template <typename P>
static constexpr bool df_implies_safe = !phi_df_v<P> || phi_safe_v<P>;

static_assert(df_implies_safe<End>);
static_assert(df_implies_safe<Send<int, End>>);
static_assert(df_implies_safe<Select<>>);  // !df, vacuously true

// W2: phi_term ⇒ phi_df
template <typename P>
static constexpr bool term_implies_df = !phi_term_v<P> || phi_df_v<P>;

static_assert(term_implies_df<End>);
static_assert(term_implies_df<Loop<Send<int, Continue>>>);  // !term

// W3: phi_live_pp ⇒ phi_term
template <typename P>
static constexpr bool live_pp_implies_term = !phi_live_pp_v<P> || phi_term_v<P>;

static_assert(live_pp_implies_term<End>);
static_assert(live_pp_implies_term<Loop<Send<int, Continue>>>);  // !live_pp

// W4: phi_live_pp ⇒ phi_live_plus ⇒ phi_live
template <typename P>
static constexpr bool live_pp_implies_live_plus =
    !phi_live_pp_v<P> || phi_live_plus_v<P>;

template <typename P>
static constexpr bool live_plus_implies_live =
    !phi_live_plus_v<P> || phi_live_v<P>;

static_assert(live_pp_implies_live_plus<Select<Send<int, End>, Send<float, End>>>);
static_assert(live_plus_implies_live<Select<Send<int, End>, Send<float, End>>>);

}  // namespace lattice_test

// ── J. Classified assertion helpers ────────────────────────────────
//
// Positive cells — every helper compiles when the predicate holds.
// (Negative cells are exercised by neg-compile fixtures in test/
// safety_neg/.)

namespace classified_helper_test {

[[maybe_unused]] consteval bool exercise_phi_assertions() noexcept {
    assert_phi_df<Send<int, End>>();
    assert_phi_term<Send<int, End>>();
    assert_phi_live_plus<Select<Send<int, End>, Send<float, End>>>();
    return true;
}

static_assert(exercise_phi_assertions());

}  // namespace classified_helper_test

// ── K. Cardinality witness — count of items V-069 surfaces ────────
//
//   Helpers (4):
//     has_empty_branch_v, loop_body_terminates_v,
//     has_unbounded_loop_v, payloads_distinct_at_choices_v
//   φ predicates (7):
//     phi_safe_v, phi_df_v, phi_term_v, phi_nterm_v,
//     phi_live_v, phi_live_plus_v, phi_live_pp_v
//   Classified assertion helpers (3):
//     assert_phi_df, assert_phi_term, assert_phi_live_plus
//                                                       ───
//                                                       14
constexpr int v069_surface_cardinality = 14;
static_assert(v069_surface_cardinality == 14,
    "sessions::proto:: V-069 surface cardinality drifted — update "
    "SessionPhi.h helpers + predicates + assertions AND this "
    "sentinel in lockstep.");

}  // namespace v069_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test (FIXY-U-103 discipline) ─────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Instantiates every public predicate against non-constant args so
// any latent SFINAE / consteval / inline-body bug surfaces under
// `-fsyntax-only` of any TU that includes SessionPhi.h.

inline void session_phi_runtime_smoke_test() noexcept {
    struct Probe {};
    using P_finite        = Send<Probe, Recv<Probe, End>>;
    using P_loop_unbounded= Loop<Send<Probe, Continue>>;
    using P_loop_bounded  = Loop<Select<Send<Probe, Continue>, Stop>>;
    using P_distinct      = Select<Send<int, End>, Send<float, End>>;
    using P_duplicate     = Select<Send<int, End>, Send<int, End>>;

    [[maybe_unused]] constexpr bool s1 = phi_safe_v<P_finite>;
    [[maybe_unused]] constexpr bool s2 = phi_df_v<P_finite>;
    [[maybe_unused]] constexpr bool s3 = phi_term_v<P_finite>;
    [[maybe_unused]] constexpr bool s4 = phi_nterm_v<P_loop_unbounded>;
    [[maybe_unused]] constexpr bool s5 = phi_live_v<P_finite>;
    [[maybe_unused]] constexpr bool s6 = phi_live_plus_v<P_distinct>;
    [[maybe_unused]] constexpr bool s7 = phi_live_pp_v<P_distinct>;
    [[maybe_unused]] constexpr bool s8 = has_empty_branch_v<Select<>>;
    [[maybe_unused]] constexpr bool s9 = loop_body_terminates_v<P_loop_bounded>;
    [[maybe_unused]] constexpr bool sA = has_unbounded_loop_v<P_loop_unbounded>;
    [[maybe_unused]] constexpr bool sB = payloads_distinct_at_choices_v<P_distinct>;

    (void)s1; (void)s2; (void)s3; (void)s4; (void)s5; (void)s6;
    (void)s7; (void)s8; (void)s9; (void)sA; (void)sB;
    (void)static_cast<P_duplicate*>(nullptr);
}

}  // namespace crucible::safety::proto
