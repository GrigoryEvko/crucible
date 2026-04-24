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
// static_assert for every protocol encountered by establish_channel.
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
// unconditionally.  This check runs at every establish_channel and
// make_session_handle site; ill-formed protocols fail compilation
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

#include <concepts>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto {

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

// External choice: the PEER picks one of Branches...
template <typename... Branches>
struct Offer {
    static constexpr std::size_t branch_count = sizeof...(Branches);
    using branches_tuple = std::tuple<Branches...>;
};

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

// ═════════════════════════════════════════════════════════════════
// ── Shape traits (is_*_v) ───────────────────────────────────────
// ═════════════════════════════════════════════════════════════════

template <typename P> struct is_send   : std::false_type {};
template <typename T, typename R>
struct is_send<Send<T, R>> : std::true_type {};

template <typename P> struct is_recv   : std::false_type {};
template <typename T, typename R>
struct is_recv<Recv<T, R>> : std::true_type {};

template <typename P> struct is_select : std::false_type {};
template <typename... Bs>
struct is_select<Select<Bs...>> : std::true_type {};

template <typename P> struct is_offer  : std::false_type {};
template <typename... Bs>
struct is_offer<Offer<Bs...>> : std::true_type {};

template <typename P> struct is_loop   : std::false_type {};
template <typename B>
struct is_loop<Loop<B>> : std::true_type {};

template <typename P> struct is_end      : std::bool_constant<std::is_same_v<P, End>>      {};
template <typename P> struct is_continue : std::bool_constant<std::is_same_v<P, Continue>> {};

template <typename P> inline constexpr bool is_send_v     = is_send<P>::value;
template <typename P> inline constexpr bool is_recv_v     = is_recv<P>::value;
template <typename P> inline constexpr bool is_select_v   = is_select<P>::value;
template <typename P> inline constexpr bool is_offer_v    = is_offer<P>::value;
template <typename P> inline constexpr bool is_loop_v     = is_loop<P>::value;
template <typename P> inline constexpr bool is_end_v      = is_end<P>::value;
template <typename P> inline constexpr bool is_continue_v = is_continue<P>::value;

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

template <typename B>
struct dual_of<Loop<B>> {
    using type = Loop<typename dual_of<B>::type>;
};

template <typename P>
using dual_of_t = typename dual_of<P>::type;

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

template <typename B, typename Q>
struct compose<Loop<B>, Q> {
    using type = Loop<typename compose<B, Q>::type>;
};

template <typename P, typename Q>
using compose_t = typename compose<P, Q>::type;

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

template <typename B, typename LoopCtx>
struct is_well_formed<Loop<B>, LoopCtx>
    // Loop<B> introduces itself as the new LoopCtx for checking B.
    : is_well_formed<B, Loop<B>> {};

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
    constexpr void mark() noexcept {}
    constexpr bool was_marked() const noexcept { return true; }
    constexpr void move_from(consumed_tracker&) noexcept {}
};
static_assert(std::is_empty_v<consumed_tracker>,
    "Release-mode consumed_tracker must be std::is_empty_v for EBO.");

#else

class consumed_tracker {
    bool flag_ = false;
public:
    constexpr void mark() noexcept { flag_ = true; }
    constexpr bool was_marked() const noexcept { return flag_; }
    constexpr void move_from(consumed_tracker& other) noexcept {
        flag_ = other.flag_;
        other.flag_ = true;
    }
};

#endif

}  // namespace detail

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

template <typename Proto>
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
    template <typename OtherProto>
    friend class SessionHandleBase;
    template <typename P, typename R, typename L>
    friend class SessionHandle;

public:
    constexpr SessionHandleBase() noexcept = default;

    // Public detach: mark the handle consumed WITHOUT advancing the
    // protocol.  Use ONLY when:
    //
    //   * The protocol is inherently infinite (Loop<X> without a close
    //     branch); the handle's "termination" is implicit via the
    //     transport layer closing the channel below the session-type
    //     abstraction.
    //   * The transport has closed the session out-of-band (peer
    //     crash detected at a lower layer, channel teardown, kernel-
    //     level socket close).
    //   * Test / instrumentation scenarios that intentionally drop
    //     handles at a known-safe point.
    //
    // Grep for `.detach()` to audit all intentional-drop sites; this
    // is the reviewable escape hatch.  The destructor-check still
    // fires for handles that DON'T explicitly detach, catching
    // accidental abandonment.  In RELEASE, detach is a no-op (the
    // destructor check is also no-op'd, so consistency holds).
    constexpr void detach() && noexcept { tracker_.mark(); }

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
    constexpr SessionHandleBase(SessionHandleBase&& other) noexcept
    {
        tracker_.move_from(other.tracker_);
    }

    constexpr SessionHandleBase& operator=(SessionHandleBase&& other) noexcept
    {
        tracker_.move_from(other.tracker_);
        return *this;
    }

    ~SessionHandleBase() {
#ifndef NDEBUG
        if (!tracker_.was_marked() && !is_terminal_state_v<Proto>) {
            std::fprintf(stderr,
                "crucible::safety::proto: SessionHandle abandoned at "
                "non-terminal protocol state.  A mid-protocol handle "
                "was destroyed without being consumed via a &&-qualified "
                "method (close, send, recv, select, pick, branch, "
                "delegate, accept, base, rollback).  Either consume the "
                "handle before its scope exits, or advance the protocol "
                "to End/Stop.\n");
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
[[nodiscard]] constexpr auto step_to_next(Resource r) noexcept
{
    if constexpr (std::is_same_v<R, Continue>) {
        static_assert(!std::is_void_v<LoopCtx>,
            "crucible::session::diagnostic [Continue_Without_Loop]: "
            "proto: Continue appears outside a Loop context.  "
            "Every Continue must have an enclosing Loop<Body>.");
        using NextBody = typename LoopCtx::body;
        // NextBody may itself begin with Loop or Continue (unlikely but
        // syntactically legal) — recurse.
        return step_to_next<NextBody, Resource, LoopCtx>(std::move(r));
    } else if constexpr (is_loop_v<R>) {
        using InnerBody = typename R::body;
        // Enter inner Loop: shadow LoopCtx with the new Loop.  Recurse
        // to handle the case where InnerBody begins with Continue
        // (which would already be ill-formed — caught by is_well_formed).
        return step_to_next<InnerBody, Resource, R>(std::move(r));
    } else {
        static_assert(is_head_v<R>,
            "crucible::session::diagnostic [Protocol_Ill_Formed]: "
            "proto: unexpected protocol shape after resolution.  "
            "Only Send/Recv/Select/Offer/End/Continue are valid heads.");
        return SessionHandle<R, Resource, LoopCtx>{std::move(r)};
    }
}

}  // namespace detail

// ═════════════════════════════════════════════════════════════════
// ── SessionHandle<End, Resource, LoopCtx> — terminal state ──────
// ═════════════════════════════════════════════════════════════════

template <typename Resource, typename LoopCtx>
class [[nodiscard]] SessionHandle<End, Resource, LoopCtx>
    : public SessionHandleBase<End>
{
    Resource resource_;

    template <typename P, typename R, typename L>
    friend class SessionHandle;

    template <typename P, typename R>
    friend constexpr auto make_session_handle(R r) noexcept;

    template <typename R, typename Res, typename L>
    friend constexpr auto detail::step_to_next(Res) noexcept;

public:
    using protocol      = End;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;

    constexpr explicit SessionHandle(Resource r)
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : resource_{std::move(r)} {}

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
    : public SessionHandleBase<Send<T, R>>
{
    Resource resource_;

    template <typename P, typename Res, typename L> friend class SessionHandle;
    template <typename P, typename Res>
    friend constexpr auto make_session_handle(Res) noexcept;
    template <typename U, typename Res, typename L>
    friend constexpr auto detail::step_to_next(Res) noexcept;

public:
    using protocol      = Send<T, R>;
    using message_type  = T;
    using continuation  = R;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;

    constexpr explicit SessionHandle(Resource r)
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : resource_{std::move(r)} {}

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
    : public SessionHandleBase<Recv<T, R>>
{
    Resource resource_;

    template <typename P, typename Res, typename L> friend class SessionHandle;
    template <typename P, typename Res>
    friend constexpr auto make_session_handle(Res) noexcept;
    template <typename U, typename Res, typename L>
    friend constexpr auto detail::step_to_next(Res) noexcept;

public:
    using protocol      = Recv<T, R>;
    using message_type  = T;
    using continuation  = R;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;

    constexpr explicit SessionHandle(Resource r)
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : resource_{std::move(r)} {}

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
    : public SessionHandleBase<Select<Branches...>>
{
    Resource resource_;

    template <typename P, typename Res, typename L> friend class SessionHandle;
    template <typename P, typename Res>
    friend constexpr auto make_session_handle(Res) noexcept;
    template <typename U, typename Res, typename L>
    friend constexpr auto detail::step_to_next(Res) noexcept;

public:
    using protocol       = Select<Branches...>;
    using resource_type  = Resource;
    using loop_ctx       = LoopCtx;

    static constexpr std::size_t branch_count = sizeof...(Branches);

    constexpr explicit SessionHandle(Resource r)
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : resource_{std::move(r)} {}

    constexpr SessionHandle(SessionHandle&&) noexcept            = default;
    constexpr SessionHandle& operator=(SessionHandle&&) noexcept = default;
    ~SessionHandle()                                             = default;

    // Pick branch I and signal the choice to peer via Transport.
    // Transport signature: void(Resource&, std::size_t).
    template <std::size_t I, typename Transport>
        requires (I < sizeof...(Branches))
              && std::is_invocable_v<Transport, Resource&, std::size_t>
    [[nodiscard]] constexpr auto select(Transport transport) &&
        noexcept(std::is_nothrow_invocable_v<Transport, Resource&, std::size_t>
                 && std::is_nothrow_move_constructible_v<Resource>)
    {
        std::invoke(transport, resource_, I);
        this->mark_consumed_();
        using Chosen = std::tuple_element_t<I, std::tuple<Branches...>>;
        return detail::step_to_next<Chosen, Resource, LoopCtx>(std::move(resource_));
    }

    // Convenience: select without an explicit transport — for in-memory
    // channels where the branch choice is implicit in subsequent ops.
    template <std::size_t I>
        requires (I < sizeof...(Branches))
    [[nodiscard]] constexpr auto select() &&
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
    {
        this->mark_consumed_();
        using Chosen = std::tuple_element_t<I, std::tuple<Branches...>>;
        return detail::step_to_next<Chosen, Resource, LoopCtx>(std::move(resource_));
    }

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return resource_; }
};

// ═════════════════════════════════════════════════════════════════
// ── SessionHandle<Offer<Bs…>, …> — external choice ──────────────
// ═════════════════════════════════════════════════════════════════

template <typename... Branches, typename Resource, typename LoopCtx>
class [[nodiscard]] SessionHandle<Offer<Branches...>, Resource, LoopCtx>
    : public SessionHandleBase<Offer<Branches...>>
{
    Resource resource_;

    template <typename P, typename Res, typename L> friend class SessionHandle;
    template <typename P, typename Res>
    friend constexpr auto make_session_handle(Res) noexcept;
    template <typename U, typename Res, typename L>
    friend constexpr auto detail::step_to_next(Res) noexcept;

public:
    using protocol      = Offer<Branches...>;
    using resource_type = Resource;
    using loop_ctx      = LoopCtx;

    static constexpr std::size_t branch_count = sizeof...(Branches);

    constexpr explicit SessionHandle(Resource r)
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : resource_{std::move(r)} {}

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

    // Convenience: pick branch I without an explicit transport — for
    // in-memory channels where the choice is fixed or already known.
    // Useful in unit tests and static pipelines.
    template <std::size_t I>
        requires (I < sizeof...(Branches))
    [[nodiscard]] constexpr auto pick() &&
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
    {
        this->mark_consumed_();
        using Chosen = std::tuple_element_t<I, std::tuple<Branches...>>;
        return detail::step_to_next<Chosen, Resource, LoopCtx>(std::move(resource_));
    }

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
// ── Factory: make_session_handle<Proto>(resource) ───────────────
// ═════════════════════════════════════════════════════════════════
//
// Entry point for constructing a SessionHandle.  If Proto starts with
// Loop, it unrolls one iteration so the returned handle is positioned
// at Loop's body with the Loop itself as the LoopCtx.  Protocols
// starting with any other head go through unchanged.
//
// Compile error if Proto is ill-formed (Continue outside Loop, etc.).

template <typename Proto, typename Resource>
[[nodiscard]] constexpr auto make_session_handle(Resource r) noexcept
{
    static_assert(is_well_formed_v<Proto>,
        "crucible::session::diagnostic [Protocol_Ill_Formed]: "
        "proto: protocol is ill-formed.  Most likely cause: a Continue "
        "appears outside any enclosing Loop<Body>.  Every Continue must "
        "have a Loop above it in the protocol tree.");

    if constexpr (is_loop_v<Proto>) {
        using Body = typename Proto::body;
        // Recursively unroll — Body could itself begin with Loop,
        // though that's unusual.  step_to_next handles the recursion
        // uniformly.
        return detail::step_to_next<Body, Resource, Proto>(std::move(r));
    } else {
        static_assert(!std::is_same_v<Proto, Continue>,
            "crucible::session::diagnostic [Continue_Without_Loop]: "
            "proto: Continue cannot be the top-level protocol.");
        return SessionHandle<Proto, Resource, void>{std::move(r)};
    }
}

// ═════════════════════════════════════════════════════════════════
// ── establish_channel<Proto>(resA, resB) ────────────────────────
// ═════════════════════════════════════════════════════════════════
//
// Construct two endpoints of a channel with dual protocols.  The
// duality check is static_assert-enforced: dual(dual(P)) must equal P
// (involution), which verifies the dual_of machinery is correctly
// applied throughout P.

template <typename Proto, typename ResourceA, typename ResourceB>
[[nodiscard]] constexpr auto establish_channel(ResourceA ra, ResourceB rb) noexcept
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

    return std::pair{
        make_session_handle<Proto>(std::move(ra)),
        make_session_handle<dual_of_t<Proto>>(std::move(rb))
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
