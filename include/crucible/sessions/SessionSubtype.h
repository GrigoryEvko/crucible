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
// GAPS-069 adds `sessions/SessionGrade.h`, which projects a protocol
// into a compile-time ProductLattice tuple over Vendor / NumericalTier
// / CipherTier / CrashClass payload grades.  This header intentionally
// does NOT consult that tuple yet: GAPS-070 wires it in as an
// orthogonal filter after the structural Gay-Hole rule succeeds.
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

#include <crucible/sessions/Session.h>

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

// ─── Subsort transitivity is the USER'S responsibility ─────────────
//
// The framework propagates is_subsort through Send/Recv's payload
// position but does NOT close it transitively.  If a user declares
// is_subsort<A, B> and is_subsort<B, C> but NOT is_subsort<A, C>,
// then:
//
//     is_subtype_sync_v<Send<A, End>, Send<B, End>> = true
//     is_subtype_sync_v<Send<B, End>, Send<C, End>> = true
//     is_subtype_sync_v<Send<A, End>, Send<C, End>> = false  (*)
//
// This is a HOLE in the derived relation, but closing it automatically
// would require transitive-closure machinery whose compile-time cost
// is proportional to the square of the user's subsort specialisations.
// We push the responsibility to the user: declare every direct subsort
// edge the user wants; transitivity is the user's contract.
//
// If your use case demands transitive closure, prefer a SINGLE CANONICAL
// subsort per payload class (e.g., every integer subtype specialises to
// the canonical `IntegerBase`) rather than a chain of incremental edges.

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

// [sub-vendor]  VendorPinned<V1, P1> ⩽ VendorPinned<V2, P2>
// iff V1 satisfies V2 under VendorLattice and P1 ⩽ P2.  Direction
// matches safety::Vendor::satisfies<Required>: a Portable protocol
// can stand in for an NV-required one, but an NV-pinned protocol
// cannot stand in for an AMD or Portable-required protocol.
template <VendorBackend V1, typename P1, VendorBackend V2, typename P2>
struct is_subtype_sync<VendorPinned<V1, P1>, VendorPinned<V2, P2>>
    : std::bool_constant<
          V1 != VendorBackend::None &&
          V2 != VendorBackend::None &&
          VendorLattice::leq(V2, V1) &&
          is_subtype_sync<P1, P2>::value
      > {};

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
// ── Ergonomic surface ──────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// Concept form — use in function templates to require a subtype
// relation at the call site:
//
//   template <typename ProtoV2>
//       requires SubtypeSync<ProtoV2, CanonicalProto>
//   auto accept(SessionHandle<ProtoV2, Resource>);
template <typename T, typename U>
concept SubtypeSync = is_subtype_sync_v<T, U>;

// Assertion helper — one-liner at every place a subtype relation must
// hold.  Named with the explicit "sync" suffix so when async subtyping
// (SEPLOG-I6) ships, `assert_subtype_async` lives alongside it.
//
// Use at integration boundaries — e.g., per-Vessel adapter declaration:
//
//   assert_subtype_sync<PyTorchVesselProto, FrontendCanon>();
//
// Fires a compile error pointing AT THE CALL SITE (not deep inside a
// template instantiation) when the relation does not hold.  The
// diagnostic names T and U via the template-instantiation context.
template <typename T, typename U>
consteval void assert_subtype_sync() noexcept {
    static_assert(is_subtype_sync_v<T, U>,
        "crucible::session::diagnostic [SubtypeMismatch]: "
        "assert_subtype_sync: T is not a synchronous subtype of U.  "
        "The six Gay-Hole rules are documented at the top of "
        "SessionSubtype.h.  Common causes: shape mismatch "
        "(Send vs Recv, Select vs Offer); too many/too few branches "
        "(subtype has more Select branches than supertype, or fewer "
        "Offer branches); payload types not related via is_subsort "
        "specialisation.  Check the template-instantiation context "
        "for the failing T and U.");
}

template <typename T, typename U>
consteval void assert_vendor_subtype_sync() noexcept {
    static_assert(is_subtype_sync_v<T, U>,
        "crucible::session::diagnostic [VendorCtx_Mismatch]: "
        "assert_vendor_subtype_sync: T is not a vendor-compatible "
        "synchronous subtype of U.  VendorPinned<V1, P1> may stand "
        "where VendorPinned<V2, P2> is expected only when "
        "VendorLattice::leq(V2, V1) holds and P1 is a synchronous "
        "subtype of P2.  Distinct vendor-specific protocols such as "
        "NV and AMD are intentionally incomparable; use "
        "VendorPinned<Portable, P> only for genuinely cross-vendor "
        "protocols.");
}

// ─── Protocol equivalence ──────────────────────────────────────────
//
// Two protocols are EQUIVALENT when each is a subtype of the other —
// bidirectional subtyping.  Equivalent protocols describe the same
// set of runtime traces; they're interchangeable in every context.
// Useful for:
//
//   * proving a pattern-library alias expands to a hand-written type
//     (RequestResponse<Req, Resp> ≡ Loop<Send<Req, Recv<Resp, Continue>>>)
//   * witnessing protocol refactor preserved meaning
//   * proving two independently-derived protocol definitions coincide

template <typename T, typename U>
inline constexpr bool equivalent_sync_v =
    is_subtype_sync_v<T, U> && is_subtype_sync_v<U, T>;

template <typename T, typename U>
concept EquivalentSync = equivalent_sync_v<T, U>;

// ─── Strict (proper) subtype relation ──────────────────────────────
//
// T is a STRICT subtype of U when T ⩽ U holds but U ⩽ T does not —
// i.e., T is a GENUINE refinement of U, not merely an equivalent
// restatement.  Useful for catching non-refinements: a proposed
// protocol evolution that claimed to narrow a Select (or widen an
// Offer) but in fact produced an equivalent type will yield
// is_strict_subtype_sync_v = false, flagging that no actual
// refinement happened.
//
// Strict subtyping is:
//   * irreflexive     (T </ T, because T ⩽ T is always bidirectional)
//   * antisymmetric   (T < U and U < T cannot both hold)
//   * transitive      (T < U and U < V ⇒ T < V)
//
// i.e., a strict partial order on session types.  Use when you want to
// REJECT equivalent protocols; prefer plain is_subtype_sync_v / the
// EquivalentSync concept when equivalence is an acceptable outcome.

template <typename T, typename U>
inline constexpr bool is_strict_subtype_sync_v =
    is_subtype_sync_v<T, U> && !is_subtype_sync_v<U, T>;

template <typename T, typename U>
concept StrictSubtypeSync = is_strict_subtype_sync_v<T, U>;

// ─── Subtype chain ──────────────────────────────────────────────────
//
// Verify a chain of subtyping relations: T_1 ⩽ T_2 ⩽ ... ⩽ T_n.
// Useful for validating a protocol-evolution ladder (v1 → v2 → v3 → v4
// is monotone).  By transitivity, T_1 ⩽ T_n follows.
//
//   static_assert(subtype_chain_v<ProtoV1, ProtoV2, ProtoV3>);

namespace detail::subtype {

template <typename First, typename... Rest>
struct subtype_chain_impl : std::true_type {};

template <typename A, typename B, typename... Rest>
struct subtype_chain_impl<A, B, Rest...>
    : std::bool_constant<
          is_subtype_sync_v<A, B> &&
          subtype_chain_impl<B, Rest...>::value
      > {};

}  // namespace detail::subtype

template <typename... Ts>
inline constexpr bool subtype_chain_v =
    detail::subtype::subtype_chain_impl<Ts...>::value;

// ─── Client / server compatibility ──────────────────────────────────
//
// Common FFI / cross-process question: "is this CLIENT's protocol
// compatible with that SERVER's protocol?"  The answer: the client's
// protocol must be a subtype of the DUAL of the server's protocol.
// (Server offers dual(ServerProto); client needs a sub-proto of that
// to be a safe substitute.)

template <typename ClientProto, typename ServerProto>
concept CompatibleClient =
    is_subtype_sync_v<ClientProto, dual_of_t<ServerProto>>;

template <typename ServerProto, typename ClientProto>
concept CompatibleServer =
    is_subtype_sync_v<ServerProto, dual_of_t<ClientProto>>;

// ─── Protocol-evolution helper ──────────────────────────────────────
//
// Named intent-revealing wrapper around the subtype check: "is
// NewProto a safe refinement of OldProto?"  Intended for use at
// protocol-version boundaries:
//
//   check_protocol_evolution<CipherProtoV1, CipherProtoV2>();
//
// Semantically identical to assert_subtype_sync<NewProto, OldProto>();
// the name signals intent to readers and reviewers.

template <typename OldProto, typename NewProto>
consteval void check_protocol_evolution() noexcept {
    static_assert(is_subtype_sync_v<NewProto, OldProto>,
        "crucible::session::diagnostic [SubtypeMismatch]: "
        "check_protocol_evolution: NewProto is not a safe refinement "
        "of OldProto.  A valid refinement may: narrow a Select (pick "
        "fewer branches), widen an Offer (handle more branches), or "
        "restrict a payload type via is_subsort specialisation.  It "
        "may NOT: add a Select branch, remove an Offer branch, change "
        "Send<->Recv, or swap Select<->Offer.");
}

// ─── Equivalence + compatibility assertion helpers ────────────────
//
// One-line consteval assertions for call-site use.  Each emits its
// diagnostic at the assertion site, not deep inside a metafunction
// instantiation — cuts typical failure diagnostic noise from ~2K
// lines to ~3 lines of actionable message.

template <typename T, typename U>
consteval void assert_equivalent_sync() noexcept {
    static_assert(equivalent_sync_v<T, U>,
        "crucible::session::diagnostic [SubtypeMismatch]: "
        "assert_equivalent_sync: T and U are not synchronously "
        "equivalent (not bidirectional subtypes).  Both "
        "is_subtype_sync_v<T, U> and is_subtype_sync_v<U, T> must hold. "
        "Common causes: asymmetric Select/Offer branch counts; "
        "differing payload types; mismatched Loop structure.  See the "
        "six Gay-Hole rules at the top of SessionSubtype.h for the "
        "positive direction of each.");
}

template <typename ClientProto, typename ServerProto>
consteval void assert_compatible_client() noexcept {
    static_assert(CompatibleClient<ClientProto, ServerProto>,
        "crucible::session::diagnostic [SubtypeMismatch]: "
        "assert_compatible_client: ClientProto is not a synchronous "
        "subtype of dual(ServerProto).  A client may safely talk to a "
        "server only when the client's protocol is a subtype of the "
        "server's DUAL (the server offers dual(ServerProto); the "
        "client must be a sub-protocol of that).  Common causes: "
        "forgot to dualise; both written from the same perspective "
        "(e.g., both send-first); mismatched payload types; client's "
        "Select picks branches the server's Offer does not provide.");
}

template <typename ServerProto, typename ClientProto>
consteval void assert_compatible_server() noexcept {
    static_assert(CompatibleServer<ServerProto, ClientProto>,
        "crucible::session::diagnostic [SubtypeMismatch]: "
        "assert_compatible_server: ServerProto is not a synchronous "
        "subtype of dual(ClientProto).  Symmetric to "
        "assert_compatible_client — see its diagnostic for the "
        "structural rule.  Typically ServerProto = dual(ClientProto) "
        "holds and this assertion is trivially true; when it fails, "
        "one side has been refactored in a way that breaks the "
        "symmetric sub-protocol relation.");
}

// ═════════════════════════════════════════════════════════════════════
// ── Framework self-test static_asserts ────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Run at header-inclusion time.  Dual purpose: document the semantics
// by example, and catch regressions from framework-level edits (any
// edit that breaks a subtype relation that SHOULD hold — or admits
// one that shouldn't — fails compile at the offending change).

#ifdef CRUCIBLE_SESSION_SELF_TESTS
namespace detail::subtype_self_test {

// ─── Reflexivity: every proto is a subtype of itself ───────────────

static_assert(is_subtype_sync_v<End, End>);
static_assert(is_subtype_sync_v<Continue, Continue>);
static_assert(is_subtype_sync_v<Send<int, End>, Send<int, End>>);
static_assert(is_subtype_sync_v<Recv<int, End>, Recv<int, End>>);
static_assert(is_subtype_sync_v<Loop<Send<int, Continue>>,
                                 Loop<Send<int, Continue>>>);
using NvSendInt = VendorPinned<VendorBackend::NV, Send<int, End>>;
using AmdSendInt = VendorPinned<VendorBackend::AMD, Send<int, End>>;
using PortableSendInt =
    VendorPinned<VendorBackend::Portable, Send<int, End>>;
static_assert(is_subtype_sync_v<NvSendInt, NvSendInt>);
static_assert(is_subtype_sync_v<PortableSendInt, NvSendInt>);
static_assert(!is_subtype_sync_v<NvSendInt, AmdSendInt>);
static_assert(!is_subtype_sync_v<AmdSendInt, NvSendInt>);
static_assert(!is_subtype_sync_v<NvSendInt, PortableSendInt>);
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
#endif  // CRUCIBLE_SESSION_SELF_TESTS

}  // namespace crucible::safety::proto

#ifdef CRUCIBLE_SESSION_SELF_TESTS
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

// ─── Dualization contravariance ────────────────────────────────────
//
// Load-bearing theorem (Gay-Hole 2005 Prop 3.4):
//
//     T ⩽ U    ⟹    dual(U) ⩽ dual(T)
//
// Subtyping is CONTRAVARIANT under dualization.  Intuition: if T is
// a safe substitute for U, then the peer's view of T is a safe
// substitute for the peer's view of U — and the peer's view IS the dual.

// Select narrowing: dual flips to Offer narrowing (note flipped direction).
using DS1 = Select<Send<PingReq, End>>;
using DS2 = Select<Send<PingReq, End>, Send<StopReq, End>>;
static_assert(is_subtype_sync_v<DS1, DS2>);
static_assert(is_subtype_sync_v<dual_of_t<DS2>, dual_of_t<DS1>>);
static_assert(!is_subtype_sync_v<DS2, DS1>);
static_assert(!is_subtype_sync_v<dual_of_t<DS1>, dual_of_t<DS2>>);

// Offer widening: dual flips to Select widening.
using DO1 = Offer<Recv<PingReq, End>, Recv<StopReq, End>>;
using DO2 = Offer<Recv<PingReq, End>>;
static_assert(is_subtype_sync_v<DO1, DO2>);
static_assert(is_subtype_sync_v<dual_of_t<DO2>, dual_of_t<DO1>>);

// Send-payload covariance dualizes to Recv-payload contravariance.
static_assert(is_subtype_sync_v<Send<DerivedInt, End>, Send<BaseInt, End>>);
static_assert(is_subtype_sync_v<dual_of_t<Send<BaseInt, End>>,
                                 dual_of_t<Send<DerivedInt, End>>>);

// Loop preserves dualization contravariance (recursive witness).
using DLoopS1 = Loop<Send<int, Select<Send<PingReq, Continue>>>>;
using DLoopS2 = Loop<Send<int, Select<Send<PingReq, Continue>,
                                       Send<StopReq, End>>>>;
static_assert(is_subtype_sync_v<DLoopS1, DLoopS2>);
static_assert(is_subtype_sync_v<dual_of_t<DLoopS2>, dual_of_t<DLoopS1>>);

// ─── Transitivity (structural) ─────────────────────────────────────
//
// T ⩽ U  ∧  U ⩽ V  ⟹  T ⩽ V  (for structural subtyping across
// combinators; subsort-transitivity is user's contract per above).

// Three-level Select narrowing chain
using TSelectT = Select<Send<PingReq, End>>;
using TSelectU = Select<Send<PingReq, End>, Send<StopReq, End>>;
using TSelectV = Select<Send<PingReq, End>, Send<StopReq, End>,
                         Recv<PingReq, End>>;
static_assert(is_subtype_sync_v<TSelectT, TSelectU>);
static_assert(is_subtype_sync_v<TSelectU, TSelectV>);
static_assert(is_subtype_sync_v<TSelectT, TSelectV>);  // transitivity

// Three-level Offer widening chain (reverse direction, more branches wins)
using TOfferW = Offer<Recv<PingReq, End>, Recv<StopReq, End>,
                       Send<PingReq, End>>;
using TOfferX = Offer<Recv<PingReq, End>, Recv<StopReq, End>>;
using TOfferY = Offer<Recv<PingReq, End>>;
static_assert(is_subtype_sync_v<TOfferW, TOfferX>);
static_assert(is_subtype_sync_v<TOfferX, TOfferY>);
static_assert(is_subtype_sync_v<TOfferW, TOfferY>);  // transitivity

// ─── Loop vs non-Loop edge cases ───────────────────────────────────
//
// Loop<X> and X are DIFFERENT protocols at the combinator level — no
// subtype relation in either direction.  Philosophically Loop<End> is
// a degenerate infinite-iterations-that-exits-immediately — distinct
// from End structurally.  Keep them unrelated; any blurring would
// require coercive rules we've chosen not to ship.
static_assert(!is_subtype_sync_v<Loop<End>, End>);
static_assert(!is_subtype_sync_v<End, Loop<End>>);
static_assert(!is_subtype_sync_v<Loop<Send<int, Continue>>, Send<int, End>>);
static_assert(!is_subtype_sync_v<Send<int, End>, Loop<Send<int, Continue>>>);

// ─── assert_subtype_sync helper compile-test ───────────────────────
//
// Exercise the helper in a consteval context so it actually forces
// the static_assert path at compile time.  Calling it with a valid
// relation should evaluate cleanly.
consteval bool check_assert_subtype_sync() {
    assert_subtype_sync<DS1, DS2>();  // valid; compiles
    return true;
}
static_assert(check_assert_subtype_sync());

// ─── Concept smoke test ────────────────────────────────────────────

template <typename T, typename U>
    requires SubtypeSync<T, U>
consteval bool requires_subtype() { return true; }

static_assert(requires_subtype<DS1, DS2>());

// ─── Strict subtype relation ───────────────────────────────────────

// Reflexive pairs are equivalent — never strict subtypes of themselves.
static_assert(!is_strict_subtype_sync_v<End, End>);
static_assert(!is_strict_subtype_sync_v<Send<int, End>, Send<int, End>>);
static_assert(!is_strict_subtype_sync_v<DS1, DS1>);

// Narrower Select is a STRICT subtype of a wider Select.
static_assert( is_strict_subtype_sync_v<DS1, DS2>);
static_assert(!is_strict_subtype_sync_v<DS2, DS1>);  // wider is not a subtype

// Wider Offer is a STRICT subtype of a narrower Offer.
static_assert( is_strict_subtype_sync_v<DO1, DO2>);
static_assert(!is_strict_subtype_sync_v<DO2, DO1>);

// Shape-mismatched pairs are never subtypes in either direction.
static_assert(!is_strict_subtype_sync_v<Send<int, End>, Recv<int, End>>);
static_assert(!is_strict_subtype_sync_v<Recv<int, End>, Send<int, End>>);

// Concept form compiles at namespace scope.
template <typename T, typename U>
    requires StrictSubtypeSync<T, U>
consteval bool requires_strict_subtype() { return true; }

static_assert(requires_strict_subtype<DS1, DS2>());

// ─── Equivalence ──────────────────────────────────────────────────

static_assert(equivalent_sync_v<End, End>);
static_assert(equivalent_sync_v<Send<int, End>, Send<int, End>>);
static_assert(equivalent_sync_v<DS1, DS1>);

// Strictly-related pairs are NOT equivalent.
static_assert(!equivalent_sync_v<DS1, DS2>);
static_assert(!equivalent_sync_v<DO1, DO2>);

// ─── Subtype chain ────────────────────────────────────────────────

static_assert( subtype_chain_v<TSelectT, TSelectU, TSelectV>);
static_assert( subtype_chain_v<TOfferW,  TOfferX,  TOfferY>);
static_assert( subtype_chain_v<End>);        // single element trivially chained
static_assert( subtype_chain_v<End, End>);   // reflexive 2-chain

// Wrong direction breaks the chain.
static_assert(!subtype_chain_v<TSelectU, TSelectT, TSelectV>);

// ─── Client / server compatibility ────────────────────────────────

namespace client_server_test {
    struct Query {};
    struct Reply {};
    using ReqRespClient = Loop<Send<Query, Recv<Reply, Continue>>>;
    using ReqRespServer = Loop<Recv<Query, Send<Reply, Continue>>>;

    // Server's dual is exactly the client's protocol (duality involution).
    static_assert(std::is_same_v<dual_of_t<ReqRespServer>, ReqRespClient>);

    // Standard compatibility: client is a subtype of server's dual.
    static_assert( CompatibleClient<ReqRespClient, ReqRespServer>);
    // And symmetrically for the server side.
    static_assert( CompatibleServer<ReqRespServer, ReqRespClient>);

    // Using the server's protocol where the client is expected is a
    // shape mismatch (Recv<Query, ...> at the head instead of
    // Send<Query, ...>) — rejected at the concept level.
    static_assert(!CompatibleClient<ReqRespServer, ReqRespServer>);
    static_assert(!CompatibleServer<ReqRespClient, ReqRespClient>);
}

// ─── Additional assertion-helper compile-tests ────────────────────

consteval bool check_additional_asserts() {
    assert_equivalent_sync<DS1, DS1>();
    assert_compatible_client<client_server_test::ReqRespClient,
                              client_server_test::ReqRespServer>();
    assert_compatible_server<client_server_test::ReqRespServer,
                              client_server_test::ReqRespClient>();
    return true;
}
static_assert(check_additional_asserts());

}  // namespace crucible::safety::proto::detail::subtype_self_test
#endif  // CRUCIBLE_SESSION_SELF_TESTS
