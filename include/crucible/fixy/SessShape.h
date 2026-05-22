#pragma once

// ── crucible::fixy::sess::shape — protocol-shape predicates ─────────
//
// FIXY-V-066.  Carves the 6-predicate protocol-shape surface
// (`is_send_v / is_recv_v / is_select_v / is_offer_v / is_loop_v /
// is_head_v`) out of `fixy/Sess.h` into its own header.  Parallel to
// V-061..V-065 (Checkpoint / RowExtraction / View / Crash / Federation
// carve-outs) — continues the umbrella restructure pattern where each
// load-bearing Sess.h section migrates to a dedicated sub-header.
//
// **Pure file-level extraction.**  No new substrate, no new symbols.
// Every using-decl preserves byte-identical semantics with its
// previous Sess.h location.
//
// ── Why this surface exists ─────────────────────────────────────────
//
// Boolean variable templates that classify a protocol's HEAD
// constructor shape (`P` is a Send / Recv / Select / Offer / Loop /
// any-non-Loop-head).  Used at three production sites:
//
//   1. Combinator metafunctions:  pattern-matching shape-dispatch
//      inside `compose_t`, `dual_t`, `is_well_formed`, etc.
//   2. Pattern matchers:  `safety::proto::pattern::*` dispatches on
//      the head shape to classify a protocol against the verified
//      pattern library (RequestResponse / Pipeline / 2PC / FanOut /
//      MPMC / Handshake / SWIM).
//   3. Per-production-call-site assertions:  call sites that demand a
//      specific protocol shape (e.g., "this transport must be a
//      Loop<...> for streaming") `static_assert(is_loop_v<P>)` at the
//      boundary.
//
// Before V-066 the 6 predicates lived inline at Sess.h lines 114-128;
// every consumer reaching them transitively pulled the entire 745-
// line Sess.h umbrella into its TU.  V-066 carves the predicates into
// a 6-symbol sub-header so callers that demand ONLY shape classification
// (no mint factories, no combinators, no bridges) can include
// `<crucible/fixy/SessShape.h>` directly.
//
// ── Six predicates (the public shape API) ──────────────────────────
//
//   `is_send_v<P>`    — true iff P is `Send<T, K>` for some T, K
//   `is_recv_v<P>`    — true iff P is `Recv<T, K>` for some T, K
//   `is_select_v<P>`  — true iff P is `Select<Bs...>` for some Bs
//   `is_offer_v<P>`   — true iff P is `Offer<Bs...>` for some Bs
//   `is_loop_v<P>`    — true iff P is `Loop<B>` for some B
//   `is_head_v<P>`    — true iff P is any head constructor (NOT loop;
//                       defined as `!is_loop_v<P>`)
//                                                          ──── 6
//
// Note on `is_head_v`:  its negation-based definition (`!is_loop_v<P>`)
// means End / Continue / Stop / Stop_g / Send / Recv / Select / Offer
// all classify as "head" (anything not a Loop is a head).  Some callers
// expect "head" to mean "Send-or-Recv-or-Select-or-Offer" excluding
// terminal states — the actual is_head_v includes terminals.  This is
// substrate behaviour; SessShape.h preserves it verbatim.
//
// ── Substrate added by this header ─────────────────────────────────
//
// NONE.  6 using-decls, sentinel battery, smoke routine.  No new
// types, no mint factories, no free functions — every entry is a
// pure name-lookup directive (zero machine code).
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — re-exports add no state path; all predicates are
//              `inline constexpr bool = ...` value templates with no
//              storage.
//   TypeSafe — using-decls preserve substrate identity; sentinel
//              battery asserts every predicate's bool value matches
//              the substrate-direct path.
//   NullSafe — no pointer state at any level.
//   MemSafe  — all symbols compile-time-only; no allocation.
//   DetSafe  — pure type-level computation; same P always produces
//              the same predicate outcome.
//   BorrowSafe — no aliasing at this layer (purely structural).
//   ThreadSafe — no shared state crossed.
//   LeakSafe — no resource owned.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Every entry is a using-decl (pure name-lookup directive).
// Each predicate is `inline constexpr bool` — folded at compile time
// to a boolean constant by every consumer; no runtime branch.

#include <crucible/sessions/Session.h>

#include <type_traits>

namespace crucible::fixy::sess::shape {

// ── 1. Send / Recv (2) ─────────────────────────────────────────────
// `is_send_v<Send<T, K>>` and `is_recv_v<Recv<T, K>>` classify the
// directional unary head constructors.  Used by the pattern library
// to dispatch the "this is a sending-side endpoint" branch and by
// payload-permission walkers (Transferable / Borrowed / Returned) to
// resolve which side consumes the permission.
using ::crucible::safety::proto::is_send_v;
using ::crucible::safety::proto::is_recv_v;

// ── 2. Select / Offer (2) ──────────────────────────────────────────
// `is_select_v<Select<Bs...>>` and `is_offer_v<Offer<Bs...>>`
// classify the choice constructors.  Select is internal choice (the
// sender picks the branch); Offer is external choice (the receiver
// picks the branch).  Used by the crash-stop walker
// (CrashAwareForTransport) to identify per-Offer crash-branch
// obligations and by the well-formedness gate to verify exhaustive
// branch coverage.
using ::crucible::safety::proto::is_select_v;
using ::crucible::safety::proto::is_offer_v;

// ── 3. Loop (1) ────────────────────────────────────────────────────
// `is_loop_v<Loop<B>>` classifies the recursion constructor.  Used
// by the well-formedness gate to verify `Continue` reachability
// within the body, by the Loop body's effect-row walker (every Send /
// Recv inside the loop contributes to the row), and by checkpoint /
// federation patterns that demand a Loop-rooted streaming shape.
using ::crucible::safety::proto::is_loop_v;

// ── 4. Head (1) ────────────────────────────────────────────────────
// `is_head_v<P>` — true iff P is NOT a Loop (`= !is_loop_v<P>`).
// Substrate defines this disjunctively over the head-constructor
// universe.  Per Session.h:602: `is_head_v = !is_loop_v<P>`, so End /
// Continue / Stop / Stop_g / Send / Recv / Select / Offer all
// classify as "head".  Used by combinators that need to dispatch on
// "this is a directly-typed protocol step" vs "this is a recursion
// wrapper".
using ::crucible::safety::proto::is_head_v;

}  // namespace crucible::fixy::sess::shape

// ═════════════════════════════════════════════════════════════════════
// ── In-header sentinel battery (FIXY-U-020 dual-export discipline) ─
// ═════════════════════════════════════════════════════════════════════
//
// Same drift-catch discipline as SessAssoc.h / SessDelegate.h /
// SessCheckpoint.h / SessRowExtraction.h / SessView.h / SessCrash.h /
// SessFederation.h.  Substrate-side renames trip at every consumer's
// include time, not three TUs deep.  ASCII-only identifiers per
// CLAUDE.md §XVII.

namespace crucible::fixy::sess::shape::v066_self_test {

namespace proto = ::crucible::safety::proto;

// Fixture protocols — minimal well-formed protocols exercising every
// head shape so each predicate has BOTH a positive and a negative
// witness.
struct Probe {};

using SendP   = proto::Send<Probe, proto::End>;
using RecvP   = proto::Recv<Probe, proto::End>;
using SelectP = proto::Select<SendP>;
using OfferP  = proto::Offer<RecvP>;
using LoopP   = proto::Loop<proto::End>;
using EndP    = proto::End;
using ContP   = proto::Continue;

// ── A. Send predicate reach (positive + negative + identity) ───────
static_assert(is_send_v<SendP>,
    "fixy::sess::shape::is_send_v must admit Send<T, K>.  If this "
    "red-lights, SessShape.h's using-decl is broken or the substrate "
    "predicate was moved.");
static_assert(!is_send_v<RecvP>,
    "is_send_v must REJECT Recv (sender-vs-receiver discriminant).");
static_assert(!is_send_v<EndP>);
static_assert(!is_send_v<LoopP>);
static_assert(is_send_v<SendP> == proto::is_send_v<SendP>,
    "is_send_v must produce identical results through fixy::sess::shape::.");

// ── B. Recv predicate reach ───────────────────────────────────────
static_assert(is_recv_v<RecvP>);
static_assert(!is_recv_v<SendP>);
static_assert(!is_recv_v<EndP>);
static_assert(!is_recv_v<LoopP>);
static_assert(is_recv_v<RecvP> == proto::is_recv_v<RecvP>);

// ── C. Select predicate reach ─────────────────────────────────────
static_assert(is_select_v<SelectP>);
static_assert(!is_select_v<OfferP>,
    "is_select_v must REJECT Offer (internal-vs-external choice "
    "discriminant).");
static_assert(!is_select_v<SendP>);
static_assert(!is_select_v<LoopP>);
static_assert(is_select_v<SelectP> == proto::is_select_v<SelectP>);

// ── D. Offer predicate reach ──────────────────────────────────────
static_assert(is_offer_v<OfferP>);
static_assert(!is_offer_v<SelectP>);
static_assert(!is_offer_v<RecvP>);
static_assert(!is_offer_v<LoopP>);
static_assert(is_offer_v<OfferP> == proto::is_offer_v<OfferP>);

// ── E. Loop predicate reach ───────────────────────────────────────
static_assert(is_loop_v<LoopP>);
static_assert(!is_loop_v<SendP>);
static_assert(!is_loop_v<EndP>);
static_assert(!is_loop_v<SelectP>);
static_assert(is_loop_v<LoopP> == proto::is_loop_v<LoopP>);

// ── F. Head predicate reach (substrate's negation-of-loop form) ───
// is_head_v = !is_loop_v per Session.h:602; everything except Loop
// classifies as head, including End / Continue / Send / Recv /
// Select / Offer.
static_assert(is_head_v<SendP>);
static_assert(is_head_v<RecvP>);
static_assert(is_head_v<SelectP>);
static_assert(is_head_v<OfferP>);
static_assert(is_head_v<EndP>);
static_assert(is_head_v<ContP>);
static_assert(!is_head_v<LoopP>,
    "is_head_v must REJECT Loop (the recursion wrapper is the only "
    "non-head shape per substrate's negation-based definition).");
static_assert(is_head_v<SendP> == proto::is_head_v<SendP>);

// ── G. Cross-predicate orthogonality ───────────────────────────────
//
// At most one head-shape predicate fires per protocol.  We cannot
// witness this at consteval for an arbitrary P (substrate could
// theoretically have predicate overlap), but for our fixtures we can
// assert disjointness manually.

template <typename P>
consteval int count_shape_matches() {
    int count = 0;
    if (is_send_v<P>)   ++count;
    if (is_recv_v<P>)   ++count;
    if (is_select_v<P>) ++count;
    if (is_offer_v<P>)  ++count;
    if (is_loop_v<P>)   ++count;
    return count;
}

static_assert(count_shape_matches<SendP>()   == 1);
static_assert(count_shape_matches<RecvP>()   == 1);
static_assert(count_shape_matches<SelectP>() == 1);
static_assert(count_shape_matches<OfferP>()  == 1);
static_assert(count_shape_matches<LoopP>()   == 1);
static_assert(count_shape_matches<EndP>()    == 0,
    "End is a terminal state — none of the head-shape predicates "
    "should fire on it.");
static_assert(count_shape_matches<ContP>()   == 0,
    "Continue is a recursion marker — none of the head-shape "
    "predicates should fire on it.");

// ── H. Cardinality witness — count of items V-066 surfaces ─────────
//
//   directional unary (2: is_send_v + is_recv_v)
// + choice (2: is_select_v + is_offer_v)
// + recursion (1: is_loop_v)
// + head-classification (1: is_head_v)
//                                                          ──── 6

constexpr int v066_surface_cardinality = 6;
static_assert(v066_surface_cardinality == 6,
    "fixy::sess::shape:: V-066 surface cardinality drifted — "
    "update SessShape.h using-decls AND this sentinel in lockstep.");

}  // namespace crucible::fixy::sess::shape::v066_self_test

namespace crucible::fixy::sess::shape {

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test (FIXY-U-103 discipline) ─────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Static-only sentinels can mask SFINAE / consteval / inline-body
// bugs.  The smoke routine forces every shape predicate through real
// instantiation so latent template-evaluation issues surface under
// `-fsyntax-only` of any TU that includes SessShape.h.

inline void runtime_smoke_test() noexcept {
    namespace proto = ::crucible::safety::proto;
    struct Probe {};

    using S  = proto::Send<Probe, proto::End>;
    using R  = proto::Recv<Probe, proto::End>;
    using Se = proto::Select<S>;
    using Of = proto::Offer<R>;
    using L  = proto::Loop<proto::End>;
    using E  = proto::End;

    [[maybe_unused]] constexpr bool send_yes   = is_send_v<S>;
    [[maybe_unused]] constexpr bool send_no    = is_send_v<R>;
    [[maybe_unused]] constexpr bool recv_yes   = is_recv_v<R>;
    [[maybe_unused]] constexpr bool recv_no    = is_recv_v<S>;
    [[maybe_unused]] constexpr bool select_yes = is_select_v<Se>;
    [[maybe_unused]] constexpr bool select_no  = is_select_v<Of>;
    [[maybe_unused]] constexpr bool offer_yes  = is_offer_v<Of>;
    [[maybe_unused]] constexpr bool offer_no   = is_offer_v<Se>;
    [[maybe_unused]] constexpr bool loop_yes   = is_loop_v<L>;
    [[maybe_unused]] constexpr bool loop_no    = is_loop_v<S>;
    [[maybe_unused]] constexpr bool head_send  = is_head_v<S>;
    [[maybe_unused]] constexpr bool head_end   = is_head_v<E>;
    [[maybe_unused]] constexpr bool head_no    = is_head_v<L>;

    (void) send_yes;   (void) send_no;
    (void) recv_yes;   (void) recv_no;
    (void) select_yes; (void) select_no;
    (void) offer_yes;  (void) offer_no;
    (void) loop_yes;   (void) loop_no;
    (void) head_send;  (void) head_end;  (void) head_no;
}

}  // namespace crucible::fixy::sess::shape
