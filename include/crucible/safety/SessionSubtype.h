#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::safety::proto — Gay-Hole synchronous subtyping
//
// Classical sync session-type subtyping from Gay-Hole 2005, lifted
// onto the binary combinators in Session.h.  Six rules, each a partial
// specialisation of the `is_subtype_sync` metafunction:
//
//   End                  ⩽  End
//   Continue             ⩽  Continue     (coinductive hypothesis: the
//                                         two enclosing Loops are
//                                         already in the relation)
//   Send<P₁, R₁>         ⩽  Send<P₂, R₂>    ⇔  P₁ ⩽ P₂  ∧  R₁ ⩽ R₂
//                                             (covariant in payload
//                                              AND covariant in continuation)
//   Recv<P₁, R₁>         ⩽  Recv<P₂, R₂>    ⇔  P₂ ⩽ P₁  ∧  R₁ ⩽ R₂
//                                             (contravariant in payload,
//                                              covariant in continuation)
//   Select<B₁,…,Bₙ>      ⩽  Select<C₁,…,Cₘ>  ⇔  n ≤ m  ∧  ∀ i∈[0,n): Bᵢ ⩽ Cᵢ
//                                             (subtype picks FEWER options
//                                              — narrower internal choice)
//   Offer<B₁,…,Bₙ>       ⩽  Offer<C₁,…,Cₘ>   ⇔  n ≥ m  ∧  ∀ i∈[0,m): Bᵢ ⩽ Cᵢ
//                                             (subtype handles MORE options
//                                              — broader external handling)
//   Loop<B₁>             ⩽  Loop<B₂>         ⇔  B₁ ⩽ B₂ (coinductive)
//
// All other shape pairs are rejected by the primary `is_subtype_sync`
// template (std::false_type).  In particular Send ⩽ Recv, Select ⩽
// Offer, etc. are all false.
//
// ─── Value-type subtyping ────────────────────────────────────────────
//
// Subtyping on payload types is a primitive of the framework.  We
// expose it as `is_subsort<T, U>` — users specialise to declare that
// T is a subtype of U.  The default is std::is_same — invariant, so
// only exact payload types match.  Specialise for integer-width
// promotion, nominal hierarchies, or whatever's semantically sound.
//
// Example:
//
//   // Our codebase: NonZero-refined int is a subtype of plain int.
//   template <>
//   struct crucible::safety::proto::is_subsort<
//       crucible::safety::Refined<NonZero, int>,
//       int
//   > : std::true_type {};
//
// ─── Positional vs labeled branches ──────────────────────────────────
//
// Gay-Hole's original formulation uses NAMED labels (e.g.,
// Select<commit, rollback>).  Our Select/Offer use POSITIONAL
// branches — no labels, just tuple positions.  The subtyping rules
// above are the positional analog: instead of "every subtype label
// must have a matching supertype label", we require "the subtype's
// branch prefix must be subtypes of the supertype's matching prefix".
//
// This is slightly stricter than labeled Gay-Hole — a "reorder
// branches" refactor isn't automatically a subtype in our scheme —
// but it matches Crucible's pattern library (compile-time
// positional).  Reorderings that Gay-Hole would allow can be expressed
// by explicit refactor of the affected Select / Offer alongside its
// consumers.
//
// ─── What this is NOT ────────────────────────────────────────────────
//
// This header implements SYNCHRONOUS subtyping only.  Asynchronous
// precise subtyping ⩽_a (GPPSY23 SISO decomposition) is deferred to
// task SEPLOG-I6.  Under synchronous semantics, message order is
// strictly preserved across refinement — the subtype cannot anticipate
// a Send past an unrelated Recv from a different participant.  Use
// sync subtyping for protocols where the runtime is rendezvous-like
// (TraceRing SPSC, ChainEdge semaphores, most of CRUCIBLE.md Part IV's
// §IV.1–§IV.10 channels); use async subtyping for those where the
// runtime is a bounded FIFO (MpmcRing, CNTP async collectives).
//
// ─── Usage ───────────────────────────────────────────────────────────
//
//   using ProtoV1 = Loop<Select<Send<PingReq,  End>,
//                                Send<CloseReq, End>>>;
//   using ProtoV2 = Loop<Select<Send<PingReq,  End>>>;
//
//   static_assert(is_subtype_sync_v<ProtoV2, ProtoV1>);   // v2 narrows
//   static_assert(!is_subtype_sync_v<ProtoV1, ProtoV2>);  // v1 is not a subtype
//
// For protocol-evolution use cases — "can I use an implementation of
// the new protocol where the old one was expected?" — the answer is
// is_subtype_sync_v<NewProto, OldProto>.
//
// ─── References ──────────────────────────────────────────────────────
//
//   Gay, S., Hole, M. (2005).  "Subtyping for Session Types in the
//     Pi Calculus."  Acta Informatica 42(2-3):191-225.  The classical
//     rules lifted here.
//   Honda, K., Yoshida, N., Carbone, M. (2016).  "Multiparty
//     Asynchronous Session Types."  JACM.  Confirms Gay-Hole
//     subtyping as the correct synchronous refinement at each
//     projection of an MPST.
//   Ghilezan, S., Pantović, J., Prokić, I., Scalas, A.,
//     Yoshida, N. (2023).  "Precise Subtyping for Asynchronous
//     Multiparty Sessions."  TCL.  The async extension; deferred
//     to SEPLOG-I6.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/Session.h>

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto {

// ═════════════════════════════════════════════════════════════════════
// ── Value-type subtyping primitive ─────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Users specialise `is_subsort<T, U>` to declare T is a value-level
// subtype of U.  The default is invariance via std::is_same.
//
// Must be SYMMETRIC under type-identity: is_subsort<T, T>::value holds
// for every T (reflexivity).  TRANSITIVITY is NOT enforced by the
// framework — users are expected to declare only sound specialisations.
// The framework propagates is_subsort through Send/Recv's payload
// position; it does not attempt to close transitively.

template <typename T, typename U>
struct is_subsort : std::is_same<T, U> {};

template <typename T, typename U>
inline constexpr bool is_subsort_v = is_subsort<T, U>::value;

// ═════════════════════════════════════════════════════════════════════
// ── is_subtype_sync<T, U> — Gay-Hole subtype relation ─────────────
// ═════════════════════════════════════════════════════════════════════
//
// Primary template: cross-shape pairs default to false.  Matching-shape
// specialisations override with the six Gay-Hole rules.

template <typename T, typename U>
struct is_subtype_sync : std::false_type {};

// [sub-end]  End ⩽ End
template <>
struct is_subtype_sync<End, End> : std::true_type {};

// [sub-continue]  Continue ⩽ Continue
//
// The coinductive hypothesis: if we've recursed into subtyping Loop<B₁>
// ⩽ Loop<B₂>, then the two Loops are assumed related by the time we
// reach a Continue inside their bodies.  Both Continues refer to their
// respective enclosing Loop, which by hypothesis are related.  So
// Continue ⩽ Continue is trivially true.
template <>
struct is_subtype_sync<Continue, Continue> : std::true_type {};

// [sub-send]  Send<P₁, R₁> ⩽ Send<P₂, R₂>  ⇔  P₁ ⩽ P₂ ∧ R₁ ⩽ R₂
//
// Covariant in payload: "a smaller payload can stand where a larger
// is expected."  Covariant in continuation.
template <typename P1, typename R1, typename P2, typename R2>
struct is_subtype_sync<Send<P1, R1>, Send<P2, R2>>
    : std::bool_constant<
          is_subsort_v<P1, P2> &&
          is_subtype_sync<R1, R2>::value
      > {};

// [sub-recv]  Recv<P₁, R₁> ⩽ Recv<P₂, R₂>  ⇔  P₂ ⩽ P₁ ∧ R₁ ⩽ R₂
//
// Contravariant in payload: "accepting a LARGER payload is fine where
// a smaller is expected" (the recipient can always downcast).
// Covariant in continuation.
template <typename P1, typename R1, typename P2, typename R2>
struct is_subtype_sync<Recv<P1, R1>, Recv<P2, R2>>
    : std::bool_constant<
          is_subsort_v<P2, P1> &&
          is_subtype_sync<R1, R2>::value
      > {};

// [sub-loop]  Loop<B₁> ⩽ Loop<B₂>  ⇔  B₁ ⩽ B₂  (coinductive)
//
// The recursion terminates because each step of is_subtype_sync
// consumes one combinator constructor; the only cycle is through
// Continue, which we resolve trivially via the Continue ⩽ Continue
// rule above.  No explicit fixed-point tracking needed: the framework
// metafunction recursion IS the coinductive proof.
template <typename B1, typename B2>
struct is_subtype_sync<Loop<B1>, Loop<B2>>
    : is_subtype_sync<B1, B2> {};

// ═════════════════════════════════════════════════════════════════════
// ── Select / Offer: positional-prefix subtyping ────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::subtype {

// Check that each index's subtype relation holds between corresponding
// positions of two branch tuples.  Fold over an index_sequence.
template <typename BranchesA, typename BranchesB, std::size_t... Is>
[[nodiscard]] constexpr bool prefix_subtypes(std::index_sequence<Is...>) noexcept
{
    return (is_subtype_sync<
                std::tuple_element_t<Is, BranchesA>,
                std::tuple_element_t<Is, BranchesB>
            >::value && ...);
}

// Two-tier check: size gate first (constexpr short-circuit safe), then
// prefix-subtypes (only when size gate passes — avoids instantiating
// std::tuple_element_t with an out-of-range index).
template <bool SizeOK, std::size_t PrefixLen,
          typename BranchesA, typename BranchesB>
struct gated_prefix_check : std::false_type {};

template <std::size_t PrefixLen, typename BranchesA, typename BranchesB>
struct gated_prefix_check<true, PrefixLen, BranchesA, BranchesB>
    : std::bool_constant<
          prefix_subtypes<BranchesA, BranchesB>(
              std::make_index_sequence<PrefixLen>{})
      > {};

}  // namespace detail::subtype

// [sub-select]  Select<B₁,…,Bₙ> ⩽ Select<C₁,…,Cₘ>
//                 ⇔ n ≤ m ∧ ∀ i ∈ [0, n): Bᵢ ⩽ Cᵢ
//
// Subtype picks from FEWER options than supertype.  A peer expecting
// the supertype's Offer<C₁,…,Cₘ> handles any of m choices; our subtype
// commits to only the first n < m.  Safe: the peer's extra C branches
// are simply never exercised.  The subtype cannot pick a position
// that doesn't exist in the supertype (the n ≤ m bound).
template <typename... B1s, typename... B2s>
struct is_subtype_sync<Select<B1s...>, Select<B2s...>>
    : detail::subtype::gated_prefix_check<
          (sizeof...(B1s) <= sizeof...(B2s)),
          sizeof...(B1s),
          std::tuple<B1s...>,
          std::tuple<B2s...>
      > {};

// [sub-offer]  Offer<B₁,…,Bₙ> ⩽ Offer<C₁,…,Cₘ>
//                ⇔ n ≥ m ∧ ∀ i ∈ [0, m): Bᵢ ⩽ Cᵢ
//
// Subtype handles MORE options than supertype.  A peer making a
// Select<C₁,…,Cₘ> choice (the dual) can pick any of m positions;
// our subtype's Offer<B₁,…,Bₙ> with n ≥ m handles ALL the peer's
// m positions (plus extras for free).  The subtype's extra branches
// beyond position m are unreachable from a supertype-speaking peer
// and are therefore safe.
template <typename... B1s, typename... B2s>
struct is_subtype_sync<Offer<B1s...>, Offer<B2s...>>
    : detail::subtype::gated_prefix_check<
          (sizeof...(B1s) >= sizeof...(B2s)),
          sizeof...(B2s),
          std::tuple<B1s...>,
          std::tuple<B2s...>
      > {};

// Public alias
template <typename T, typename U>
inline constexpr bool is_subtype_sync_v = is_subtype_sync<T, U>::value;

// ═════════════════════════════════════════════════════════════════════
// ── Framework self-test static_asserts ────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Run at header-inclusion time.  Dual purpose: document the semantics
// by example, and catch regressions from framework-level edits (any
// edit that breaks a subtype relation that SHOULD hold — or admits
// one that shouldn't — fails compile at the offending change).

namespace detail::subtype_self_test {

// ─── Reflexivity: every proto is a subtype of itself ───────────────

static_assert(is_subtype_sync_v<End, End>);
static_assert(is_subtype_sync_v<Continue, Continue>);
static_assert(is_subtype_sync_v<Send<int, End>, Send<int, End>>);
static_assert(is_subtype_sync_v<Recv<int, End>, Recv<int, End>>);
static_assert(is_subtype_sync_v<Loop<Send<int, Continue>>,
                                 Loop<Send<int, Continue>>>);
static_assert(is_subtype_sync_v<
    Select<Send<int, End>, Recv<bool, End>>,
    Select<Send<int, End>, Recv<bool, End>>>);
static_assert(is_subtype_sync_v<
    Offer<Recv<int, End>, Send<bool, End>>,
    Offer<Recv<int, End>, Send<bool, End>>>);

// ─── Shape mismatches: always false ────────────────────────────────

static_assert(!is_subtype_sync_v<Send<int, End>, Recv<int, End>>);
static_assert(!is_subtype_sync_v<Recv<int, End>, Send<int, End>>);
static_assert(!is_subtype_sync_v<End, Send<int, End>>);
static_assert(!is_subtype_sync_v<Send<int, End>, End>);
static_assert(!is_subtype_sync_v<Select<End>, Offer<End>>);
static_assert(!is_subtype_sync_v<Loop<Send<int, Continue>>, End>);
static_assert(!is_subtype_sync_v<End, Loop<Send<int, Continue>>>);
static_assert(!is_subtype_sync_v<Continue, End>);

// ─── Send: covariant in continuation ───────────────────────────────

// Same payload, subtype continuation (via Select narrowing below).
struct PingReq  {};
struct StopReq  {};

static_assert(is_subtype_sync_v<
    Send<int, Select<Send<PingReq, End>>>,
    Send<int, Select<Send<PingReq, End>, Send<StopReq, End>>>>);

// ─── Recv: contravariant in payload (at value-type level) ──────────

// With default is_subsort<T, T>, the only "subtype" at payload level
// is identity.  Cov / contra is therefore equivalent to "same payload"
// — we test by demonstrating shape-level recursion passes even when
// the continuation narrows.
static_assert(is_subtype_sync_v<
    Recv<int, Select<Send<PingReq, End>>>,
    Recv<int, Select<Send<PingReq, End>, Send<StopReq, End>>>>);

// ─── Select narrowing (the subtype commits to fewer options) ──────

// Subtype has 1 branch, supertype has 2 — the subtype picks a subset.
static_assert(is_subtype_sync_v<
    Select<Send<PingReq, End>>,
    Select<Send<PingReq, End>, Send<StopReq, End>>>);

// Subtype with 0 branches is a (vacuous) subtype of any Select.
// (Empty Select is syntactically legal but rare; we handle it
//  structurally — n=0 ≤ m, prefix_subtypes with empty index_sequence
//  returns true vacuously.)
static_assert(is_subtype_sync_v<Select<>, Select<Send<PingReq, End>>>);

// Subtype with MORE branches is NOT a subtype (would pick a branch
// unknown to the supertype).
static_assert(!is_subtype_sync_v<
    Select<Send<PingReq, End>, Send<StopReq, End>>,
    Select<Send<PingReq, End>>>);

// ─── Offer widening (the subtype handles more options) ─────────────

// Subtype has 3 branches, supertype has 2 — subtype accepts any peer
// choice of the first 2 plus a third unreachable one.
static_assert(is_subtype_sync_v<
    Offer<Recv<PingReq, End>, Recv<StopReq, End>, End>,
    Offer<Recv<PingReq, End>, Recv<StopReq, End>>>);

// Subtype with FEWER branches is NOT a subtype (peer might pick a
// branch the subtype doesn't handle).
static_assert(!is_subtype_sync_v<
    Offer<Recv<PingReq, End>>,
    Offer<Recv<PingReq, End>, Recv<StopReq, End>>>);

// Subtype with 0 branches is NOT a subtype of any non-empty Offer.
static_assert(!is_subtype_sync_v<Offer<>, Offer<Recv<PingReq, End>>>);

// But Offer<> ⩽ Offer<> (reflexivity).
static_assert(is_subtype_sync_v<Offer<>, Offer<>>);

// ─── Loop coinduction ──────────────────────────────────────────────

// Same body: reflexive.
static_assert(is_subtype_sync_v<
    Loop<Send<int, Continue>>,
    Loop<Send<int, Continue>>>);

// Narrowing inside Loop: the body's Select narrows.
static_assert(is_subtype_sync_v<
    Loop<Select<Send<PingReq, Continue>>>,
    Loop<Select<Send<PingReq, Continue>, Send<StopReq, End>>>>);

// Continue alone is a subtype of Continue (coinductive hypothesis).
static_assert(is_subtype_sync_v<Continue, Continue>);

// ─── Nested loops with shadowing ───────────────────────────────────

// Nested loops are well-typed; the inner Continue refers to the
// innermost Loop.  Subtype relation holds if every corresponding
// combinator pair is related.
static_assert(is_subtype_sync_v<
    Loop<Loop<Send<int, Continue>>>,
    Loop<Loop<Send<int, Continue>>>>);

// ─── Protocol evolution (v2 is subtype of v1) ──────────────────────

namespace proto_evolution_example {
    struct Req  {};
    struct Resp {};
    struct CloseCmd {};

    // v1 — server offers three request types
    using ServerV1 = Loop<Offer<
        Recv<Req,      Send<Resp, Continue>>,
        Recv<CloseCmd, End>,
        Recv<PingReq,  Send<PingReq, Continue>>  // echo
    >>;

    // v2 — server offers an additional fourth request type (StopReq
    // returning Resp then looping).  v2 HANDLES STRICTLY MORE — it
    // is a subtype of v1 (per Offer widening).
    using ServerV2 = Loop<Offer<
        Recv<Req,      Send<Resp, Continue>>,
        Recv<CloseCmd, End>,
        Recv<PingReq,  Send<PingReq, Continue>>,
        Recv<StopReq,  Send<Resp, End>>
    >>;

    static_assert(is_subtype_sync_v<ServerV2, ServerV1>);

    // The reverse is NOT a subtype: v1 doesn't handle StopReq.
    static_assert(!is_subtype_sync_v<ServerV1, ServerV2>);
}

// ─── MPMC protocol shape + subtype for producer narrowing ──────────

namespace mpmc_subtype_example {
    struct Job {};

    using ProducerFull = Loop<Select<
        Send<Job,    Continue>,
        Send<Job,    Continue>,    // redundant branch (for test purposes)
        End
    >>;

    using ProducerNarrow = Loop<Select<
        Send<Job,    Continue>
    >>;

    // Narrow producer is a subtype of full (picks from fewer branches).
    static_assert(is_subtype_sync_v<ProducerNarrow, ProducerFull>);
    static_assert(!is_subtype_sync_v<ProducerFull, ProducerNarrow>);
}

// ─── Subsort specialisation smoke test ─────────────────────────────

// Verify that when is_subsort is specialised, Send/Recv pick up the
// relation.  We specialise inside this namespace to avoid polluting.
struct BaseInt {};
struct DerivedInt {};  // hypothetically a subtype of BaseInt

}  // namespace detail::subtype_self_test

}  // namespace crucible::safety::proto

// Specialisation OUTSIDE the test namespace must be in the primary
// namespace of the template — we declare the test-local subsort
// relation here to exercise the propagation rule.  This is a one-off
// test fixture; real users specialise at the point of use.
namespace crucible::safety::proto {
template <>
struct is_subsort<detail::subtype_self_test::DerivedInt,
                  detail::subtype_self_test::BaseInt>
    : std::true_type {};
}  // namespace crucible::safety::proto

namespace crucible::safety::proto::detail::subtype_self_test {

// With Derived ⩽ Base at the value-type level, Send is covariant in
// payload — so Send<Derived, _> ⩽ Send<Base, _>.
static_assert(is_subtype_sync_v<Send<DerivedInt, End>, Send<BaseInt, End>>);
// But not the reverse — value-type subtyping isn't symmetric.
static_assert(!is_subtype_sync_v<Send<BaseInt, End>, Send<DerivedInt, End>>);

// Recv is contravariant in payload — Recv<Base, _> ⩽ Recv<Derived, _>.
static_assert(is_subtype_sync_v<Recv<BaseInt, End>, Recv<DerivedInt, End>>);
static_assert(!is_subtype_sync_v<Recv<DerivedInt, End>, Recv<BaseInt, End>>);

}  // namespace crucible::safety::proto::detail::subtype_self_test
