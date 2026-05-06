#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::safety::proto — session-type protocol DSL
//
// A compile-time type-level grammar for describing ARBITRARY
// communication protocols.  Given a protocol P built from the
// combinators below, the framework:
//
//   1. Computes its dual `dual_of_t<P>` (type-level, O(1) template
//      instantiations per node) so two endpoints at opposite ends
//      of a channel can statically verify their protocols agree.
//   2. Verifies the protocol is well-formed (no Continue outside a
//      Loop, no other stuck states) via `is_well_formed_v<P>`.
//   3. Composes `compose_t<P, Q>` sequentially by replacing P's End
//      with Q — protocol catenation as type-level cons.
//   4. Materialises `SessionHandle<P, Resource>` — a move-only
//      linear object whose methods are defined BY THE HEAD of P.
//      Calling the wrong method at the wrong protocol position is
//      a compile error, not a runtime check.
//
// ─── The combinator grammar ─────────────────────────────────────────
//
//     P ::= Send<T, P>            send a T, continue as P
//         | Recv<T, P>            receive a T, continue as P
//         | Select<P₁, …, Pₙ>     internal choice — we pick
//         | Offer<P₁, …, Pₙ>      external choice — peer picks
//         | Loop<P>               recursive fixpoint μX.P (X = Continue)
//         | Continue              loop-back; only valid inside a Loop
//         | End                   terminal
//
// This is the dyadic fragment of Honda 1993 / Honda-Vasconcelos-Kubo
// 1998.  The multiparty (MPST) extension of Honda-Yoshida-Carbone
// 2008 projects to per-participant local types of this shape; our
// tag-tree mechanism in safety/Permission.h (splits_into_pack) handles
// the projection side at the resource layer, keeping this header
// focused on the per-endpoint local type.
//
// ─── Duality, formally ─────────────────────────────────────────────
//
//     dual(End)             = End
//     dual(Continue)        = Continue
//     dual(Send<T, P>)      = Recv<T, dual(P)>
//     dual(Recv<T, P>)      = Send<T, dual(P)>
//     dual(Select<P₁,…,Pₙ>) = Offer<dual(P₁),…,dual(Pₙ)>
//     dual(Offer<P₁,…,Pₙ>)  = Select<dual(P₁),…,dual(Pₙ)>
//     dual(Loop<P>)         = Loop<dual(P)>
//
// Involution: dual(dual(P)) == P, verified as a framework-level
// static_assert for every protocol encountered by mint_channel.
//
// ─── Recursion semantics — how Loop / Continue actually work ────────
//
// The handle carries a LoopCtx template parameter (void by default).
// When SessionHandle<Loop<B>, Res> is requested, the factory unrolls
// ONE iteration: it constructs SessionHandle<B, Res, Loop<B>> instead.
// While stepping through B, if a continuation R is syntactically
// Continue, the step helper resolves it by substituting LoopCtx's
// body, re-entering the loop at the next iteration.  Nested Loops
// shadow the outer LoopCtx for the duration of the inner body — just
// like lexical scoping in ordinary programming languages.
//
// This gives bounded recursion depth at the TYPE level (≤ 1 unrolling
// per transition — O(1) template instantiations per operation) while
// supporting arbitrarily deep runtime iteration.  Classical session
// type systems (Gay-Hole 2005, Vasconcelos 2012) use equirecursion
// to dodge this problem; we use isorecursion with explicit unroll-on-
// entry.  Same soundness properties, simpler mechanics.
//
// ─── Protocol well-formedness ──────────────────────────────────────
//
// `is_well_formed_v<P>` checks at compile time that every Continue
// has an enclosing Loop (otherwise the protocol would be stuck: no
// state to loop back to).  All other combinators are well-formed
// unconditionally.  This check runs at every mint_channel and
// mint_session_handle site; ill-formed protocols fail compilation
// with a named static_assert message rather than degrading to a
// runtime fault.
//
// ─── Zero-cost dispatch ────────────────────────────────────────────
//
// `SessionHandle<P, Res, L>` holds exactly one Resource value.  Its
// sizeof equals sizeof(Resource) — the protocol P, dual computation,
// LoopCtx, and transition decisions all live in the type system and
// vanish at code-gen.  Every method is `gnu::hot` + always-inline-
// eligible; the emitted machine code is identical to hand-rolled
// per-step functions.  No virtual dispatch, no runtime state machine,
// no allocation.
//
// ─── The axis of Crucible integration ──────────────────────────────
//
// This header ships the *bare* framework — combinators + dispatch.
// The Permission-integrated variant `PermissionedSessionHandle<P, A…>`
// (task #333, SEPLOG-H2b) threads CSL Permission types through each
// step — consuming on Send of a permission-carrier value, producing
// on matching Recv, splitting at parallel composition, and verifying
// at End that the active permission set matches the declared exit
// set.  That layer lives in a sibling header to keep this one focused
// on the pure protocol DSL.
//
// MPMC protocol instantiation (task #328, SEPLOG-H2):
//
//     template <typename T>
//     using ProducerProto = proto::Loop<proto::Select<
//         proto::Send<T, proto::Continue>,
//         proto::End>>;
//
//     template <typename T>
//     using ConsumerProto = proto::dual_of_t<ProducerProto<T>>;
//
// Request/response server (inference session, Cipher warmup):
//
//     using Server = proto::Loop<proto::Recv<Req,
//                                proto::Send<Resp, proto::Continue>>>;
//
// Two-phase commit:
//
//     using Coord = proto::Send<Prepare,
//                   proto::Recv<Vote,
//                   proto::Select<
//                       proto::Send<Commit, proto::End>,
//                       proto::Send<Abort,  proto::End>>>>;
//
// Each of these is ONE type alias; the framework auto-generates the
// handle chain, verifies duality against the peer's protocol, and
// refuses wrong-order operations at compile time.
//
// ─── References ───────────────────────────────────────────────────
//
//   Honda, K. (1993).  "Types for Dyadic Interaction."  CONCUR.
//   Honda-Vasconcelos-Kubo (1998).  "Language Primitives and Type
//     Discipline for Structured Communication-Based Programming."
//     ESOP.
//   Gay-Hole (2005).  "Subtyping for Session Types in the Pi
//     Calculus."  Acta Informatica.
//   Honda-Yoshida-Carbone (2008).  "Multiparty Asynchronous Session
//     Types."  POPL.  (MPST)
//   Vasconcelos (2012).  "Fundamentals of Session Types."  Inf Comp.
//   Toninho-Caires-Pfenning (2013).  "Higher-Order Processes,
//     Functions, and Sessions: A Monadic Integration."  ESOP.
//     (The linear-session-typed lambda calculus that #333 ports.)
//   Scalas-Yoshida (2019).  "Less is More: Multiparty Session Types
//     Revisited."  POPL.  (OCaml reference implementation.)
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/algebra/lattices/VendorLattice.h>
#include <crucible/safety/Pinned.h>

#include <concepts>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <meta>
#include <optional>
#include <source_location>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto {

using ::crucible::algebra::lattices::VendorBackend;
using ::crucible::algebra::lattices::VendorLattice;

// ═════════════════════════════════════════════════════════════════
// ── Combinator types ────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════
//
// All combinators are empty classes — pure type markers.  The protocol
// lives entirely in the type system; no runtime state representation.

// Send a value of type T, then continue as Rest.
template <typename T, typename Rest>
struct Send {
    using message_type = T;
    using next         = Rest;
};

// Receive a value of type T, then continue as Rest.
template <typename T, typename Rest>
struct Recv {
    using message_type = T;
    using next         = Rest;
};

// Internal choice: THIS endpoint picks one of Branches...
template <typename... Branches>
struct Select {
    static constexpr std::size_t branch_count = sizeof...(Branches);
    using branches_tuple = std::tuple<Branches...>;
};

// ── Sender<Role> — MPST role annotation for Offer<> (#367) ──────
//
// In a 2-party session, `Offer<Branches...>` is unambiguous: the
// choice signal comes from "the peer," singular.  In MPST (Honda-
// Yoshida-Carbone 2008) the projected local protocol at role `q` can
// contain multiple Offers whose senders are different roles — `Offer
// from Alice` and `Offer from Bob` both appear in q's local type.
//
// Crash analysis (SessionCrash.h, #368) needs to know WHICH role
// sends an Offer to correctly answer "does this Offer need a
// Recv<Crash<PeerTag>, _> branch?"  The answer is YES only when
// PeerTag is the Offer's sender — an Offer from Bob doesn't need
// Alice's crash branch.  Without a sender annotation, the walker
// over-rejects.
//
// Sender<Role> is a phantom-type tag placed as the FIRST template
// argument of Offer<>:
//
//   Offer<Sender<Alice>, Recv<Msg, End>, Recv<Crash<Alice>, Recovery>>
//          ^ annotated: choice signal from Alice
//
// Partial specialization of Offer<Sender<R>, Branches...> carries
// `sender = R` and `branches_tuple = std::tuple<Branches...>` —
// the sender tag is NOT counted as a branch.  Downstream traits
// (has_crash_branch_for_peer, all_offers_have_crash_branch,
// compose, compose_at_branch, is_well_formed, is_empty_choice)
// specialize symmetrically so the tag is transparent to every
// operation except crash analysis.
//
// `Offer<Branches...>` without the Sender<> tag keeps its 2-party
// semantics; `sender` resolves to `AnonymousPeer`, a sentinel that
// matches any PeerTag in crash analysis (preserves existing
// behavior — no migration required).
template <typename Role>
struct Sender {
    using role_type = Role;
};

// Sentinel sender returned by `offer_sender_t` when an Offer<> is
// not annotated.  AnonymousPeer is NOT intended as a user-facing
// role — it exists so trait specializations can distinguish
// annotated from unannotated Offers at the type level.
struct AnonymousPeer {};

// External choice: the PEER picks one of Branches...
template <typename... Branches>
struct Offer {
    static constexpr std::size_t branch_count = sizeof...(Branches);
    using branches_tuple = std::tuple<Branches...>;
    // Unannotated Offer — sender matches any PeerTag in crash
    // analysis (the 2-party default).
    using sender = AnonymousPeer;
};

// External choice with explicit sender role (MPST, #367).  Class-
// template partial specialization on `Sender<Role>` as the first
// template argument; `branch_count` and `branches_tuple` reflect
// the REAL branches only, not the tag.
template <typename Role, typename... Branches>
struct Offer<Sender<Role>, Branches...> {
    static constexpr std::size_t branch_count = sizeof...(Branches);
    using branches_tuple = std::tuple<Branches...>;
    using sender = Role;
};

// Extract the declared sender role from an Offer type, or
// `AnonymousPeer` when unannotated.
template <typename OfferType>
struct offer_sender {
    using type = typename OfferType::sender;
};

template <typename OfferType>
using offer_sender_t = typename offer_sender<OfferType>::type;

// Recursive fixpoint μX.Body.  Continue refers to the nearest
// enclosing Loop.  Nested Loops shadow outer ones.
template <typename Body>
struct Loop {
    using body = Body;
};

// Loop-back marker.  Only valid inside a Loop — well-formedness
// check (is_well_formed_v) rejects any protocol with a free Continue.
struct Continue {};

// Terminal — protocol complete.  dual(End) = End.
struct End {};

// VendorPinned<V, P> — protocol-level vendor declaration for CNTP
// upper-layer sessions.  The wrapper is transparent to structural
// protocol operations but visible to mint/subtyping admission checks.
template <VendorBackend V, typename Proto>
struct VendorPinned : Proto {
    using protocol = Proto;
    static constexpr VendorBackend vendor_backend = V;
};

// ═════════════════════════════════════════════════════════════════
// ── Shape traits (is_*_v) ───────────────────────────────────────
// ═════════════════════════════════════════════════════════════════

template <typename P> struct is_send   : std::false_type {};
template <typename T, typename R>
struct is_send<Send<T, R>> : std::true_type {};
template <VendorBackend V, typename P>
struct is_send<VendorPinned<V, P>> : is_send<P> {};

template <typename P> struct is_recv   : std::false_type {};
template <typename T, typename R>
struct is_recv<Recv<T, R>> : std::true_type {};
template <VendorBackend V, typename P>
struct is_recv<VendorPinned<V, P>> : is_recv<P> {};

template <typename P> struct is_select : std::false_type {};
template <typename... Bs>
struct is_select<Select<Bs...>> : std::true_type {};
template <VendorBackend V, typename P>
struct is_select<VendorPinned<V, P>> : is_select<P> {};

template <typename P> struct is_offer  : std::false_type {};
template <typename... Bs>
struct is_offer<Offer<Bs...>> : std::true_type {};
template <VendorBackend V, typename P>
struct is_offer<VendorPinned<V, P>> : is_offer<P> {};

template <typename P> struct is_loop   : std::false_type {};
template <typename B>
struct is_loop<Loop<B>> : std::true_type {};
template <VendorBackend V, typename P>
struct is_loop<VendorPinned<V, P>> : is_loop<P> {};

template <typename P> struct is_end      : std::bool_constant<std::is_same_v<P, End>>      {};
template <VendorBackend V, typename P>
struct is_end<VendorPinned<V, P>> : is_end<P> {};

template <typename P> struct is_continue : std::bool_constant<std::is_same_v<P, Continue>> {};
template <VendorBackend V, typename P>
struct is_continue<VendorPinned<V, P>> : is_continue<P> {};

template <typename P> struct is_vendor_pinned : std::false_type {
    using protocol = P;
    static constexpr VendorBackend vendor_backend = VendorBackend::Portable;
};
template <VendorBackend V, typename P>
struct is_vendor_pinned<VendorPinned<V, P>> : std::true_type {
    using protocol = P;
    static constexpr VendorBackend vendor_backend = V;
};

template <typename P> inline constexpr bool is_send_v     = is_send<P>::value;
template <typename P> inline constexpr bool is_recv_v     = is_recv<P>::value;
template <typename P> inline constexpr bool is_select_v   = is_select<P>::value;
template <typename P> inline constexpr bool is_offer_v    = is_offer<P>::value;
template <typename P> inline constexpr bool is_loop_v     = is_loop<P>::value;
template <typename P> inline constexpr bool is_end_v      = is_end<P>::value;
template <typename P> inline constexpr bool is_continue_v = is_continue<P>::value;
template <typename P>
inline constexpr bool is_vendor_pinned_v = is_vendor_pinned<P>::value;
template <typename P>
inline constexpr VendorBackend protocol_vendor_v =
    is_vendor_pinned<P>::vendor_backend;
template <typename P>
using protocol_inner_t = typename is_vendor_pinned<P>::protocol;

// ─── is_empty_choice<P> — detects empty Select<> / Offer<> (#364) ──
//
// `Select<>` / `Offer<>` with zero branches are LEGITIMATE as type
// operands in subtyping theory — Gay-Hole 2005 gives them the minimum
// subtype role (an empty Select is a subtype of any larger Select;
// covariance on alternatives).  SessionSubtype.h's
// `is_subtype_sync_v<Select<>, Select<Send<X, End>>>` tests depend on
// this.
//
// But they are NOT runnable protocols.  `SessionHandle<Select<>>` has
// no branch to pick; `SessionHandle<Offer<>>` cannot receive a valid
// label from the peer.  Constructing a runnable handle on one would
// leave the protocol stuck at a dead-end state.
//
// `is_empty_choice_v<P>` detects exactly this shape.  Used by
// mint_session_handle's static_assert to reject handle instantiation
// at the runnable boundary while leaving the type-level trait
// machinery in SessionSubtype.h untouched.  Complements
// is_well_formed_v, which covers structural illegality (free
// Continue, Stop outside Crash context, etc.); empty choice is
// orthogonal — structurally valid at the type level, but unrunnable
// when reified as a handle.

template <typename P> struct is_empty_choice : std::false_type {};

// Specialise only on the two empty variants — Select<>/Offer<> with
// ANY branches are NOT empty, so the primary template's false base
// applies.
template <> struct is_empty_choice<Select<>> : std::true_type {};
template <> struct is_empty_choice<Offer<>>  : std::true_type {};

// Annotated-but-empty Offer (#367): `Offer<Sender<Role>>` declares a
// sender but no branches — the empty-choice rejection applies equally
// to the annotated form.  Without this specialization, the primary
// `is_empty_choice<P>::false_type` would leak through and the
// empty-choice guard would miss `Offer<Sender<Role>>`.
template <typename Role>
struct is_empty_choice<Offer<Sender<Role>>> : std::true_type {};

template <VendorBackend V, typename P>
struct is_empty_choice<VendorPinned<V, P>> : is_empty_choice<P> {};

template <typename P>
inline constexpr bool is_empty_choice_v = is_empty_choice<P>::value;

// A "protocol head" is anything other than Loop<_> (which is a
// pseudo-state that auto-unrolls at handle construction).
//
// The definition is negative — `!is_loop_v<P>` — rather than an
// explicit enumeration so that sibling headers which add combinators
// (e.g., SessionDelegate.h's Delegate/Accept, SessionCrash.h's Stop)
// are automatically admitted as valid handle positions without needing
// to edit this file.  Any future combinator that is NOT a Loop is a
// head.  Loop is the sole exception because its SessionHandle
// specialisation doesn't exist — the factory unrolls Loop one step
// at construction and positions the handle at the body instead.
template <typename P>
inline constexpr bool is_head_v = !is_loop_v<P>;

// ═════════════════════════════════════════════════════════════════
// ── dual_of<P> — type-level protocol inversion ──────────────────
// ═════════════════════════════════════════════════════════════════

template <typename P> struct dual_of;

template <> struct dual_of<End>      { using type = End; };
template <> struct dual_of<Continue> { using type = Continue; };

template <typename T, typename R>
struct dual_of<Send<T, R>> {
    using type = Recv<T, typename dual_of<R>::type>;
};

template <typename T, typename R>
struct dual_of<Recv<T, R>> {
    using type = Send<T, typename dual_of<R>::type>;
};

template <typename... Bs>
struct dual_of<Select<Bs...>> {
    using type = Offer<typename dual_of<Bs>::type...>;
};

template <typename... Bs>
struct dual_of<Offer<Bs...>> {
    using type = Select<typename dual_of<Bs>::type...>;
};

// Sender-annotated Offer (#367): dual drops the sender tag and
// produces a plain Select.  From the DUAL endpoint's perspective
// the sender of the original Offer is "us" (local role), so the
// annotation — useful for the Offer-side crash analysis — carries
// no meaning on the Select-side internal choice.
template <typename Role, typename... Bs>
struct dual_of<Offer<Sender<Role>, Bs...>> {
    using type = Select<typename dual_of<Bs>::type...>;
};

template <typename B>
struct dual_of<Loop<B>> {
    using type = Loop<typename dual_of<B>::type>;
};

template <VendorBackend V, typename P>
struct dual_of<VendorPinned<V, P>> {
    using type = VendorPinned<V, typename dual_of<P>::type>;
};

template <typename P>
using dual_of_t = typename dual_of<P>::type;

// ═════════════════════════════════════════════════════════════════
// ── is_dual_v / ensure_dual — endpoint-pair duality check (#431)
// ═════════════════════════════════════════════════════════════════
//
// Pairing two endpoint protocols on opposite ends of a channel must
// yield duals.  Without that, the framework's MAIN safety promise
// (dual endpoints can never deadlock) silently fails: the user
// observes runtime hangs, type confusion in the transport bytes, or
// segfaults when the channel runs out of message types it knows how
// to decode.
//
// `is_dual_v<P1, P2>` is the trait — `true` iff P1 == dual_of_t<P2>.
// (Equivalently, since `dual_of_t` is involutive on well-formed
// protocols, dual_of_t<P1> == P2 holds too.)
//
// `ensure_dual<P1, P2>()` is the consteval one-line check that fires
// the framework-controlled `[Dual_Mismatch]` diagnostic when the pair
// isn't dual.  The diagnostic carries both protocols' rendered names
// (via the protocol_name() infrastructure from #379) so the developer
// sees both "actual" and "expected" forms inline.
//
// Use ensure_dual at the establishment site of every channel pair —
// either client/server REST-style, producer/consumer SPSC, request/
// reply pipelines, or any two-endpoint composition.  The cost is a
// single static_assert at the type-pairing point; runtime cost is
// zero.
//
// Example:
//
//   using ClientProto = Send<Request, Recv<Response, End>>;
//   using ServerProto = Recv<Request, Send<Response, End>>;
//   ensure_dual<ClientProto, ServerProto>();   // OK — duals
//
//   using BadServer = Recv<Request, Recv<Response, End>>;  // forgot Send
//   ensure_dual<ClientProto, BadServer>();     // [Dual_Mismatch] fires
//
// Audit: grep "ensure_dual<"  →  every channel-establishment pair.

template <typename P1, typename P2>
inline constexpr bool is_dual_v = std::is_same_v<dual_of_t<P1>, P2>;

template <typename P1, typename P2>
consteval void ensure_dual() noexcept {
    static_assert(is_dual_v<P1, P2>,
        "crucible::session::diagnostic [Dual_Mismatch]: "
        "ensure_dual<P1, P2>(): the two endpoint protocols are NOT "
        "structural duals.  One side's Send must pair with the other "
        "side's Recv; one side's Select must pair with the other "
        "side's Offer; Loop pairs with Loop, End with End, Continue "
        "with Continue.  Inspect dual_of_t<P1> against P2 (they must "
        "be the same type).  Without this duality the framework's "
        "deadlock-freedom guarantee does NOT hold — runtime hangs and "
        "transport-byte misinterpretation become possible.  Common "
        "causes: (a) one side missing a Send/Recv step the other side "
        "has, (b) Select/Offer mismatch (one endpoint internal-choice, "
        "other endpoint should be external-choice), (c) branch types "
        "of a Select/Offer pair don't dualize element-wise.");
}

// Convenience inline-callable form for use at namespace scope (e.g.,
// at module init or within a function body):
//   inline static constexpr auto _dual_check = (ensure_dual<C, S>(), 0);
// The cleaner pattern is `static_assert(is_dual_v<C, S>, "...")` at
// the declaration of one of the two protocols, OR a single
// `ensure_dual<C, S>();` call in a consteval function near the
// channel factory.

// ═════════════════════════════════════════════════════════════════
// ── compose<P, Q> — sequential composition ──────────────────────
// ═════════════════════════════════════════════════════════════════
//
// Replace every End in P with Q, propagating through Send/Recv/
// Select/Offer/Loop recursively.  Continue stays as Continue — it's
// a LOOP marker, not a protocol-end marker, and its resolution is
// handled at stepping time by the LoopCtx mechanism, not at
// composition time.

template <typename P, typename Q> struct compose;

template <typename Q>
struct compose<End, Q> { using type = Q; };

template <typename Q>
struct compose<Continue, Q> { using type = Continue; };

template <typename T, typename R, typename Q>
struct compose<Send<T, R>, Q> {
    using type = Send<T, typename compose<R, Q>::type>;
};

template <typename T, typename R, typename Q>
struct compose<Recv<T, R>, Q> {
    using type = Recv<T, typename compose<R, Q>::type>;
};

template <typename... Bs, typename Q>
struct compose<Select<Bs...>, Q> {
    using type = Select<typename compose<Bs, Q>::type...>;
};

template <typename... Bs, typename Q>
struct compose<Offer<Bs...>, Q> {
    using type = Offer<typename compose<Bs, Q>::type...>;
};

// Sender-annotated Offer (#367): compose Q into each REAL branch and
// preserve the `Sender<Role>` tag at position 0.  Without this
// specialization the primary `compose<Offer<Bs...>, Q>` would
// compose Q into the Sender<Role> tag too, producing garbage.
template <typename Role, typename... Bs, typename Q>
struct compose<Offer<Sender<Role>, Bs...>, Q> {
    using type = Offer<Sender<Role>, typename compose<Bs, Q>::type...>;
};

template <typename B, typename Q>
struct compose<Loop<B>, Q> {
    using type = Loop<typename compose<B, Q>::type>;
};

template <VendorBackend V, typename P, typename Q>
struct compose<VendorPinned<V, P>, Q> {
    using type = VendorPinned<V, typename compose<P, Q>::type>;
};

template <typename P, typename Q>
using compose_t = typename compose<P, Q>::type;

// ═════════════════════════════════════════════════════════════════
// ── compose_at_branch<P, I, Q> — branch-asymmetric composition (#378)
// ═════════════════════════════════════════════════════════════════
//
// `compose_t<P, Q>` is UNIFORM — it replaces every End in P with Q
// across all branches.  But many real protocols are branch-ASYMMETRIC:
// one branch loops back, another terminates; one branch enters a sub-
// session, another reports an error and ends.  Hand-writing each
// branch's continuation defeats the point of composing patterns.
//
// `compose_at_branch_t<P, I, Q>` walks P's head until it finds the
// first Select<Bs...> or Offer<Bs...>, then replaces ONLY branch I's
// continuation (compose_t-style — every End within branch I becomes
// Q), leaving the other branches untouched.
//
// Pre-head Send<T, _> / Recv<T, _> nodes are passed through (the
// metafunction recurses into the continuation looking for the choice).
// Loop<B> is passed through and recursed into B.
//
// Errors:
//   * No reachable Select/Offer in P's spine        → [Branch_Compose_No_Choice]
//   * I out of range for the reached Select/Offer   → [Branch_Compose_Index_Out_Of_Range]
//
// Example:
//
//     using ServerOnce = Recv<Request, Select<
//         Send<Ok,    End>,        // branch 0: success path
//         Send<Error, End>>>;       // branch 1: error path
//
//     // Add a follow-up loop ONLY on the success branch:
//     using ServerLoop = compose_at_branch_t<
//         ServerOnce, /*branch=*/0,
//         Loop<Recv<Request, Continue>>>;
//
//     // Equivalent to manually:
//     //   Recv<Request, Select<Send<Ok, Loop<Recv<Request, Continue>>>,
//     //                        Send<Error, End>>>
//
// Audit:
//   grep "compose_at_branch_t<"   — every branch-asymmetric site

namespace detail {
// Replace branch_index in (Bs...) with compose_t<Bs[branch_index], Q>.
template <std::size_t BranchIndex, typename Q, typename... Bs>
struct rewrite_branches {
    template <std::size_t I>
    using at = std::conditional_t<
        I == BranchIndex,
        compose_t<std::tuple_element_t<I, std::tuple<Bs...>>, Q>,
        std::tuple_element_t<I, std::tuple<Bs...>>>;

    template <typename Idxs>
    struct unpack;

    template <std::size_t... Is>
    struct unpack<std::index_sequence<Is...>> {
        using as_select = Select<at<Is>...>;
        using as_offer  = Offer<at<Is>...>;
    };
};
}  // namespace detail

template <typename P, std::size_t I, typename Q>
struct compose_at_branch;

// Reaching the choice: Select<Bs...>.
template <typename... Bs, std::size_t I, typename Q>
struct compose_at_branch<Select<Bs...>, I, Q> {
    static_assert(I < sizeof...(Bs),
        "crucible::session::diagnostic [Branch_Compose_Index_Out_Of_Range]: "
        "compose_at_branch_t<Select<Bs...>, I, Q>: branch index I is "
        "out of range for the reached Select.  The Select has fewer "
        "branches than the index requested; verify I < sizeof...(Bs) "
        "at the call site (decltype-expose `branch_count` if needed).");
    using type = typename detail::rewrite_branches<I, Q, Bs...>::template
        unpack<std::make_index_sequence<sizeof...(Bs)>>::as_select;
};

// Reaching the choice: Offer<Bs...>.
template <typename... Bs, std::size_t I, typename Q>
struct compose_at_branch<Offer<Bs...>, I, Q> {
    static_assert(I < sizeof...(Bs),
        "crucible::session::diagnostic [Branch_Compose_Index_Out_Of_Range]: "
        "compose_at_branch_t<Offer<Bs...>, I, Q>: branch index I is "
        "out of range for the reached Offer.  The Offer has fewer "
        "branches than the index requested.");
    using type = typename detail::rewrite_branches<I, Q, Bs...>::template
        unpack<std::make_index_sequence<sizeof...(Bs)>>::as_offer;
};

// Sender-annotated Offer (#367): index I ranges over REAL branches
// (Bs...), not including the Sender<Role> tag.  Reuse the rewriter
// on Bs... alone and rebuild with the sender preserved at position 0.
template <typename Role, typename... Bs, std::size_t I, typename Q>
struct compose_at_branch<Offer<Sender<Role>, Bs...>, I, Q> {
    static_assert(I < sizeof...(Bs),
        "crucible::session::diagnostic [Branch_Compose_Index_Out_Of_Range]: "
        "compose_at_branch_t<Offer<Sender<Role>, Bs...>, I, Q>: branch "
        "index I is out of range for the reached sender-annotated "
        "Offer.  Branches are counted WITHOUT the Sender<Role> tag — "
        "an Offer<Sender<R>, B0, B1> has 2 branches, not 3.");

    template <std::size_t J>
    using at = std::conditional_t<
        J == I,
        compose_t<std::tuple_element_t<J, std::tuple<Bs...>>, Q>,
        std::tuple_element_t<J, std::tuple<Bs...>>>;

    template <typename Idxs> struct build;
    template <std::size_t... Js>
    struct build<std::index_sequence<Js...>> {
        using type = Offer<Sender<Role>, at<Js>...>;
    };

    using type = typename build<std::make_index_sequence<sizeof...(Bs)>>::type;
};

// Pass-through pre-head nodes: recurse into Send/Recv's continuation.
template <typename T, typename R, std::size_t I, typename Q>
struct compose_at_branch<Send<T, R>, I, Q> {
    using type = Send<T, typename compose_at_branch<R, I, Q>::type>;
};

template <typename T, typename R, std::size_t I, typename Q>
struct compose_at_branch<Recv<T, R>, I, Q> {
    using type = Recv<T, typename compose_at_branch<R, I, Q>::type>;
};

// Pass-through Loop<B>: recurse into the body.  Note: the loop body's
// own End is composed normally if the choice is below the Loop; if
// the loop body itself ends with End (no choice reachable), the
// no-choice diagnostic fires below.
template <typename B, std::size_t I, typename Q>
struct compose_at_branch<Loop<B>, I, Q> {
    using type = Loop<typename compose_at_branch<B, I, Q>::type>;
};

template <VendorBackend V, typename P, std::size_t I, typename Q>
struct compose_at_branch<VendorPinned<V, P>, I, Q> {
    using type = VendorPinned<V, typename compose_at_branch<P, I, Q>::type>;
};

// Reaching End / Continue / Stop / etc. WITHOUT having found a
// Select or Offer means the spine has no choice to compose at —
// named diagnostic.
template <std::size_t I, typename Q>
struct compose_at_branch<End, I, Q> {
    static_assert(sizeof(Q) == 0,
        "crucible::session::diagnostic [Branch_Compose_No_Choice]: "
        "compose_at_branch_t<P, I, Q>: walked P's spine to End without "
        "encountering a Select<Bs...> or Offer<Bs...> to compose at.  "
        "Branch-asymmetric composition requires P to contain a choice "
        "combinator at some position reachable from the head via "
        "Send/Recv/Loop pass-through.  If you intended UNIFORM "
        "composition (every End → Q), use the existing compose_t<P, Q> "
        "instead.");
};

template <std::size_t I, typename Q>
struct compose_at_branch<Continue, I, Q> {
    static_assert(sizeof(Q) == 0,
        "crucible::session::diagnostic [Branch_Compose_No_Choice]: "
        "compose_at_branch_t<P, I, Q>: walked P's spine to Continue "
        "without encountering a Select or Offer.  Continue is a loop-"
        "back marker, not a choice point; compose_at_branch is meant "
        "for choice combinators.");
};

template <typename P, std::size_t I, typename Q>
using compose_at_branch_t = typename compose_at_branch<P, I, Q>::type;

// ═════════════════════════════════════════════════════════════════
// ── is_well_formed<P, LoopCtx> — compile-time soundness check ───
// ═════════════════════════════════════════════════════════════════
//
// Verifies every Continue has an enclosing Loop.  Nothing else can go
// wrong at the combinator level — Send/Recv always have a continuation,
// Select/Offer always have at least one branch by type-system well-
// formedness, End/Loop are unconditionally OK.

template <typename P, typename LoopCtx = void>
struct is_well_formed;

template <typename LoopCtx>
struct is_well_formed<End, LoopCtx> : std::true_type {};

template <typename LoopCtx>
struct is_well_formed<Continue, LoopCtx>
    : std::bool_constant<!std::is_void_v<LoopCtx>> {};

template <typename T, typename R, typename LoopCtx>
struct is_well_formed<Send<T, R>, LoopCtx>
    : is_well_formed<R, LoopCtx> {};

template <typename T, typename R, typename LoopCtx>
struct is_well_formed<Recv<T, R>, LoopCtx>
    : is_well_formed<R, LoopCtx> {};

template <typename... Bs, typename LoopCtx>
struct is_well_formed<Select<Bs...>, LoopCtx>
    : std::bool_constant<(is_well_formed<Bs, LoopCtx>::value && ...)> {};

template <typename... Bs, typename LoopCtx>
struct is_well_formed<Offer<Bs...>, LoopCtx>
    : std::bool_constant<(is_well_formed<Bs, LoopCtx>::value && ...)> {};

// Sender-annotated Offer (#367): check only the REAL branches
// (Bs...); the Sender<Role> tag is a type-level annotation, not a
// runnable combinator, so it isn't a target for well-formedness.
template <typename Role, typename... Bs, typename LoopCtx>
struct is_well_formed<Offer<Sender<Role>, Bs...>, LoopCtx>
    : std::bool_constant<(is_well_formed<Bs, LoopCtx>::value && ...)> {};

template <typename B, typename LoopCtx>
struct is_well_formed<Loop<B>, LoopCtx>
    // Loop<B> introduces itself as the new LoopCtx for checking B.
    : is_well_formed<B, Loop<B>> {};

template <VendorBackend V, typename P, typename LoopCtx>
struct is_well_formed<VendorPinned<V, P>, LoopCtx>
    : is_well_formed<P, LoopCtx> {};

template <typename P>
inline constexpr bool is_well_formed_v = is_well_formed<P>::value;

// ═════════════════════════════════════════════════════════════════
// ── is_terminal_state<P> — handle-destruction safety trait ──────
// ═════════════════════════════════════════════════════════════════
//
// Classifies protocol positions where a SessionHandle may be safely
// destroyed without consuming it.  Used by SessionHandleBase's
// debug-time abandoned-protocol check below.
//
// Extensible:  SessionCrash.h specialises for Stop (adds it as a
// terminal state) by including a `template <> struct
// is_terminal_state<Stop> : std::true_type {};` below its Stop
// definition.  Users who ship their own terminal combinators do
// the same.

template <typename P>
struct is_terminal_state : std::bool_constant<std::is_same_v<P, End>> {};

template <VendorBackend V, typename P>
struct is_terminal_state<VendorPinned<V, P>> : is_terminal_state<P> {};

template <typename P>
inline constexpr bool is_terminal_state_v = is_terminal_state<P>::value;

// ═════════════════════════════════════════════════════════════════
// ── detail::consumed_tracker — conditional flag storage ─────────
// ═════════════════════════════════════════════════════════════════
//
// Stores the SessionHandle abandonment-check flag with TWO different
// shapes per build mode, controlled by NDEBUG:
//
//   * DEBUG:    holds a real `bool` recording whether the handle has
//               been consumed.  Drives the destructor check.
//   * RELEASE:  empty class — when combined with SessionHandleBase's
//               `[[no_unique_address]]` member, the base class becomes
//               empty, EBO collapses it in derived SessionHandle
//               classes, and the per-handle overhead drops to ZERO.
//
// Net effect:  in release, sizeof(SessionHandle<End, R>) == sizeof(R)
// — no per-handle overhead beyond the underlying Resource.  The
// framework's "zero-cost discipline" claim is verified at the type
// level (release-mode static_asserts at the bottom of this file).
//
// ABI note:  this is a deliberate debug/release ABI difference.
// SessionHandle's sizeof differs between modes.  Crucible's build
// discipline (single mode per binary; never link debug + release
// object files together) makes this safe.  Cross-mode linking would
// produce silent layout mismatches.

namespace detail {

#ifdef NDEBUG

class consumed_tracker {
public:
    constexpr consumed_tracker() noexcept = default;
    // Source-location-accepting ctor (#429 improvement C).  In RELEASE
    // builds we DROP the location on the floor — the destructor check
    // doesn't fire, so capturing the site is pointless overhead.  The
    // signature stays for ABI parity with the DEBUG branch so derived-
    // class ctors can pass a loc argument unconditionally.
    constexpr explicit consumed_tracker(std::source_location) noexcept {}
    constexpr void mark() noexcept {}
    constexpr bool was_marked() const noexcept { return true; }
    constexpr void move_from(consumed_tracker&) noexcept {}
    // RELEASE: no construction-site info to expose; render a sentinel.
    constexpr std::source_location construction_loc() const noexcept {
        return std::source_location{};
    }
};
static_assert(std::is_empty_v<consumed_tracker>,
    "Release-mode consumed_tracker must be std::is_empty_v for EBO.");

#else

class consumed_tracker {
    bool flag_ = false;
    // Source location captured at the handle's CONSTRUCTION SITE
    // (#429 improvement C).  Default-constructs to "unknown source"
    // for handles minted without an explicit loc argument (e.g.,
    // legacy code paths or programmatic construction).  Stored ONLY
    // in DEBUG builds — the release-mode tracker is empty and EBO-
    // collapses to zero bytes.
    std::source_location loc_{};
public:
    constexpr consumed_tracker() noexcept = default;
    constexpr explicit consumed_tracker(std::source_location loc) noexcept
        : loc_{loc} {}
    constexpr void mark() noexcept { flag_ = true; }
    constexpr bool was_marked() const noexcept { return flag_; }
    constexpr std::source_location construction_loc() const noexcept {
        return loc_;
    }

    // Self-safe move.  When &other == *this (reachable via aliasing
    // or chained moves like `h = std::move(h);`), the naive
    // implementation would store flag_ = flag_ then unconditionally
    // set flag_ = true — silently marking the handle consumed
    // regardless of its prior state and breaking the destructor's
    // abandonment-check invariant (#365).  The guard short-circuits
    // self-aliasing, leaving the tracker exactly as it was.
    constexpr void move_from(consumed_tracker& other) noexcept {
        if (this == &other) [[unlikely]] return;
        flag_       = other.flag_;
        loc_        = other.loc_;        // inherit source-location
        other.flag_ = true;
    }
};

#endif

// ─── type_name<T> — compile-time protocol-shape rendering (#379) ──
//
// Extracts a human-readable rendering of T from GCC's
// __PRETTY_FUNCTION__ string.  Used by SessionHandleBase's
// protocol_name() static accessor and by the abandonment-check
// destructor message — without this, the destructor's fprintf says
// only "SessionHandle abandoned" with no clue WHICH protocol leaked.
//
// Implementation is GCC-specific (relies on __PRETTY_FUNCTION__'s
// `[with T = ...; std::string_view = ...]` format documented in
// GCC's manual).  Crucible is GCC 16-only per code_guide.md §I, so
// no portability fallback is needed.
//
// The returned std::string_view points into the function template's
// constant data (program-lifetime); copying out is unnecessary.
//
// Example output for T = Loop<Send<int, Continue>>:
//
//   "crucible::safety::proto::Loop<crucible::safety::proto::Send<
//    int, crucible::safety::proto::Continue> >"
//
// (The verbose namespace prefix is preserved as-is — a future task
// could strip it for readability without breaking the API.)

template <typename T>
constexpr std::string_view pretty_function_raw_() noexcept {
    return std::string_view{__PRETTY_FUNCTION__};
}

template <typename T>
constexpr std::string_view type_name() noexcept {
    constexpr std::string_view raw    = pretty_function_raw_<T>();
    constexpr std::string_view marker = "T = ";

    constexpr auto pos = raw.find(marker);
    if (pos == std::string_view::npos) [[unlikely]] {
        // Should be unreachable on GCC 16; if reached, return the
        // full pretty-function string so the diagnostic at least
        // contains SOME information.
        return raw;
    }
    constexpr auto start = pos + marker.size();

    // End at the first ';' (separator between with-clause entries —
    // appears when the template has multiple parameters with default-
    // arg metadata, e.g. our `[with T = ...; std::string_view = ...]`)
    // OR the closing ']' of the with-clause if no ';' is present.
    constexpr auto semi    = raw.find(';', start);
    constexpr auto bracket = raw.find(']', start);
    constexpr auto end     = (semi != std::string_view::npos
                              && (bracket == std::string_view::npos || semi < bracket))
                                 ? semi
                                 : bracket;

    if (end == std::string_view::npos) [[unlikely]] return raw.substr(start);
    return raw.substr(start, end - start);
}

// ─── wrapper_class_name<Derived> — reflection-based wrapper spelling (#429)
//
// SessionHandleBase<Proto> is inherited by SEVERAL distinct wrapper
// classes — SessionHandle (the canonical one), CrashWatchedHandle
// (#400's crash-aware wrapper), RecordingSessionHandle (#404's event-
// log wrapper), Delegate/Accept handles (SessionDelegate.h), and any
// future wrapper layered atop the framework.  All inherit from the
// SAME base specialisation `SessionHandleBase<Proto>` for a given
// Proto.  Without per-derived-class identity, the destructor's
// abandonment diagnostic prints only Proto — leaving the developer
// unable to tell WHICH wrapper aborted when many coexist (the exact
// confusion that cost real debugging cycles in #400).
//
// This helper uses C++26 P2996 static reflection (GCC 16's
// `-freflection`, the same surface used by Reflect.h's auto-hashers)
// to extract the bare TEMPLATE name of `Derived` — "SessionHandle",
// "CrashWatchedHandle", "RecordingSessionHandle", etc. — without
// dragging the full template-argument list in front of the reader.
// The Proto rendering stays via the existing `type_name<Proto>()`
// helper, so the composed message reads cleanly:
//
//   "crucible::safety::proto: CrashWatchedHandle<Recv<int, End>>
//    abandoned at non-terminal protocol state.  ..."
//
// vs the pre-#429 ambiguous form:
//
//   "crucible::safety::proto: SessionHandle<Recv<int, End>>
//    abandoned ..."  (was it the wrapper?  the inner?  who knows)
//
// `Derived` may be `void` (the SessionHandleBase default — used when
// a derived class doesn't pass itself for backward compatibility);
// callers handle that case with `if constexpr (!std::is_void_v<...>)`.
//
// For non-template types (e.g., a hand-rolled wrapper without
// template parameters), the function returns the unqualified
// identifier.  For template specialisations, it walks through
// `template_of` to get the bare template name without the angle
// brackets and arguments.
template <typename Derived>
consteval std::string_view wrapper_class_name() noexcept {
    constexpr auto info = ^^Derived;
    if constexpr (std::meta::has_template_arguments(info)) {
        return std::meta::identifier_of(std::meta::template_of(info));
    } else {
        return std::meta::identifier_of(info);
    }
}

// ─── next_method_hint<Proto> — protocol-head action suggestion (#429)
//
// Maps each non-terminal protocol head shape to the &&-qualified
// consumer method the developer was supposed to call before letting
// the handle die.  Used by SessionHandleBase's destructor diagnostic
// to surface a concrete suggestion alongside the abandonment notice
// — instead of the developer having to look up "what method advances
// Recv<T, K>?", the message says "you forgot to call .recv()."
//
// The hints are deliberately lower-case verb-phrases so the message
// reads naturally:
//
//   "Expected: call .recv() on the handle (or .detach(detach_reason::*))."
//
// The hint surface mirrors the framework's actual method names:
//   * Send<T, K>            → "send(value)"
//   * Recv<T, K>            → "recv() / recv(callback)"
//   * Select<Bs...>         → "pick<branch_index>(transport_callable)"
//   * Offer<Bs...>          → "branch(visitor) or pick<branch_index>()"
//   * Loop<B>               → handles never reach Loop directly
//   * Anything else         → fall-through hint
//
// Delegate / Accept / CheckpointedSession / Stop are layered atop
// Session.h via separate headers; their hints live HERE so the
// destructor can render them without circular-include grief.  When a
// user adds a new combinator, they extend this switch rather than
// chasing the destructor body.
template <typename Proto>
consteval std::string_view next_method_hint() noexcept {
    if constexpr (is_send_v<Proto>) {
        return "send(value, transport_callable)";
    } else if constexpr (is_recv_v<Proto>) {
        return "recv() or recv(callback)";
    } else if constexpr (is_select_v<Proto>) {
        return "pick<branch_index>(transport_callable)";
    } else if constexpr (is_offer_v<Proto>) {
        return "branch(visitor) or pick<branch_index>(transport_callable)";
    } else if constexpr (is_loop_v<Proto>) {
        // Should be unreachable: the framework unrolls Loop before
        // constructing a SessionHandle.  Render for completeness.
        return "the loop body's appropriate consumer method "
               "(framework should have unrolled Loop<...>)";
    } else {
        // Delegate / Accept / Stop / CheckpointedSession / future
        // combinators land here.  The user knows which combinator
        // they're using; the message names it via Proto's full
        // type_name rendering above, so a generic hint suffices.
        return "the protocol head's appropriate consumer method "
               "(close / send / recv / pick / branch / delegate / accept / "
               "base / rollback)";
    }
}

}  // namespace detail

// ═════════════════════════════════════════════════════════════════
// ── detach_reason::* — typed reasons for SessionHandle::detach()
// ═════════════════════════════════════════════════════════════════
//
// `.detach(reason)` requires a tag from this namespace identifying
// the AUDIT CLASS of the abandonment (#376).  Each tag names a
// distinct sanctioned reason for marking the handle consumed
// without advancing the protocol; per-class greps localize each
// pattern's call sites:
//
//   grep "detach(detach_reason::TestInstrumentation"
//     — every test-side intentional drop
//   grep "detach(detach_reason::TransportClosedOutOfBand"
//     — every CNTP/peer-crash induced abandonment
//   grep "detach(detach_reason::InfiniteLoopProtocol"
//     — every infinite Loop<X> termination point
//
// Tags are empty structs inheriting from `detach_reason::tag_base`;
// the `DetachReason` concept gates `.detach()` to accept only
// inheritors of the base.  User extensions plug in by inheriting
// from tag_base; the concept admits them automatically.

namespace detach_reason {

// Marker base for the DetachReason concept.  Inheritance-based
// detection lets new tags plug in without trait-specialisation
// boilerplate.
struct tag_base {};

// Loop<X...Continue> protocols without a close branch — termination
// is implicit via the transport layer closing the channel below
// the session-type abstraction (TraceRing flush, MetaLog drain end,
// Augur metrics broadcast shutdown, SWIM gossip cluster shutdown,
// infinite serving loops at process exit).
struct InfiniteLoopProtocol : tag_base {};

// Transport detected the peer's session is gone out-of-band — CNTP
// completion error fires (IB_WC_RETRY_EXC_ERR), SWIM confirmed-dead
// signal arrives, kernel socket close on the underlying fd.  The
// session is unrecoverable; the handle is dropped without protocol
// advance because no advance can complete.
struct TransportClosedOutOfBand : tag_base {};

// Test / instrumentation drop — the test exercises a partial
// protocol path, then intentionally abandons.  Almost always
// scoped to test code.
struct TestInstrumentation : tag_base {};

// Async cancellation — std::jthread's stop_token fired mid-protocol,
// or a higher-level cancellation primitive routed a stop signal to
// the worker holding this handle.  Different from
// TransportClosedOutOfBand because the local side initiated the
// cancellation; the peer may still be alive.
struct AsyncCancellation : tag_base {};

// The owning object that bounded this handle's lifetime is being
// destructed; the handle has no choice but to detach.  Last-resort —
// indicates a design where the handle's lifetime didn't match the
// protocol's expected termination.  Common in bridge / wrapper
// types that mint borrowed handles (e.g.,
// MachineSessionBridge::session_view's mid-protocol consumer).
struct OwnerLifetimeBoundEarlyExit : tag_base {};

}  // namespace detach_reason

// Concept gate for detach()'s reason parameter.  Inheritance-based
// — user extensions plug in automatically by inheriting from
// detach_reason::tag_base.
template <typename T>
concept DetachReason =
    std::is_base_of_v<detach_reason::tag_base, T>
    && !std::is_same_v<T, detach_reason::tag_base>;

// ═════════════════════════════════════════════════════════════════
// ── SessionHandleBase<Proto> — lifetime-tracked CRTP base ────────
// ═════════════════════════════════════════════════════════════════
//
// Every SessionHandle specialisation inherits from this.  Provides:
//
//   * `tracker_` (consumed-flag) recording whether the handle has
//     been advanced past via a &&-qualified consumer method (close,
//     send, recv, select, pick, branch, delegate, accept, base,
//     rollback) or via .detach().  Move ctor/assign mark the SOURCE
//     consumed so moved-from handles don't fire the destructor check.
//
//   * Destructor check: in DEBUG builds, if the handle is destroyed
//     at a NON-TERMINAL protocol state AND was NOT consumed, call
//     std::abort() with a diagnostic.  This catches the class of
//     bug where code drops a mid-protocol handle on the floor
//     (e.g., forgot to loop back to Continue, forgot to call
//     close(), exception escapes a handle's scope).
//
//   * In RELEASE builds (NDEBUG), the destructor is a no-op AND the
//     `tracker_` field is empty (zero bytes via [[no_unique_address]]
//     + std::is_empty_v<consumed_tracker>).  EBO collapses the base
//     class in derived SessionHandle specialisations — sizeof of a
//     SessionHandle equals sizeof of its underlying Resource.
//
// Handle overhead:
//   * DEBUG:   1 byte for tracker_ + alignment padding (~8 bytes
//              for SessionHandle<End, int>).
//   * RELEASE: 0 bytes overhead.  sizeof(SessionHandle<End, int>) == 4.
//
// ─── Why CRTP base + public inheritance ──────────────────────────
//
// Deriving each SessionHandle<Proto, ...> from SessionHandleBase<
// Proto> avoids 9 copies of the destructor + move-semantics code,
// and keeps the lifetime contract in ONE place.  Public inheritance
// allows .detach() to be called on derived handles without per-class
// using-declarations; friend-of-SessionHandle relationships let
// sibling SessionHandle instantiations access protected members
// (e.g., Delegate's delegate() marking the delegated handle consumed).

// `Derived` is an OPTIONAL second template parameter naming the
// wrapper class that inherits this base (CRTP-style).  When supplied,
// the abandonment-check diagnostic in ~SessionHandleBase() can name
// the SPECIFIC wrapper that aborted (#429) — distinguishing
// SessionHandle's abort from CrashWatchedHandle's abort from
// RecordingSessionHandle's abort, even when all three share the same
// Proto template argument.  Defaulting to `void` keeps every existing
// inheritance site working unchanged; new wrappers SHOULD pass
// themselves to make the diagnostic precise.
template <typename Proto, typename Derived = void>
class SessionHandleBase {
    [[no_unique_address]] detail::consumed_tracker tracker_;

protected:
    // Call before returning from a &&-qualified consumer method.
    // After this, *this is safe to destruct (the destructor check
    // sees the tracker marked and skips the abort).  In RELEASE, this
    // is a no-op since the tracker is empty.
    constexpr void mark_consumed_() noexcept { tracker_.mark(); }

    constexpr bool is_consumed_() const noexcept { return tracker_.was_marked(); }

    // Expose to friend SessionHandle instantiations (e.g., Delegate's
    // delegate() method marks the delegated handle consumed).
    template <typename OtherProto, typename OtherDerived>
    friend class SessionHandleBase;
    template <typename P, typename R, typename L>
    friend class SessionHandle;

public:
    constexpr SessionHandleBase() noexcept = default;

    // Explicit source-location-capturing ctor (#429 improvement C).
    // Derived-class ctors propagate `std::source_location::current()`
    // (defaulted in their own signatures, so the captured site is the
    // user's call to mint_session_handle / .send / .recv / etc.).
    // RELEASE collapses the loc into a no-op via consumed_tracker's
    // empty-class branch — zero per-handle overhead.
    constexpr explicit SessionHandleBase(std::source_location loc) noexcept
        : tracker_{loc} {}

    // Static accessor for the protocol's compile-time-rendered name
    // (#379).  Inherited by every SessionHandle specialisation, so
    // user code can call `MyHandle::protocol_name()` to get the
    // human-readable shape (e.g., "crucible::safety::proto::Loop<
    // crucible::safety::proto::Send<int, crucible::safety::proto::
    // Continue> >").
    //
    // Useful for:
    //   * Logging the active session in production crash handlers
    //   * Debug-time diagnostics where you have a handle but no
    //     visibility into its compile-time Proto template arg
    //   * Test failure messages
    //
    // Returns a std::string_view pointing into the program's
    // constant data — safe to store and pass around for the lifetime
    // of the program.  Zero allocation; computed at compile time.
    [[nodiscard]] static constexpr std::string_view protocol_name() noexcept {
        return detail::type_name<Proto>();
    }

    // Static accessor for the DERIVED wrapper class's bare template
    // name (#429), via C++26 P2996 reflection — "SessionHandle" /
    // "CrashWatchedHandle" / "RecordingSessionHandle" / etc.  Useful
    // for:
    //
    //   * Production crash handlers logging which wrapper aborted
    //   * Test failure messages when many wrapper types coexist
    //   * Manual inspection: handle.wrapper_name() returns "CrashWatchedHandle"
    //     even when `decltype(handle)::protocol_name()` returns just the Proto
    //
    // When the derived class hasn't passed itself as the Derived
    // template argument (legacy inheritance sites — kept for
    // backward compatibility), wrapper_name() returns the literal
    // string "SessionHandle" — matching the historical fallback in
    // the destructor diagnostic.
    [[nodiscard]] static constexpr std::string_view wrapper_name() noexcept {
        if constexpr (!std::is_void_v<Derived>) {
            return detail::wrapper_class_name<Derived>();
        } else {
            return "SessionHandle";
        }
    }

    // Static accessor for the protocol-head-driven next-method hint
    // (#429 improvement B).  Returns a verb-phrase naming the
    // &&-qualified consumer method that advances the current
    // protocol head — "send(value)" / "recv()" / "pick<I>()" /
    // "branch(visitor)" / etc.  Useful for production crash
    // handlers that catch an abandonment signal and want to suggest
    // the missing call to the operator.
    [[nodiscard]] static constexpr std::string_view next_method_hint() noexcept {
        return detail::next_method_hint<Proto>();
    }

    // Static accessor for the FULL handle type spelling (#429
    // improvement A).  Returns the complete `__PRETTY_FUNCTION__`-
    // derived type name carrying every template argument
    // (Resource, PeerTag, LoopCtx, ...).  protocol_name() returns
    // ONLY Proto; full_handle_type_name() returns the WRAPPER's
    // full instantiation.  When `Derived` is `void` (legacy site),
    // returns the empty string; callers fall back to wrapper_name().
    [[nodiscard]] static constexpr std::string_view
    full_handle_type_name() noexcept {
        if constexpr (!std::is_void_v<Derived>) {
            return detail::type_name<Derived>();
        } else {
            return std::string_view{};
        }
    }

    // Public detach: mark the handle consumed WITHOUT advancing the
    // protocol.  Requires a typed reason tag from the
    // `detach_reason::*` namespace — the tag NAMES the audit class
    // and makes per-class greps mechanical (#376).
    //
    // Use ONLY when:
    //
    //   * The protocol is inherently infinite (Loop<X> without a close
    //     branch); the handle's "termination" is implicit via the
    //     transport layer closing the channel below the session-type
    //     abstraction.  Tag: `detach_reason::InfiniteLoopProtocol`.
    //   * The transport has closed the session out-of-band (peer
    //     crash detected at a lower layer, channel teardown, kernel-
    //     level socket close).
    //     Tag: `detach_reason::TransportClosedOutOfBand`.
    //   * Test / instrumentation scenarios that intentionally drop
    //     handles at a known-safe point.
    //     Tag: `detach_reason::TestInstrumentation`.
    //   * Runtime cancellation (jthread stop_token fired mid-protocol).
    //     Tag: `detach_reason::AsyncCancellation`.
    //   * Owning object destructed while handle still alive (last-resort).
    //     Tag: `detach_reason::OwnerLifetimeBoundEarlyExit`.
    //
    // Audit:
    //   grep "detach(detach_reason::"                — every detach
    //   grep "detach(detach_reason::TestInstrumentation"  — only tests
    //   grep "detach(detach_reason::TransportClosedOutOfBand" — only crashes
    //
    // The destructor-check still fires for handles that DON'T
    // explicitly detach, catching accidental abandonment.  In
    // RELEASE, detach is a no-op (the destructor check is also
    // no-op'd, so consistency holds).
    template <typename Reason>
        requires DetachReason<Reason>
    constexpr void detach(Reason /*reason_tag*/) && noexcept {
        tracker_.mark();
    }

    // Framework-routed rejection of bare `.detach()` (no reason tag).
    // The deleted overload outranks the templated one for a zero-arg
    // call, so the diagnostic comes from this string rather than from
    // the GCC-version-specific "no matching function" wrapper text.
    // Audit grep: `[DetachReason_Required]`.
    void detach() && = delete(
        "[DetachReason_Required] SessionHandle::detach() requires a typed "
        "reason tag from detach_reason::*.  Pass one of "
        "detach_reason::InfiniteLoopProtocol{} (Loop<X> with no close "
        "branch), TransportClosedOutOfBand{} (peer crash), "
        "TestInstrumentation{} (test code only), AsyncCancellation{} "
        "(jthread stop_token), or OwnerLifetimeBoundEarlyExit{} "
        "(bridge/wrapper destructor).  Per #376, the tag NAMES the audit "
        "class so per-class greps stay mechanical.");

    // Linear — copy-deleted with reason.
    SessionHandleBase(const SessionHandleBase&)
        = delete("SessionHandle is linear — protocol progress is consumed, not copied.");
    SessionHandleBase& operator=(const SessionHandleBase&)
        = delete("SessionHandle is linear — protocol progress is consumed, not copied.");

    // Move semantics mark the source as consumed so its destructor
    // check skips the abort.  The MOVED-INTO handle inherits the
    // source's consumed state (typically false — a newly-moved-into
    // handle is fresh).  In RELEASE, tracker.move_from is a no-op
    // and the entire move sequence trivializes to a default move.
    //
    // The move ctor cannot self-construct (the language forbids
    // `auto h(std::move(h))` from naming `h` in its own initialiser
    // before construction completes), but the move-assignment CAN
    // self-assign via aliasing (`h = std::move(h);`).  The C++
    // standard says self-move leaves the object "valid but
    // unspecified"; our specific contract is STRONGER — self-move
    // must leave the consumed-state unchanged so the abandonment
    // check still fires for genuinely-leaked handles.  Defence is
    // layered: consumed_tracker::move_from short-circuits the
    // primitive case (#365), and the explicit early return below
    // documents the contract at the boundary the user calls.
    constexpr SessionHandleBase(SessionHandleBase&& other) noexcept
    {
        tracker_.move_from(other.tracker_);
    }

    constexpr SessionHandleBase& operator=(SessionHandleBase&& other) noexcept
    {
        if (this == &other) [[unlikely]] return *this;
        tracker_.move_from(other.tracker_);
        return *this;
    }

    ~SessionHandleBase() {
#ifndef NDEBUG
        if (!tracker_.was_marked() && !is_terminal_state_v<Proto>) {
            // ── Structured ABANDONMENT diagnostic (#429 improvements
            //    A + B + C + D — render every load-bearing fact about
            //    the abandoning handle so the developer doesn't have
            //    to re-derive any of it from a stack trace) ─────────
            //
            //   A. Full Derived type spelling (carries Resource,
            //      PeerTag, LoopCtx — pre-#429 these were absent and
            //      two CrashWatchedHandles for different PeerTags
            //      rendered indistinguishable).
            //   B. Protocol-head-driven next-method hint (suggests
            //      .send / .recv / .pick / .branch — whatever was
            //      supposed to advance Proto's head).
            //   C. Construction site captured at mint_session_handle
            //      / .send / .recv / etc. via std::source_location's
            //      default-arg call-site capture.  Tells the
            //      developer where the now-orphaned handle was born,
            //      so they can grep directly to the line that
            //      ultimately leaked.
            //   D. Structured enumeration of detach reasons (with
            //      one-line per-reason explanation) so the developer
            //      can pick the right tag without consulting docs.
            constexpr auto pname = detail::type_name<Proto>();
            constexpr auto hint  = detail::next_method_hint<Proto>();
            const auto loc = tracker_.construction_loc();
            const char* loc_file = loc.file_name();
            const char* loc_func = loc.function_name();
            const auto loc_line  = loc.line();
            const auto loc_col   = loc.column();
            // file_name() returns "" when the location is default-
            // constructed (handle minted without an explicit loc — a
            // legacy code path).  Render a sentinel in that case so
            // the diagnostic stays unambiguous.
            const bool have_loc = loc_file != nullptr && loc_file[0] != '\0';

            if constexpr (!std::is_void_v<Derived>) {
                constexpr auto wname = detail::wrapper_class_name<Derived>();
                constexpr auto fname = detail::type_name<Derived>();
                std::fprintf(stderr,
                    "\n"
                    "═════════════════════════════════════════════════════════════════════\n"
                    "crucible::safety::proto: ABANDONMENT DETECTED (non-terminal handle)\n"
                    "═════════════════════════════════════════════════════════════════════\n"
                    "  Wrapper class:    %.*s\n"
                    "  Full handle type: %.*s\n"
                    "  Protocol head:    %.*s\n",
                    static_cast<int>(wname.size()), wname.data(),
                    static_cast<int>(fname.size()), fname.data(),
                    static_cast<int>(pname.size()), pname.data());
            } else {
                std::fprintf(stderr,
                    "\n"
                    "═════════════════════════════════════════════════════════════════════\n"
                    "crucible::safety::proto: ABANDONMENT DETECTED (non-terminal handle)\n"
                    "═════════════════════════════════════════════════════════════════════\n"
                    "  Wrapper class:    SessionHandle (Derived not provided to base)\n"
                    "  Protocol head:    %.*s\n",
                    static_cast<int>(pname.size()), pname.data());
            }
            if (have_loc) {
                std::fprintf(stderr,
                    "  Construction at:  %s:%u:%u\n"
                    "  In function:      %s\n",
                    loc_file, static_cast<unsigned>(loc_line),
                    static_cast<unsigned>(loc_col), loc_func);
            } else {
                std::fprintf(stderr,
                    "  Construction at:  <unknown — handle minted without "
                    "source_location capture>\n");
            }
            std::fprintf(stderr,
                "  Expected action:  call .%.*s\n"
                "\n"
                "The handle was destroyed via its destructor without being\n"
                "consumed via a &&-qualified consumer method.  Either:\n"
                "  1. Consume the handle by calling its appropriate consumer\n"
                "     method (close / send / recv / pick / branch / delegate /\n"
                "     accept / base / rollback), OR\n"
                "  2. Advance the protocol to a terminal state (End or Stop), OR\n"
                "  3. Explicitly abandon via std::move(handle).detach(reason),\n"
                "     where `reason` is one of:\n"
                "       * detach_reason::InfiniteLoopProtocol\n"
                "           — Loop<X> with no close branch; transport-level close.\n"
                "       * detach_reason::TransportClosedOutOfBand\n"
                "           — peer crash detected (CNTP RETRY_EXC, SWIM dead, fd close).\n"
                "       * detach_reason::TestInstrumentation\n"
                "           — test code intentionally drops at a known-safe point.\n"
                "       * detach_reason::AsyncCancellation\n"
                "           — std::jthread stop_token fired mid-protocol.\n"
                "       * detach_reason::OwnerLifetimeBoundEarlyExit\n"
                "           — bridge/wrapper destructor; last-resort abandonment.\n"
                "\n"
                "See safety/Session.h §SessionHandleBase for the full lifetime\n"
                "contract.  The framework cannot recover the protocol; aborting.\n"
                "═════════════════════════════════════════════════════════════════════\n",
                static_cast<int>(hint.size()), hint.data());
            std::abort();
        }
#endif
    }
};

// ═════════════════════════════════════════════════════════════════
// ── SessionHandle — the runtime handle ───────────────────────────
// ═════════════════════════════════════════════════════════════════
//
// Primary template is INTENTIONALLY undefined.  Only the combinator-
// head specialisations below (End / Send / Recv / Select / Offer)
// can be instantiated.  Instantiating SessionHandle with, say, a
// Loop<B> proto is a compile error — the factory unrolls Loop before
// constructing the handle.

template <typename Proto, typename Resource, typename LoopCtx = void>
class SessionHandle;

// ─── Internal: step_to_next<R, Resource, LoopCtx> ────────────────
//
// Given a continuation Proto R and a LoopCtx, construct the next
// SessionHandle.  Resolves:
//   * R = Continue       → SessionHandle<LoopCtx::body, Res, LoopCtx>
//   * R = Loop<InnerB>   → SessionHandle<InnerB, Res, Loop<InnerB>>
//   * R = anything else  → SessionHandle<R, Res, LoopCtx>
//
// The LoopCtx shadowing at nested Loop entry is what gives us
// lexical scope for Continue.

namespace detail {

template <typename R, typename Resource, typename LoopCtx>
[[nodiscard]] constexpr auto step_to_next(
    Resource r,
    std::source_location loc = std::source_location::current()) noexcept
{
    if constexpr (std::is_same_v<R, Continue>) {
        static_assert(!std::is_void_v<LoopCtx>,
            "crucible::session::diagnostic [Continue_Without_Loop]: "
            "proto: Continue appears outside a Loop context.  "
            "Every Continue must have an enclosing Loop<Body>.");
        using NextBody = typename LoopCtx::body;
        // NextBody may itself begin with Loop or Continue (unlikely but
        // syntactically legal) — recurse.  Forward `loc` so the
        // outermost producer's call site survives the recursion (#429).
        return step_to_next<NextBody, Resource, LoopCtx>(std::move(r), loc);
    } else if constexpr (is_loop_v<R>) {
        using InnerBody = typename R::body;
        // Enter inner Loop: shadow LoopCtx with the new Loop.  Recurse
        // to handle the case where InnerBody begins with Continue
        // (which would already be ill-formed — caught by is_well_formed).
        return step_to_next<InnerBody, Resource, R>(std::move(r), loc);
    } else {
        static_assert(is_head_v<R>,
            "crucible::session::diagnostic [Protocol_Ill_Formed]: "
            "proto: unexpected protocol shape after resolution.  "
            "Only Send/Recv/Select/Offer/End/Continue are valid heads.");
        return SessionHandle<R, Resource, LoopCtx>{std::move(r), loc};
    }
}

}  // namespace detail

// ═════════════════════════════════════════════════════════════════
// ── SessionHandle<End, Resource, LoopCtx> — terminal state ──────
// ═════════════════════════════════════════════════════════════════

template <typename Resource, typename LoopCtx>
class [[nodiscard]] SessionHandle<End, Resource, LoopCtx>
    : public SessionHandleBase<End, SessionHandle<End, Resource, LoopCtx>>
{
    Resource resource_;

    template <typename P, typename R, typename L>
    friend class SessionHandle;

    template <typename P, typename R>
    friend constexpr auto mint_session_handle(R r) noexcept;

    template <typename R, typename Res, typename L>
    friend constexpr auto detail::step_to_next(Res) noexcept;

public:
    using protocol      = End;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;

    constexpr explicit SessionHandle(
        Resource r,
        std::source_location loc = std::source_location::current())
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<End, SessionHandle<End, Resource, LoopCtx>>{loc}
        , resource_{std::move(r)} {}

    // Copy-delete with reason inherited from SessionHandleBase<End>.
    // Move-ctor/assign: = default invokes base's custom move (which
    // sets source.consumed_ = true) + default-moves Resource.
    // Destructor: = default invokes base's dtor (which skips the check
    // because is_terminal_state_v<End> is true).
    constexpr SessionHandle(SessionHandle&&) noexcept            = default;
    constexpr SessionHandle& operator=(SessionHandle&&) noexcept = default;
    ~SessionHandle()                                             = default;

    // Terminal operation — consumes the handle, yields the Resource.
    [[nodiscard]] constexpr Resource close() &&
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
    {
        this->mark_consumed_();
        return std::move(resource_);
    }

    // Diagnostic borrow — does NOT consume the handle; useful for
    // logging / inspection before the explicit close().
    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return resource_; }
};

// ═════════════════════════════════════════════════════════════════
// ── SessionHandle<Send<T, R>, …> ────────────────────────────────
// ═════════════════════════════════════════════════════════════════

template <typename T, typename R, typename Resource, typename LoopCtx>
class [[nodiscard]] SessionHandle<Send<T, R>, Resource, LoopCtx>
    : public SessionHandleBase<Send<T, R>,
                               SessionHandle<Send<T, R>, Resource, LoopCtx>>
{
    Resource resource_;

    template <typename P, typename Res, typename L> friend class SessionHandle;
    template <typename P, typename Res>
    friend constexpr auto mint_session_handle(Res) noexcept;
    template <typename U, typename Res, typename L>
    friend constexpr auto detail::step_to_next(Res) noexcept;

public:
    using protocol      = Send<T, R>;
    using message_type  = T;
    using continuation  = R;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;

    constexpr explicit SessionHandle(
        Resource r,
        std::source_location loc = std::source_location::current())
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Send<T, R>,
                            SessionHandle<Send<T, R>, Resource, LoopCtx>>{loc}
        , resource_{std::move(r)} {}

    constexpr SessionHandle(SessionHandle&&) noexcept            = default;
    constexpr SessionHandle& operator=(SessionHandle&&) noexcept = default;
    ~SessionHandle()                                             = default;

    // Send via user-supplied Transport.  Transport signature:
    //   void(Resource&, T&&)
    // Returns the handle for the continuation (with Continue/Loop
    // resolution applied by step_to_next).
    template <typename Transport>
        requires std::is_invocable_v<Transport, Resource&, T&&>
    [[nodiscard]] constexpr auto send(T value, Transport transport) &&
        noexcept(std::is_nothrow_invocable_v<Transport, Resource&, T&&>
                 && std::is_nothrow_move_constructible_v<Resource>
                 && std::is_nothrow_move_constructible_v<T>)
    {
        std::invoke(transport, resource_, std::move(value));
        this->mark_consumed_();
        return detail::step_to_next<R, Resource, LoopCtx>(std::move(resource_));
    }

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return resource_; }
};

// ═════════════════════════════════════════════════════════════════
// ── SessionHandle<Recv<T, R>, …> ────────────────────────────────
// ═════════════════════════════════════════════════════════════════

template <typename T, typename R, typename Resource, typename LoopCtx>
class [[nodiscard]] SessionHandle<Recv<T, R>, Resource, LoopCtx>
    : public SessionHandleBase<Recv<T, R>,
                               SessionHandle<Recv<T, R>, Resource, LoopCtx>>
{
    Resource resource_;

    template <typename P, typename Res, typename L> friend class SessionHandle;
    template <typename P, typename Res>
    friend constexpr auto mint_session_handle(Res) noexcept;
    template <typename U, typename Res, typename L>
    friend constexpr auto detail::step_to_next(Res) noexcept;

public:
    using protocol      = Recv<T, R>;
    using message_type  = T;
    using continuation  = R;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;

    constexpr explicit SessionHandle(
        Resource r,
        std::source_location loc = std::source_location::current())
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Recv<T, R>,
                            SessionHandle<Recv<T, R>, Resource, LoopCtx>>{loc}
        , resource_{std::move(r)} {}

    constexpr SessionHandle(SessionHandle&&) noexcept            = default;
    constexpr SessionHandle& operator=(SessionHandle&&) noexcept = default;
    ~SessionHandle()                                             = default;

    // Receive via Transport.  Transport signature:
    //   T(Resource&)
    // Returns (received value, next-handle).
    template <typename Transport>
        requires std::is_invocable_r_v<T, Transport, Resource&>
    [[nodiscard]] constexpr auto recv(Transport transport) &&
        noexcept(std::is_nothrow_invocable_r_v<T, Transport, Resource&>
                 && std::is_nothrow_move_constructible_v<Resource>
                 && std::is_nothrow_move_constructible_v<T>)
    {
        T value = std::invoke(transport, resource_);
        this->mark_consumed_();
        auto next = detail::step_to_next<R, Resource, LoopCtx>(std::move(resource_));
        return std::pair{std::move(value), std::move(next)};
    }

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return resource_; }
};

// ═════════════════════════════════════════════════════════════════
// ── SessionHandle<Select<Bs…>, …> — internal choice ─────────────
// ═════════════════════════════════════════════════════════════════

template <typename... Branches, typename Resource, typename LoopCtx>
class [[nodiscard]] SessionHandle<Select<Branches...>, Resource, LoopCtx>
    : public SessionHandleBase<Select<Branches...>,
                               SessionHandle<Select<Branches...>, Resource, LoopCtx>>
{
    Resource resource_;

    template <typename P, typename Res, typename L> friend class SessionHandle;
    template <typename P, typename Res>
    friend constexpr auto mint_session_handle(Res) noexcept;
    template <typename U, typename Res, typename L>
    friend constexpr auto detail::step_to_next(Res) noexcept;

public:
    using protocol       = Select<Branches...>;
    using resource_type  = Resource;
    using loop_ctx       = LoopCtx;

    static constexpr std::size_t branch_count = sizeof...(Branches);

    // Belt-and-braces empty-choice rejection (#364).  Direct
    // construction of `SessionHandle<Select<>, ...>` (bypassing
    // mint_session_handle's own static_assert) lands here and fires
    // the same named diagnostic, so users can't sneak past the
    // discipline by going around the factory.  Subtyping-level uses
    // of `Select<>` (Gay-Hole 2005 minimum subtype) are unaffected
    // — they don't instantiate this class.
    static_assert(branch_count > 0,
        "crucible::session::diagnostic [Empty_Choice_Combinator]: "
        "SessionHandle<Select<>>: cannot construct a runnable handle "
        "on Select<> with zero branches — there is no branch for "
        ".pick<I>() to select.  See mint_session_handle for the full "
        "diagnostic and remediation.");

    constexpr explicit SessionHandle(
        Resource r,
        std::source_location loc = std::source_location::current())
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Select<Branches...>,
                            SessionHandle<Select<Branches...>, Resource, LoopCtx>>{loc}
        , resource_{std::move(r)} {}

    constexpr SessionHandle(SessionHandle&&) noexcept            = default;
    constexpr SessionHandle& operator=(SessionHandle&&) noexcept = default;
    ~SessionHandle()                                             = default;

    // Pick branch I and signal the choice to peer via Transport.
    // Transport signature: void(Resource&, std::size_t).
    //
    // Out-of-range I produces a NAMED [Branch_Index_Out_Of_Range]
    // diagnostic (#432) — pre-#432 the requires-clause-only gate
    // produced the generic GCC "constraints not satisfied: (I <
    // sizeof...(Branches))" message which omitted the protocol
    // context.  The static_assert inside the body fires after the
    // function is selected (Transport requirement still gates
    // overload resolution), so the message naming conventions from
    // the rest of the framework apply here too.
    template <std::size_t I, typename Transport>
        requires std::is_invocable_v<Transport, Resource&, std::size_t>
    [[nodiscard]] constexpr auto select(Transport transport) &&
        noexcept(std::is_nothrow_invocable_v<Transport, Resource&, std::size_t>
                 && std::is_nothrow_move_constructible_v<Resource>)
    {
        static_assert(I < sizeof...(Branches),
            "crucible::session::diagnostic [Branch_Index_Out_Of_Range]: "
            "SessionHandle<Select<...>>::select<I>(transport): branch "
            "index I is out of range for this Select position.  The "
            "protocol has fewer branches than the index requested; "
            "verify I < branch_count at the call site (decltype("
            "handle)::branch_count is exposed for compile-time queries).");
        std::invoke(transport, resource_, I);
        this->mark_consumed_();
        using Chosen = std::tuple_element_t<I, std::tuple<Branches...>>;
        return detail::step_to_next<Chosen, Resource, LoopCtx>(std::move(resource_));
    }

    // Convenience: select WITHOUT signalling the choice over the wire
    // (#377).  Renamed from the bare `select<I>()` to `select_local
    // <I>()` so the wire ABSENCE is visible at every call site —
    // calling this on a wire-based session means the peer NEVER sees
    // the branch choice and silently drifts off-protocol (the original
    // footgun this rename surfaces).  Use ONLY for:
    //   * In-memory channels where both endpoints share state.
    //   * Unit tests with mocked transport.
    //   * Static pipelines whose branch is fixed at compile time and
    //     the peer's protocol is hard-coded to receive the same
    //     branch's continuation.
    //
    // For wire-based sessions, use `select<I>(transport)` instead —
    // its Transport callable is what physically tells the peer which
    // branch was picked.
    //
    // Audit:
    //   grep "select_local<"   — every wire-omitting call site
    //   grep "select<.*>(.*)"  — every wire-based call site
    template <std::size_t I>
    [[nodiscard]] constexpr auto select_local() &&
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
    {
        static_assert(I < sizeof...(Branches),
            "crucible::session::diagnostic [Branch_Index_Out_Of_Range]: "
            "SessionHandle<Select<...>>::select_local<I>(): branch index "
            "I is out of range for this Select position.  The protocol "
            "has fewer branches than the index requested; verify I < "
            "branch_count at the call site.");
        this->mark_consumed_();
        using Chosen = std::tuple_element_t<I, std::tuple<Branches...>>;
        return detail::step_to_next<Chosen, Resource, LoopCtx>(std::move(resource_));
    }

    // The bare `select<I>()` overload is now `= delete`'d (#377) so
    // every existing call site is FORCED to make an explicit choice
    // between the wire variant (`select<I>(transport)`) and the wire-
    // omitting variant (`select_local<I>()`).  Pre-#377, the bare
    // `select<I>()` silently selected the wire-omitting variant —
    // which on a wire-based session means the peer never receives
    // the branch choice and silently drifts off-protocol.
    template <std::size_t I>
    void select() && = delete(
        "[Wire_Variant_Required] SessionHandle<Select<...>>::select<I>() "
        "without arguments is no longer allowed (#377).  Choose one: "
        "(a) `select<I>(transport)` to signal the branch choice over "
        "the wire (the peer sees the I-th branch and stays in sync), "
        "OR (b) `select_local<I>()` to advance the local handle WITHOUT "
        "signalling the peer (in-memory channels and unit tests only — "
        "wire-based sessions where the peer doesn't observe the "
        "choice will silently drift off-protocol).  Per #377, the "
        "framework refuses to guess which one you meant.");

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return resource_; }
};

// ═════════════════════════════════════════════════════════════════
// ── SessionHandle<Offer<Bs…>, …> — external choice ──────────────
// ═════════════════════════════════════════════════════════════════

template <typename... Branches, typename Resource, typename LoopCtx>
class [[nodiscard]] SessionHandle<Offer<Branches...>, Resource, LoopCtx>
    : public SessionHandleBase<Offer<Branches...>,
                               SessionHandle<Offer<Branches...>, Resource, LoopCtx>>
{
    Resource resource_;

    template <typename P, typename Res, typename L> friend class SessionHandle;
    template <typename P, typename Res>
    friend constexpr auto mint_session_handle(Res) noexcept;
    template <typename U, typename Res, typename L>
    friend constexpr auto detail::step_to_next(Res) noexcept;

public:
    using protocol      = Offer<Branches...>;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;

    static constexpr std::size_t branch_count = sizeof...(Branches);

    // Belt-and-braces empty-choice rejection (#364).  Mirror of the
    // Select<> guard above — direct construction of `SessionHandle<
    // Offer<>, ...>` cannot happen because there is no peer label
    // that could decode to a valid branch.  Subtyping-level uses
    // (Gay-Hole 2005) are unaffected.
    static_assert(branch_count > 0,
        "crucible::session::diagnostic [Empty_Choice_Combinator]: "
        "SessionHandle<Offer<>>: cannot construct a runnable handle "
        "on Offer<> with zero branches — there is no label the peer "
        "can send.  See mint_session_handle for the full diagnostic "
        "and remediation.");

    constexpr explicit SessionHandle(
        Resource r,
        std::source_location loc = std::source_location::current())
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Offer<Branches...>,
                            SessionHandle<Offer<Branches...>, Resource, LoopCtx>>{loc}
        , resource_{std::move(r)} {}

    constexpr SessionHandle(SessionHandle&&) noexcept            = default;
    constexpr SessionHandle& operator=(SessionHandle&&) noexcept = default;
    ~SessionHandle()                                             = default;

    // Receive peer's branch choice via Transport, then invoke handler
    // with the resulting branch handle.
    //   Transport signature: std::size_t(Resource&)
    //   Handler  signature: R(SessionHandle<BranchI, Resource, LoopCtx>)
    //                       for each branch (all must return same R, or all void).
    //
    // If Transport returns an out-of-range index, std::abort() is
    // called — the protocol is broken and there is no safe recovery.
    template <typename Transport, typename Handler>
        requires std::is_invocable_r_v<std::size_t, Transport, Resource&>
    constexpr auto branch(Transport transport, Handler handler) &&
    {
        const std::size_t idx = std::invoke(transport, resource_);
        this->mark_consumed_();
        return dispatch_branch_(idx, std::move(resource_), std::move(handler),
                                std::make_index_sequence<sizeof...(Branches)>{});
    }

    // Pick branch I WITHOUT receiving a peer-driven label (#377).
    // Renamed from `pick<I>()` to `pick_local<I>()` so the wire
    // ABSENCE is visible at every call site — calling this on a
    // wire-based session means the local handle assumes branch I
    // without ever receiving the peer's actual choice.  If the peer
    // signals a different branch, the two endpoints silently diverge
    // off-protocol.
    //
    // Use ONLY for:
    //   * In-memory channels where both endpoints share state.
    //   * Unit tests with mocked transport.
    //   * Static pipelines whose branch is fixed at compile time.
    //
    // Out-of-range I → named [Branch_Index_Out_Of_Range] (#433),
    // mirror of Select::select_local<I>'s discipline.
    template <std::size_t I>
    [[nodiscard]] constexpr auto pick_local() &&
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
    {
        static_assert(I < sizeof...(Branches),
            "crucible::session::diagnostic [Branch_Index_Out_Of_Range]: "
            "SessionHandle<Offer<...>>::pick_local<I>(): branch index "
            "I is out of range for this Offer position.  The protocol "
            "has fewer branches than the index requested; verify I < "
            "branch_count at the call site.");
        this->mark_consumed_();
        using Chosen = std::tuple_element_t<I, std::tuple<Branches...>>;
        return detail::step_to_next<Chosen, Resource, LoopCtx>(std::move(resource_));
    }

    // The bare `pick<I>()` overload is `= delete`'d (#377) — every
    // call site must pick (a) a peer-receiving variant (when one is
    // wired up by the user's transport) or (b) `pick_local<I>()` to
    // explicitly skip the peer label.  Pre-#377, the bare `pick<I>()`
    // silently selected the peer-skipping variant — wire-based
    // sessions where the peer didn't send the matching label silently
    // diverged off-protocol.
    template <std::size_t I>
    void pick() && = delete(
        "[Wire_Variant_Required] SessionHandle<Offer<...>>::pick<I>() "
        "without arguments is no longer allowed (#377).  Use "
        "`pick_local<I>()` to advance the local handle WITHOUT "
        "receiving a peer label (in-memory channels and unit tests "
        "only — wire-based sessions where the peer's actual choice "
        "differs from I will silently drift off-protocol).  Per #377, "
        "the framework refuses to guess that the peer-skipping "
        "variant was what you meant.");

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return resource_; }

private:
    // Construct the branch-I handle with Continue / Loop resolution.
    template <std::size_t I>
    static constexpr auto make_branch_handle_(Resource r) {
        using B = std::tuple_element_t<I, std::tuple<Branches...>>;
        return detail::step_to_next<B, Resource, LoopCtx>(std::move(r));
    }

    template <std::size_t... Is, typename Handler>
    static constexpr auto dispatch_branch_(
        std::size_t                 idx,
        Resource                    res,
        Handler                     handler,
        std::index_sequence<Is...>)
    {
        if (idx >= sizeof...(Branches)) [[unlikely]] {
            std::abort();  // broken protocol — no safe recovery
        }

        // Deduce the handler's return type from branch 0.  All branches
        // must produce the same type (or all void); enforced by the
        // fact that we assign to a single std::optional<Result> /
        // single expression evaluation below.
        using FirstHandle = decltype(make_branch_handle_<0>(std::declval<Resource>()));
        using Result      = std::invoke_result_t<Handler&&, FirstHandle>;

        if constexpr (std::is_void_v<Result>) {
            // Void path — dispatch, no return value to collect.
            bool dispatched = false;
            ([&]() {
                if (!dispatched && idx == Is) {
                    std::invoke(std::move(handler),
                                make_branch_handle_<Is>(std::move(res)));
                    dispatched = true;
                }
            }(), ...);
            // dispatched must be true (idx was bounds-checked above).
        } else {
            // Non-void — collect via optional emplace.
            std::optional<Result> result;
            bool dispatched = false;
            ([&]() {
                if (!dispatched && idx == Is) {
                    result.emplace(std::invoke(std::move(handler),
                                                make_branch_handle_<Is>(std::move(res))));
                    dispatched = true;
                }
            }(), ...);
            if (!result) [[unlikely]] std::abort();
            return std::move(*result);
        }
    }
};

// ═════════════════════════════════════════════════════════════════
// ── SessionResource concept — channel-resource pin discipline ────
// ═════════════════════════════════════════════════════════════════
//
// The Resource template parameter of SessionHandle<Proto, Resource>
// is what the handle stores at runtime.  In production it is one of
// three shapes:
//
//   1. A VALUE TYPE (FakeRes, Wire, std::reference_wrapper<Channel>,
//      std::unique_ptr<Channel>, std::span<T>, function pointers,
//      POD scalars, etc.).  The handle owns it by value; copy/move
//      of the handle moves the value with it.  Cannot dangle —
//      lifetime is the handle's.
//
//   2. An LVALUE REFERENCE TO A PINNED OBJECT (PermissionedSpscChannel
//      &, MpmcRing<T,N>&, AtomicSnapshot<T>&).  The handle stores a
//      pointer; the pointee's address is stable for the pointee's
//      lifetime (Pinned's deleted move guarantees this).  The
//      caller's discipline is to outlive every handle they minted
//      from the channel; the framework enforces the address-stability
//      half via Pinned.
//
//   3. An LVALUE REFERENCE TO A NON-PINNED OBJECT.  HAZARD CLASS.
//      The pointee's address can change when the pointee is moved or
//      assigned-to; live handles dangle without diagnostic.  The
//      SessionResource concept REJECTS this case at compile time.
//
// Rvalue references are also rejected — a handle bound to an rvalue
// reference would point at a temporary that disappears before the
// handle is used.
//
// ─── What this concept ENFORCES ─────────────────────────────────────
//
//   Resource = T        (value type)              ALLOWED
//   Resource = T&       where T derives Pinned    ALLOWED
//   Resource = T&       where T does NOT          REJECTED at compile time
//   Resource = T&&                                REJECTED at compile time
//   Resource = T*       where T is an object      ALLOWED (caller's
//                                                  manual lifetime
//                                                  contract; raw ptrs
//                                                  are rare in this
//                                                  codebase, and the
//                                                  pointer being a
//                                                  value type means
//                                                  the handle owns
//                                                  the pointer slot)
//   Resource = R(*)(...)  function pointer        ALLOWED (functions
//                                                  have stable
//                                                  addresses by
//                                                  language rule)
//
// The discipline closes the bug class "I created handles, then moved
// my channel, and now the handles dangle" — a real production failure
// mode flagged in misc/24_04_2026_safety_integration.md §17 and the
// SessionResource Pinned-constraint task #406.
//
// ─── Why we don't constrain raw object pointers ─────────────────────
//
// Raw object pointers (Resource = SomeObject*) could in principle
// require the pointee to be Pinned-derived too.  We choose to allow
// them unconstrained in v1 because (a) raw object pointers are rare
// in Crucible's code style — references and smart pointers are the
// idioms — and (b) when raw pointers ARE used, the calling code is
// already in the manual-lifetime regime where the framework's
// type-system support is necessarily partial.  Tightening this is a
// future v2 refinement; the canonical hazard (lvalue reference to a
// non-Pinned object) is what we close today.

template <typename Resource>
concept SessionResource =
    !std::is_reference_v<Resource>
    || (std::is_lvalue_reference_v<Resource>
        && std::derived_from<std::remove_reference_t<Resource>,
                              safety::Pinned<std::remove_reference_t<Resource>>>);

// ═════════════════════════════════════════════════════════════════
// ── Factory: mint_session_handle<Proto>(resource) ───────────────
// ═════════════════════════════════════════════════════════════════
//
// Entry point for constructing a SessionHandle.  If Proto starts with
// Loop, it unrolls one iteration so the returned handle is positioned
// at Loop's body with the Loop itself as the LoopCtx.  Protocols
// starting with any other head go through unchanged.
//
// Compile error if Proto is ill-formed (Continue outside Loop, etc.)
// or if Resource fails the SessionResource pin-discipline check.

template <typename Proto, typename Resource>
[[nodiscard]] constexpr auto mint_session_handle(
    Resource r,
    std::source_location loc = std::source_location::current()) noexcept
{
    static_assert(is_well_formed_v<Proto>,
        "crucible::session::diagnostic [Protocol_Ill_Formed]: "
        "proto: protocol is ill-formed.  Most likely cause: a Continue "
        "appears outside any enclosing Loop<Body>.  Every Continue must "
        "have a Loop above it in the protocol tree.");

    // Reject Select<> / Offer<> with zero branches (#364).  These are
    // valid TYPE operands in subtyping theory (Gay-Hole 2005 rule:
    // Select<> ⩽ Select<X>, covariance on alternatives), and
    // SessionSubtype.h's trait-level tests legitimately use them —
    // but they are NOT runnable: there is no branch to pick and no
    // label the peer can send.  Reject at the handle boundary so
    // subtyping-level reasoning stays permissive while handle
    // construction stays safe.
    static_assert(!is_empty_choice_v<Proto>,
        "crucible::session::diagnostic [Empty_Choice_Combinator]: "
        "proto: mint_session_handle<Select<>> or mint_session_handle"
        "<Offer<>> — cannot construct a runnable handle on a choice "
        "combinator with zero branches.  Select<> has no branch for "
        ".pick<I>() to select; Offer<> has no label the peer can "
        "signal.  If you intend a type-level subtyping witness, use "
        "is_subtype_sync_v<...> directly; if you intend a runnable "
        "handle, add at least one branch (e.g., Select<Send<Stop, "
        "End>> for an acknowledgement-only selection).");

    static_assert(SessionResource<Resource>,
        "crucible::session::diagnostic [SessionResource_NotPinned]: "
        "mint_session_handle<Proto, Resource>: Resource must be either "
        "a value type (handle owns it by value) or an lvalue reference "
        "to a type derived from safety::Pinned<T>.  An lvalue reference "
        "to a non-Pinned object lets a subsequent move of the channel "
        "leave live handles dangling (use-after-free).  Either: (a) "
        "make the channel Pinned by deriving it from "
        "safety::Pinned<ChannelType>, or (b) pass the channel by "
        "value (copies are fine for value-like channels), or (c) wrap "
        "the channel in std::reference_wrapper if the caller's "
        "lifetime contract is satisfied by other means.  Rvalue-"
        "reference Resource is also rejected — the handle would bind "
        "to a temporary and dangle immediately on return.");

    if constexpr (is_loop_v<Proto>) {
        using Body = typename Proto::body;
        // Recursively unroll — Body could itself begin with Loop,
        // though that's unusual.  step_to_next handles the recursion
        // uniformly.  Forward `loc` so the user's mint_session_handle
        // call site is what shows up in the abandonment diagnostic
        // (#429 improvement C), even after a Loop unroll inserted an
        // intermediate handle.
        return detail::step_to_next<Body, Resource, Proto>(std::move(r), loc);
    } else {
        static_assert(!std::is_same_v<Proto, Continue>,
            "crucible::session::diagnostic [Continue_Without_Loop]: "
            "proto: Continue cannot be the top-level protocol.");
        return SessionHandle<Proto, Resource, void>{std::move(r), loc};
    }
}

// ═════════════════════════════════════════════════════════════════
// ── mint_channel<Proto>(resA, resB) ────────────────────────
// ═════════════════════════════════════════════════════════════════
//
// Construct two endpoints of a channel with dual protocols.  The
// duality check is static_assert-enforced: dual(dual(P)) must equal P
// (involution), which verifies the dual_of machinery is correctly
// applied throughout P.

template <typename Proto, typename ResourceA, typename ResourceB>
[[nodiscard]] constexpr auto mint_channel(ResourceA ra, ResourceB rb) noexcept
{
    static_assert(is_well_formed_v<Proto>,
        "crucible::session::diagnostic [Protocol_Ill_Formed]: "
        "proto: protocol is ill-formed.");
    static_assert(is_well_formed_v<dual_of_t<Proto>>,
        "proto: dual protocol is ill-formed — this is a framework bug "
        "(dual_of must preserve well-formedness).");
    static_assert(std::is_same_v<Proto, dual_of_t<dual_of_t<Proto>>>,
        "proto: duality must be involutive (dual(dual(P)) == P).  "
        "If this fires, the framework's dual_of computation is broken.");

    static_assert(SessionResource<ResourceA>,
        "crucible::session::diagnostic [SessionResource_NotPinned]: "
        "mint_channel<Proto, ResourceA, ResourceB>: ResourceA "
        "fails the pin-discipline.  See the SessionResource concept "
        "documentation in Session.h for the allowed shapes.");
    static_assert(SessionResource<ResourceB>,
        "crucible::session::diagnostic [SessionResource_NotPinned]: "
        "mint_channel<Proto, ResourceA, ResourceB>: ResourceB "
        "fails the pin-discipline.  See the SessionResource concept "
        "documentation in Session.h for the allowed shapes.");

    return std::pair{
        mint_session_handle<Proto>(std::move(ra)),
        mint_session_handle<dual_of_t<Proto>>(std::move(rb))
    };
}

// ═════════════════════════════════════════════════════════════════
// ── Framework self-test static_asserts ──────────────────────────
// ═════════════════════════════════════════════════════════════════
//
// These run at header-inclusion time and serve two purposes:
//   1. Document the framework's semantics by example.
//   2. Catch regressions during development — any edit that breaks
//      duality / composition / well-formedness rules fails compile
//      at the offending header's first include.
//
// Gated on CRUCIBLE_SESSION_SELF_TESTS (#372 SEPLOG-PERF-2): every
// TU that includes Session*.h used to pay the compile-time cost of
// ~1000 cumulative static_asserts across the family, even though
// the invariants are stable and only need checking in one place.
// The gate reduces header-include cost by roughly half.  The
// dedicated test/test_session_self_tests.cpp TU defines the macro
// to keep CI coverage intact.

#ifdef CRUCIBLE_SESSION_SELF_TESTS
namespace detail::self_test {

// Duality base cases
static_assert(std::is_same_v<dual_of_t<End>, End>);
static_assert(std::is_same_v<dual_of_t<Continue>, Continue>);
static_assert(std::is_same_v<dual_of_t<Send<int, End>>, Recv<int, End>>);
static_assert(std::is_same_v<dual_of_t<Recv<int, End>>, Send<int, End>>);
static_assert(std::is_same_v<dual_of_t<Select<End, End>>, Offer<End, End>>);
static_assert(std::is_same_v<dual_of_t<Offer<End, End>>, Select<End, End>>);
static_assert(std::is_same_v<dual_of_t<Loop<Send<int, Continue>>>,
                              Loop<Recv<int, Continue>>>);

// Duality involution (dual(dual(P)) == P)
static_assert(std::is_same_v<dual_of_t<dual_of_t<Send<int, End>>>,
                              Send<int, End>>);
static_assert(std::is_same_v<
    dual_of_t<dual_of_t<Loop<Select<Send<int, Continue>, End>>>>,
    Loop<Select<Send<int, Continue>, End>>>);

// Composition
static_assert(std::is_same_v<compose_t<End, Send<int, End>>,
                              Send<int, End>>);
static_assert(std::is_same_v<
    compose_t<Send<int, End>, Recv<bool, End>>,
    Send<int, Recv<bool, End>>>);
static_assert(std::is_same_v<
    compose_t<Send<int, Select<End, End>>, Recv<bool, End>>,
    Send<int, Select<Recv<bool, End>, Recv<bool, End>>>>);
static_assert(std::is_same_v<
    compose_t<Loop<Send<int, Continue>>, Recv<bool, End>>,
    Loop<Send<int, Continue>>>);  // Continue isn't End; loop body stays closed

// Well-formedness
static_assert(is_well_formed_v<End>);
static_assert(is_well_formed_v<Send<int, End>>);
static_assert(is_well_formed_v<Loop<Send<int, Continue>>>);
static_assert(is_well_formed_v<Loop<Select<Send<int, Continue>, End>>>);
static_assert(!is_well_formed_v<Continue>);             // Continue outside Loop
static_assert(!is_well_formed_v<Send<int, Continue>>);  // Continue outside Loop
static_assert(!is_well_formed_v<Select<Continue, End>>);

// Nested loops: Continue binds to the INNERMOST enclosing Loop.
static_assert(is_well_formed_v<Loop<Loop<Send<int, Continue>>>>);

// Shape predicates
static_assert(is_send_v<Send<int, End>>);
static_assert(!is_send_v<Recv<int, End>>);
static_assert(is_loop_v<Loop<End>>);
static_assert(!is_loop_v<End>);

// The MPMC protocol shape is well-formed + involutive under dual.
namespace mpmc_shape_test {
    struct Item {};
    using ProducerP = Loop<Select<Send<Item, Continue>, End>>;
    using ConsumerP = dual_of_t<ProducerP>;
    static_assert(is_well_formed_v<ProducerP>);
    static_assert(is_well_formed_v<ConsumerP>);
    static_assert(std::is_same_v<ConsumerP,
        Loop<Offer<Recv<Item, Continue>, End>>>);
    static_assert(std::is_same_v<dual_of_t<ConsumerP>, ProducerP>);
}

// Request/response server shape
namespace req_resp_test {
    struct Req  {};
    struct Resp {};
    using Server = Loop<Recv<Req, Send<Resp, Continue>>>;
    using Client = dual_of_t<Server>;
    static_assert(std::is_same_v<Client,
        Loop<Send<Req, Recv<Resp, Continue>>>>);
    static_assert(is_well_formed_v<Server>);
    static_assert(is_well_formed_v<Client>);
}

// Two-phase commit coordinator shape
namespace two_pc_test {
    struct Prepare {};
    struct Vote    {};
    struct Commit  {};
    struct Abort   {};
    using Coord = Send<Prepare,
                  Recv<Vote,
                  Select<
                      Send<Commit, End>,
                      Send<Abort,  End>>>>;
    using Follower = dual_of_t<Coord>;
    static_assert(std::is_same_v<Follower,
        Recv<Prepare,
        Send<Vote,
        Offer<
            Recv<Commit, End>,
            Recv<Abort,  End>>>>>);
    static_assert(is_well_formed_v<Coord>);
    static_assert(is_well_formed_v<Follower>);
}

}  // namespace detail::self_test
#endif  // CRUCIBLE_SESSION_SELF_TESTS

// ═════════════════════════════════════════════════════════════════
// ── Release-mode size verification ───────────────────────────────
// ═════════════════════════════════════════════════════════════════
//
// In release builds, the framework's "zero-cost discipline" promise
// requires that SessionHandle adds NO bytes beyond its underlying
// Resource.  Verified via three sizeof checks: SessionHandle wrapping
// a 1-byte Resource is 1 byte; a 4-byte Resource is 4 bytes; an
// 8-byte aligned Resource is 8 bytes.  Each fires only under NDEBUG
// — debug builds carry the consumed_tracker for abandonment detection
// and naturally have larger sizeof.
//
// If these static_asserts FAIL in release, the EBO has not collapsed
// SessionHandleBase as expected — likely cause: compiler does not
// recognise [[no_unique_address]] empty member as a candidate for
// EBO base-collapse.  The fix in that case is a #ifdef NDEBUG/else
// dual implementation of SessionHandleBase (one truly empty, one
// with the bool flag) — kept in reserve as a backup.

#ifdef NDEBUG
namespace detail::release_size_test {
    struct OneByteRes  { char x; };
    struct FourByteRes { int  x; };
    struct EightByteRes { double x; };

    static_assert(sizeof(SessionHandle<End, OneByteRes>) == sizeof(OneByteRes),
        "Release-mode SessionHandle<End, OneByteRes> must equal sizeof(OneByteRes) "
        "— the EBO must collapse SessionHandleBase to zero bytes.  If this fires, "
        "see the comment block above the assert for remediation.");

    static_assert(sizeof(SessionHandle<End, FourByteRes>) == sizeof(FourByteRes),
        "Release-mode SessionHandle<End, FourByteRes> must equal sizeof(FourByteRes).");

    static_assert(sizeof(SessionHandle<End, EightByteRes>) == sizeof(EightByteRes),
        "Release-mode SessionHandle<End, EightByteRes> must equal sizeof(EightByteRes).");

    // Same property holds for non-terminal protocol states — Send, Recv,
    // Select, Offer all derive from SessionHandleBase<Proto> and inherit
    // the EBO collapse.
    static_assert(sizeof(SessionHandle<Send<int, End>, FourByteRes>)
                  == sizeof(FourByteRes),
        "Release-mode SessionHandle<Send, FourByteRes> must equal sizeof(FourByteRes).");
}
#endif  // NDEBUG

}  // namespace crucible::safety::proto
