#pragma once

// A hierarchy of session-safety properties, as compile-time predicates
// over a binary protocol tree.  The abbreviated names stand for:
//
//   phi_safe       the protocol is well-formed.
//   phi_df         deadlock-free: no choice is left with no branch to
//                  take.
//   phi_term       every loop has a path out.
//   phi_nterm      at least one loop has no path out.
//   phi_live       no stuck state.
//   phi_live_plus  every branch of a choice is distinguishable from its
//                  siblings by the message that selects it.
//   phi_live_pp    every concrete message is delivered.
//
// The order is two chains meeting at both ends, not one:
//
//   safe ⊇ df ⊇ term      ⊇ live_pp
//   safe ⊇ df ⊇ live ⊇ live_plus ⊇ live_pp
//
// phi_nterm sits outside both.  It is the sibling of phi_term rather
// than a strengthening of anything: the two cannot both hold, and both
// fail when phi_safe fails.
//
// Every predicate is sound and deliberately incomplete.  A protocol
// that passes genuinely has the property.  Some protocols that have the
// property are rejected, because refuting one of these properties has a
// finite structural witness and confirming one in general does not.

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCrash.h>
#include <type_traits>

namespace crucible::safety::proto {

// A choice with no branches leaves the deciding side nothing to pick
// and the peer nothing to expect, so the protocol cannot progress.
// Well-formedness accepts it, because its branch-wise conjunction is
// vacuously true over an empty pack.
//
// This one witness is enough for a binary protocol.  Every other way to
// get stuck is already ruled out by well-formedness together with
// duality, since a send is always faced by the matching receive.

template <typename P>
struct has_empty_branch : std::false_type {};

template <>
struct has_empty_branch<End> : std::false_type {};

template <CrashClass C>
struct has_empty_branch<Stop_g<C>> : std::false_type {};

template <>
struct has_empty_branch<Continue> : std::false_type {};

template <typename T, typename K>
struct has_empty_branch<Send<T, K>> : has_empty_branch<K> {};

template <typename T, typename K>
struct has_empty_branch<Recv<T, K>> : has_empty_branch<K> {};

template <typename B>
struct has_empty_branch<Loop<B>> : has_empty_branch<B> {};

template <VendorBackend V, typename P>
struct has_empty_branch<VendorPinned<V, P>> : has_empty_branch<P> {};

template <>
struct has_empty_branch<Select<>> : std::true_type {};

template <>
struct has_empty_branch<Offer<>> : std::true_type {};

template <typename B0, typename... Bs>
struct has_empty_branch<Select<B0, Bs...>>
    : std::bool_constant<(has_empty_branch<B0>::value || (has_empty_branch<Bs>::value || ...))> {};

template <typename B0, typename... Bs>
struct has_empty_branch<Offer<B0, Bs...>>
    : std::bool_constant<(has_empty_branch<B0>::value || (has_empty_branch<Bs>::value || ...))> {};

// The leading Sender tag is metadata rather than a branch, so it is not
// walked and does not count towards the branch total.
template <typename Role, typename B0, typename... Bs>
struct has_empty_branch<Offer<Sender<Role>, B0, Bs...>>
    : std::bool_constant<(has_empty_branch<B0>::value || (has_empty_branch<Bs>::value || ...))> {};

template <typename Role>
struct has_empty_branch<Offer<Sender<Role>>> : std::true_type {};

template <typename P>
inline constexpr bool has_empty_branch_v = has_empty_branch<P>::value;

// A loop body terminates when at least one path through it reaches a
// terminal state without passing through Continue.  A body that always
// reaches Continue is an inescapable cycle.
//
// Offer folds disjunctively, the same way Select does.  The requirement
// is that an escape path exists, not that the protocol is driven down
// it, so it makes no difference which side picks the branch.

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
struct loop_body_terminates<Select<Bs...>> : std::bool_constant<(loop_body_terminates<Bs>::value || ...)> {};

template <typename... Bs>
struct loop_body_terminates<Offer<Bs...>> : std::bool_constant<(loop_body_terminates<Bs>::value || ...)> {};

template <typename Role, typename... Bs>
struct loop_body_terminates<Offer<Sender<Role>, Bs...>> : std::bool_constant<(loop_body_terminates<Bs>::value || ...)> {
};

template <typename B>
struct loop_body_terminates<Loop<B>> : loop_body_terminates<B> {};

template <VendorBackend V, typename P>
struct loop_body_terminates<VendorPinned<V, P>> : loop_body_terminates<P> {};

template <>
struct loop_body_terminates<Select<>> : std::false_type {};

template <>
struct loop_body_terminates<Offer<>> : std::false_type {};

template <typename B>
inline constexpr bool loop_body_terminates_v = loop_body_terminates<B>::value;

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
struct has_unbounded_loop<Select<Bs...>> : std::bool_constant<(has_unbounded_loop<Bs>::value || ...)> {};

template <typename... Bs>
struct has_unbounded_loop<Offer<Bs...>> : std::bool_constant<(has_unbounded_loop<Bs>::value || ...)> {};

template <typename Role, typename... Bs>
struct has_unbounded_loop<Offer<Sender<Role>, Bs...>> : std::bool_constant<(has_unbounded_loop<Bs>::value || ...)> {};

template <typename B>
struct has_unbounded_loop<Loop<B>>
    : std::bool_constant<!loop_body_terminates<B>::value || has_unbounded_loop<B>::value> {};

template <VendorBackend V, typename P>
struct has_unbounded_loop<VendorPinned<V, P>> : has_unbounded_loop<P> {};

template <typename P>
inline constexpr bool has_unbounded_loop_v = has_unbounded_loop<P>::value;

// The message at the head of a branch is what tells the peer which
// branch was taken.  Two branches that lead with the same message type
// are indistinguishable on the wire, so one of them is reachable only
// by accident.

namespace detail_phi {

// A branch with no leading action is given nullptr_t, which no real
// payload type can collide with.

template <typename B>
struct head_payload {
    using type = std::nullptr_t;
};

template <typename T, typename K>
struct head_payload<Send<T, K>> {
    using type = T;
};

template <typename T, typename K>
struct head_payload<Recv<T, K>> {
    using type = T;
};

template <typename B>
struct head_payload<Loop<B>> : head_payload<B> {};

template <VendorBackend V, typename P>
struct head_payload<VendorPinned<V, P>> : head_payload<P> {};

template <typename B>
using head_payload_t = typename head_payload<B>::type;

// The comparison is quadratic in the branch count.  A sort-based check
// would scale better but is far harder to write at compile time, and a
// choice rarely carries more than a handful of branches.

template <typename T, typename... Rest>
struct distinct_from_all : std::bool_constant<(!std::is_same_v<T, Rest> && ...)> {};

template <typename... Ts>
struct all_pairwise_distinct;

template <>
struct all_pairwise_distinct<> : std::true_type {};

template <typename T>
struct all_pairwise_distinct<T> : std::true_type {};

template <typename T0, typename... Rest>
struct all_pairwise_distinct<T0, Rest...>
    : std::bool_constant<distinct_from_all<T0, Rest...>::value && all_pairwise_distinct<Rest...>::value> {};

template <typename... Bs>
inline constexpr bool branches_have_distinct_heads_v = all_pairwise_distinct<head_payload_t<Bs>...>::value;

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
struct payloads_distinct_at_choices<Send<T, K>> : payloads_distinct_at_choices<K> {};

template <typename T, typename K>
struct payloads_distinct_at_choices<Recv<T, K>> : payloads_distinct_at_choices<K> {};

template <typename... Bs>
struct payloads_distinct_at_choices<Select<Bs...>>
    : std::bool_constant<detail_phi::branches_have_distinct_heads_v<Bs...>
                         && (payloads_distinct_at_choices<Bs>::value && ...)> {};

template <typename... Bs>
struct payloads_distinct_at_choices<Offer<Bs...>>
    : std::bool_constant<detail_phi::branches_have_distinct_heads_v<Bs...>
                         && (payloads_distinct_at_choices<Bs>::value && ...)> {};

template <typename Role, typename... Bs>
struct payloads_distinct_at_choices<Offer<Sender<Role>, Bs...>>
    : std::bool_constant<detail_phi::branches_have_distinct_heads_v<Bs...>
                         && (payloads_distinct_at_choices<Bs>::value && ...)> {};

template <typename B>
struct payloads_distinct_at_choices<Loop<B>> : payloads_distinct_at_choices<B> {};

template <VendorBackend V, typename P>
struct payloads_distinct_at_choices<VendorPinned<V, P>> : payloads_distinct_at_choices<P> {};

template <typename P>
inline constexpr bool payloads_distinct_at_choices_v = payloads_distinct_at_choices<P>::value;

template <typename P>
inline constexpr bool phi_safe_v = is_well_formed_v<P>;

template <typename P>
inline constexpr bool phi_df_v = phi_safe_v<P> && !has_empty_branch_v<P>;

template <typename P>
inline constexpr bool phi_term_v = phi_df_v<P> && !has_unbounded_loop_v<P>;

template <typename P>
inline constexpr bool phi_nterm_v = phi_safe_v<P> && has_unbounded_loop_v<P>;

// For a binary protocol the branchless choice is the only structural
// stuck state, so liveness coincides with deadlock freedom here.
template <typename P>
inline constexpr bool phi_live_v = phi_df_v<P>;

template <typename P>
inline constexpr bool phi_live_plus_v = phi_live_v<P> && payloads_distinct_at_choices_v<P>;

// Delivery of every message needs both halves: branches that the peer
// can tell apart, and a bound on how many steps each message waits.
template <typename P>
inline constexpr bool phi_live_pp_v = phi_live_plus_v<P> && phi_term_v<P>;

// The bracketed tag opening each message below is matched by a CI grep,
// so it stays verbatim.

template <typename P>
consteval void assert_phi_df() noexcept {
    static_assert(phi_df_v<P>, "[PhiDfViolation_HasEmptyBranch] protocol P fails phi_df: "
                               "it contains an empty Select<>/Offer<> branch (structural "
                               "deadlock — no choice to make, no expectation from peer).  "
                               "Either remove the empty branch or thread its place through "
                               "an explicit Send<unit, End> escape.");
}

template <typename P>
consteval void assert_phi_term() noexcept {
    static_assert(phi_term_v<P>, "[PhiTermViolation_HasUnboundedLoop] protocol P fails phi_term: "
                                 "it contains a Loop<B> whose body B has no path reaching End "
                                 "or Stop — every branch returns to Continue, making the loop "
                                 "inescapable.  Add at least one terminal-bearing branch to B.");
}

template <typename P>
consteval void assert_phi_live_plus() noexcept {
    static_assert(phi_live_plus_v<P>, "[PhiLivePlusViolation_DuplicatePayloadHeads] protocol P fails "
                                      "phi_live_plus: some Select<>/Offer<> branch shares its head-"
                                      "payload type with a sibling branch, making the branches "
                                      "indistinguishable to the peer.  Pick distinct head-payload "
                                      "types per branch (introduce wrapper types if necessary).");
}

namespace v069_self_test {

static_assert(!has_empty_branch_v<End>);
static_assert(!has_empty_branch_v<Send<int, End>>);
static_assert(has_empty_branch_v<Select<>>);
static_assert(has_empty_branch_v<Offer<>>);
static_assert(!has_empty_branch_v<Select<Send<int, End>>>);
static_assert(has_empty_branch_v<Select<Send<int, End>, Offer<>>>);
static_assert(has_empty_branch_v<Loop<Select<Send<int, Continue>, Stop>>> == false,
              "non-empty Loop body must not be flagged as empty-branch");
static_assert(!has_empty_branch_v<Recv<int, Send<float, End>>>);

static_assert(loop_body_terminates_v<End>);
static_assert(loop_body_terminates_v<Stop>);
static_assert(!loop_body_terminates_v<Continue>);
static_assert(loop_body_terminates_v<Send<int, End>>);
static_assert(!loop_body_terminates_v<Send<int, Continue>>);
static_assert(loop_body_terminates_v<Select<Send<int, Continue>, Stop>>,
              "select with one Continue branch and one Stop branch terminates "
              "via the Stop branch");
static_assert(!loop_body_terminates_v<Select<Send<int, Continue>, Recv<float, Continue>>>,
              "every branch goes back to Continue → loop is inescapable");

static_assert(!has_unbounded_loop_v<End>);
static_assert(!has_unbounded_loop_v<Send<int, End>>);
static_assert(has_unbounded_loop_v<Loop<Send<int, Continue>>>, "Loop body only sends and Continues — never escapes");
static_assert(!has_unbounded_loop_v<Loop<Select<Send<int, Continue>, Stop>>>,
              "Loop body has Stop escape branch → bounded");

static_assert(payloads_distinct_at_choices_v<End>);
static_assert(payloads_distinct_at_choices_v<Send<int, End>>);
static_assert(payloads_distinct_at_choices_v<Select<Send<int, End>, Send<float, End>>>);
static_assert(!payloads_distinct_at_choices_v<Select<Send<int, End>, Send<int, End>>>,
              "two branches with identical head payload int — peer cannot "
              "distinguish which branch was chosen");
static_assert(!payloads_distinct_at_choices_v<Offer<Recv<int, End>, Recv<int, End>>>);

static_assert(phi_safe_v<End>);
static_assert(phi_safe_v<Send<int, End>>);
static_assert(!phi_safe_v<Loop<End>>, "Loop<End> is rejected by is_well_formed (terminal body) so "
                                      "phi_safe rejects it too");

static_assert(phi_df_v<End>);
static_assert(phi_df_v<Send<int, End>>);
static_assert(!phi_df_v<Select<>>, "phi_df must reject empty Select<> (structural deadlock witness) "
                                   "even though phi_safe accepts it via vacuous AND-fold");
static_assert(!phi_df_v<Offer<>>);
static_assert(phi_df_v<Select<Send<int, End>>>);
static_assert(!phi_df_v<Select<Send<int, Offer<>>>>, "nested empty Offer<> still rejected — the witness is anywhere "
                                                     "in the reachable tree");

static_assert(phi_term_v<End>);
static_assert(phi_term_v<Send<int, End>>);
static_assert(phi_term_v<Loop<Select<Send<int, Continue>, Stop>>>, "Loop with Stop escape branch terminates");
static_assert(!phi_term_v<Loop<Send<int, Continue>>>, "Loop with only Send→Continue body is non-terminating");

static_assert(!phi_nterm_v<End>, "End is well-formed but trivially terminates — not phi_nterm");
static_assert(phi_nterm_v<Loop<Send<int, Continue>>>, "infinite productive loop is well-formed AND non-terminating");
static_assert(!phi_nterm_v<Loop<Select<Send<int, Continue>, Stop>>>,
              "Loop with Stop branch terminates → not phi_nterm");

static_assert(!(phi_term_v<Loop<Send<int, Continue>>> && phi_nterm_v<Loop<Send<int, Continue>>>));
static_assert(!(phi_term_v<End> && phi_nterm_v<End>));

static_assert(phi_live_v<End>);
static_assert(phi_live_v<Send<int, End>>);
static_assert(!phi_live_v<Select<>>);

static_assert(phi_live_plus_v<End>);
static_assert(phi_live_plus_v<Send<int, End>>);
static_assert(phi_live_plus_v<Select<Send<int, End>, Send<float, End>>>);
static_assert(!phi_live_plus_v<Select<Send<int, End>, Send<int, End>>>,
              "two Send<int, _> branches indistinguishable → dead-branch witness");

static_assert(phi_live_pp_v<End>);
static_assert(phi_live_pp_v<Send<int, End>>);
static_assert(phi_live_pp_v<Select<Send<int, End>, Send<float, End>>>);
static_assert(!phi_live_pp_v<Loop<Select<Send<int, Continue>, Send<float, Continue>>>>,
              "productive infinite loop fails phi_term and therefore phi_live_pp "
              "even though branches are distinguishable");

namespace lattice_test {

template <typename P>
static constexpr bool df_implies_safe = !phi_df_v<P> || phi_safe_v<P>;

static_assert(df_implies_safe<End>);
static_assert(df_implies_safe<Send<int, End>>);
static_assert(df_implies_safe<Select<>>);

template <typename P>
static constexpr bool term_implies_df = !phi_term_v<P> || phi_df_v<P>;

static_assert(term_implies_df<End>);
static_assert(term_implies_df<Loop<Send<int, Continue>>>);

template <typename P>
static constexpr bool live_pp_implies_term = !phi_live_pp_v<P> || phi_term_v<P>;

static_assert(live_pp_implies_term<End>);
static_assert(live_pp_implies_term<Loop<Send<int, Continue>>>);

template <typename P>
static constexpr bool live_pp_implies_live_plus = !phi_live_pp_v<P> || phi_live_plus_v<P>;

template <typename P>
static constexpr bool live_plus_implies_live = !phi_live_plus_v<P> || phi_live_v<P>;

static_assert(live_pp_implies_live_plus<Select<Send<int, End>, Send<float, End>>>);
static_assert(live_plus_implies_live<Select<Send<int, End>, Send<float, End>>>);

}  // namespace lattice_test

namespace classified_helper_test {

[[maybe_unused]] consteval bool exercise_phi_assertions() noexcept {
    assert_phi_df<Send<int, End>>();
    assert_phi_term<Send<int, End>>();
    assert_phi_live_plus<Select<Send<int, End>, Send<float, End>>>();
    return true;
}

static_assert(exercise_phi_assertions());

}  // namespace classified_helper_test

// Four structural helpers, seven predicates and three assertion
// helpers make up the public surface.
constexpr int v069_surface_cardinality = 14;
static_assert(v069_surface_cardinality == 14, "sessions::proto:: the public surface of this header changed.  Update "
                                              "the helpers, predicates and assertion helpers together with this "
                                              "count.");

}  // namespace v069_self_test

// Naming every public predicate against arguments the compiler cannot
// fold away surfaces a latent substitution or consteval fault in any
// translation unit that includes this header.

inline void session_phi_runtime_smoke_test() noexcept {
    struct Probe {};
    using P_finite = Send<Probe, Recv<Probe, End>>;
    using P_loop_unbounded = Loop<Send<Probe, Continue>>;
    using P_loop_bounded = Loop<Select<Send<Probe, Continue>, Stop>>;
    using P_distinct = Select<Send<int, End>, Send<float, End>>;
    using P_duplicate = Select<Send<int, End>, Send<int, End>>;

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

    (void)s1;
    (void)s2;
    (void)s3;
    (void)s4;
    (void)s5;
    (void)s6;
    (void)s7;
    (void)s8;
    (void)s9;
    (void)sA;
    (void)sB;
    (void)static_cast<P_duplicate*>(nullptr);
}

}  // namespace crucible::safety::proto
