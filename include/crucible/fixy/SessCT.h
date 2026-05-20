#pragma once

// ── crucible::fixy::sess::ct — CTPayload + ct::eq chokepoint ──────
//
// FIXY-U-052b (second slice of U-052 umbrella).  Re-exports the
// session-flavored constant-time discipline surface from
// sessions/SessionCT.h into `crucible::fixy::sess::ct::` so production
// callers can spell CT-required session payloads through the fixy
// umbrella without descending into the substrate.
//
// Ten symbols surfaced (the CTPayload<T> + ct::eq chokepoint layer):
//
//   1. requires_ct<T>                         — primary user-opt-in trait
//      requires_ct_v<T>                       — value alias
//      RequiresCT<T>                          — concept gate
//   2. CTPayload<T>                           — move-only wrapper with
//                                                deleted ==/!= operators
//   3. eq(CTPayload<T>, CTPayload<T>)         — sanctioned comparison
//                                                chokepoint (returns bool)
//   4. is_ct_payload<T>                       — shape trait
//      is_ct_payload_v<T>                     — value alias
//      CTPayloadType<T>                       — concept
//   5. ct_payload_value_type<T>               — metafn extracting inner T
//      ct_payload_value_type_t<T>             — alias (passthrough fallback)
//
// ── Why a dedicated ct:: sub-namespace under fixy::sess:: ─────────
//
// fixy::sess:: holds the binary session-type surface (Send / Recv /
// Loop / Select + the various Handle wrappers); fixy::sess::mpst::
// holds the MPST global-types layer; fixy::sess::declassify:: (U-052a)
// holds the wire-policy payload-marker layer.  CTPayload is a PAYLOAD
// MARKER with stronger discipline than DeclassifyOnSend — it ALSO
// deletes operator== and operator!= so naive byte-content comparison
// won't compile, and ships a sanctioned ct::eq overload as the only
// path that does.  Keeping it in fixy::sess::ct:: lets the audit-grep
// `fixy::sess::ct::` find every fixy-routed CT-payload site distinct
// from:
//   * `fixy::wrap::ct::` — general-purpose CT primitives (mask_from_bit,
//     select, eq(span,span), less, is_zero, cswap), and
//   * `safety::ct::CTPayload<` — substrate-direct sites that bypass
//     the fixy layer entirely.
//
// Substrate-namespace note: SessionCT.h declares into the same
// `crucible::safety::ct::*` namespace as ConstantTime.h.  At the
// substrate level the two surfaces share a namespace but ship
// disjoint symbols.  At the fixy:: layer the audit-discoverability
// concern motivates the split: general-purpose CT primitives route
// through `fixy::wrap::ct::`, session-flavored CT payload discipline
// routes through `fixy::sess::ct::`.  The two are deliberately
// distinct grep targets.
//
// ── User-extensibility of requires_ct (load-bearing footnote) ─────
//
// `requires_ct<T>` is a USER-SPECIALIZED trait: a consumer opts a
// type T into the CT discipline by writing
//
//     template <> struct crucible::safety::ct::requires_ct<MyTag>
//         : std::true_type {};
//
// Specialization MUST occur in the substrate namespace where the
// primary template is declared; `fixy::sess::ct::requires_ct` is
// only a USING-DECL ALIAS for the substrate name and cannot be
// specialized from the fixy:: path.  This mirrors the standard C++
// rule that template specializations must be in the namespace of
// the primary.  Consumers reach the wrapper through
// `fixy::sess::ct::CTPayload<MyTag>`; they specialize the opt-in
// trait at the substrate namespace once per type.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   safety::ct::requires_ct<T>                — primary trait
//   safety::ct::requires_ct_v<T>              — value alias
//   safety::ct::RequiresCT<T>                 — concept
//   safety::ct::CTPayload<T>                  — wrapper
//   safety::ct::eq(CTPayload<T>, CTPayload<T>) — sanctioned overload
//   safety::ct::is_ct_payload<T>              — shape trait
//   safety::ct::is_ct_payload_v<T>            — value alias
//   safety::ct::CTPayloadType<T>              — concept
//   safety::ct::ct_payload_value_type<T>      — metafn
//   safety::ct::ct_payload_value_type_t<T>    — alias
//
// ── Substrate added by this header ─────────────────────────────────
//
// NONE.  Ten using-decls + a sentinel battery + smoke routine.  No
// new types, no new mint factories, no new free functions.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Every entry is a using-decl.  `sizeof(CTPayload<T>) ==
// sizeof(T)` per the substrate's class-layout discipline; the eq
// overload is inline and routes through the existing byte-comparison
// primitive at zero overhead beyond the substrate call.

#include <crucible/sessions/SessionCT.h>

#include <cstddef>
#include <type_traits>

namespace crucible::fixy::sess::ct {

// ═════════════════════════════════════════════════════════════════════
// ── 1. requires_ct family ──────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::safety::ct::requires_ct;
using ::crucible::safety::ct::requires_ct_v;
using ::crucible::safety::ct::RequiresCT;

// ═════════════════════════════════════════════════════════════════════
// ── 2. CTPayload<T> wrapper ────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::safety::ct::CTPayload;

// ═════════════════════════════════════════════════════════════════════
// ── 3. Sanctioned comparison chokepoint ────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// The using-decl brings the entire `safety::ct::eq` overload set
// into `fixy::sess::ct::eq`.  Argument-dependent lookup picks the
// CTPayload<T> overload when both arguments are CTPayload; the
// (span, span) overload from ConstantTime.h is still reachable for
// byte-level comparison but the CTPayload one is the one production
// callers will hit when comparing wrapped payloads.

using ::crucible::safety::ct::eq;

// ═════════════════════════════════════════════════════════════════════
// ── 4. Shape predicates ────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::safety::ct::is_ct_payload;
using ::crucible::safety::ct::is_ct_payload_v;
using ::crucible::safety::ct::CTPayloadType;

// ═════════════════════════════════════════════════════════════════════
// ── 5. Inner-value-type metafunctions (with passthrough fallback) ──
// ═════════════════════════════════════════════════════════════════════

using ::crucible::safety::ct::ct_payload_value_type;
using ::crucible::safety::ct::ct_payload_value_type_t;

// ═════════════════════════════════════════════════════════════════════
// ── In-header sentinel battery ─────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Same dual-export discipline as fixy/SessDecl.h::u052a_self_test +
// fixy/Mpst.h::u013_self_test + fixy/Rules.h::u062_self_test.  Drift
// between the substrate's SessionCT.h surface and the fixy projection
// here trips at every consumer's include time — NOT only inside a
// downstream test TU.

}  // namespace crucible::fixy::sess::ct

// requires_ct specialization MUST happen in the substrate namespace
// where the primary template is declared (standard C++ rule).  The
// fixy sentinel below opts in a self-test type by re-opening the
// substrate namespace; consumers must do the same when registering
// their own CT-required types.

namespace crucible::fixy::sess::ct::u052b_self_test {

// ── A. Payload placeholders (trivially-copyable POD by design) ─────
struct TokenA {
    std::byte bytes[16]{};
};
struct TokenB {
    std::byte bytes[16]{};
};
struct NotCt {  // not opted into requires_ct — used to prove rejection
    std::byte bytes[16]{};
};

}  // namespace crucible::fixy::sess::ct::u052b_self_test

// Opt the placeholders into the CT discipline.  Specialization site
// is the substrate namespace, not the fixy alias.
namespace crucible::safety::ct {

template <>
struct requires_ct<::crucible::fixy::sess::ct::u052b_self_test::TokenA>
    : std::true_type {};

template <>
struct requires_ct<::crucible::fixy::sess::ct::u052b_self_test::TokenB>
    : std::true_type {};

}  // namespace crucible::safety::ct

namespace crucible::fixy::sess::ct::u052b_self_test {

using TokenAPayload = CTPayload<TokenA>;
using TokenBPayload = CTPayload<TokenB>;

// ── B. Re-export type-identity witness (dual-export sentinel) ──────
//
// Every using-decl preserves the substrate type identity bit-for-bit.
// If a substrate symbol is moved out from under us (renamed, removed,
// or relocated to a different namespace), the next lines fail to
// compile with a recognisable substrate-rename diagnostic.

static_assert(std::is_same_v<
    CTPayload<TokenA>,
    ::crucible::safety::ct::CTPayload<TokenA>>,
    "fixy::sess::ct::CTPayload must alias safety::ct::CTPayload.");

static_assert(std::is_same_v<
    requires_ct<TokenA>,
    ::crucible::safety::ct::requires_ct<TokenA>>,
    "fixy::sess::ct::requires_ct must alias safety::ct::requires_ct.");

// ── C. requires_ct trait fires for opted-in types ──────────────────

static_assert( requires_ct_v<TokenA>);
static_assert( requires_ct_v<TokenB>);
static_assert(!requires_ct_v<NotCt>);
static_assert(!requires_ct_v<int>);

// RequiresCT also requires trivial copyability — sanity-check that
// the concept matches the trait + triviality conjunction.
static_assert( RequiresCT<TokenA>);
static_assert(!RequiresCT<NotCt>);   // trait not opted in
static_assert(!RequiresCT<int>);     // trait not opted in

// ── D. is_ct_payload shape predicates discriminate wrapper vs raw ─

static_assert( is_ct_payload_v<TokenAPayload>);
static_assert( is_ct_payload_v<TokenBPayload>);
static_assert(!is_ct_payload_v<TokenA>);       // bare payload, not wrapper
static_assert(!is_ct_payload_v<int>);
static_assert(!is_ct_payload_v<NotCt>);

static_assert( CTPayloadType<TokenAPayload>);
static_assert(!CTPayloadType<TokenA>);

// ── E. ct_payload_value_type_t extracts inner T (with passthrough) ─
//
// The metafunction has a passthrough fallback that returns non-
// CTPayload types unchanged (so generic transport code can use it
// uniformly).  Both branches must reach through the umbrella.

static_assert(std::is_same_v<ct_payload_value_type_t<TokenAPayload>, TokenA>);
static_assert(std::is_same_v<ct_payload_value_type_t<TokenBPayload>, TokenB>);
static_assert(std::is_same_v<ct_payload_value_type_t<TokenA>,        TokenA>);
static_assert(std::is_same_v<ct_payload_value_type_t<int>,           int>);

// ── F. eq overload-set reach (consteval-only shape check) ──────────
//
// The runtime smoke routine exercises the actual eq() call against
// non-constant payloads.  Here we just witness that the overload is
// reachable as a non-deleted, callable expression on CTPayload pairs.

template <typename A, typename B>
concept has_fixy_ct_eq = requires(A const& a, B const& b) {
    { eq(a, b) } -> std::same_as<bool>;
};

static_assert( has_fixy_ct_eq<TokenAPayload, TokenAPayload>);

// Cross-T comparison must NOT compile — eq is constrained to
// matching T on both sides.  This is the substrate's safety net
// against accidentally comparing payloads of different families.
static_assert(!has_fixy_ct_eq<TokenAPayload, TokenBPayload>);

// ── G. Distinct-T wrapper distinctness ─────────────────────────────
//
// CTPayload<TokenA> and CTPayload<TokenB> are unrelated wrapper types
// — the T parameter is part of the wrapper identity, so accidentally
// flowing TokenAPayload to a TokenBPayload-position would defeat the
// audit-discoverability of `grep CTPayload<T>` against a specific T.
// Pinned at the fixy layer so a future substrate refactor that
// accidentally unified them breaks this header BEFORE the diagnostic
// propagates into Send/Recv call sites.

static_assert(!std::is_same_v<TokenAPayload, TokenBPayload>);

// ── H. Move-only discipline + deleted == / != preserved ────────────
//
// CTPayload is move-only by design; copying classified bytes would
// silently duplicate them and defeat the security promise.  Both
// operator== and operator!= are explicitly deleted to force every
// comparison through ct::eq — the only path that runs in constant
// time.  If a future refactor accidentally restored either, this
// header breaks BEFORE production code starts leaking timing
// side-channels.

static_assert(!std::is_copy_constructible_v<TokenAPayload>);
static_assert(!std::is_copy_assignable_v<TokenAPayload>);
static_assert( std::is_move_constructible_v<TokenAPayload>);
static_assert( std::is_move_assignable_v<TokenAPayload>);

template <typename A, typename B>
concept has_operator_eq = requires(A const& a, B const& b) { a == b; };
template <typename A, typename B>
concept has_operator_neq = requires(A const& a, B const& b) { a != b; };

static_assert(!has_operator_eq <TokenAPayload, TokenAPayload>);
static_assert(!has_operator_neq<TokenAPayload, TokenAPayload>);

// Zero-cost size guarantee — wrapper is layout-identical to T.
static_assert(sizeof(TokenAPayload) == sizeof(TokenA));

// ── I. Cardinality witness — count of items U-052b surfaces.
//
//   Trait family (3):
//     requires_ct, requires_ct_v, RequiresCT
//   Wrapper (1):
//     CTPayload
//   Sanctioned chokepoint (1):
//     eq (the CTPayload-pair overload routed via using-decl)
//   Shape predicates (3):
//     is_ct_payload, is_ct_payload_v, CTPayloadType
//   Inner-value-type metafns (2):
//     ct_payload_value_type, ct_payload_value_type_t
//                                                       ────
//                                                        10
constexpr int u052b_surface_cardinality = 10;
static_assert(u052b_surface_cardinality == 10,
    "fixy::sess::ct:: U-052b surface cardinality drifted — update "
    "SessCT.h using-decls AND this sentinel in lockstep.");

}  // namespace crucible::fixy::sess::ct::u052b_self_test

namespace crucible::fixy::sess::ct {

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test (FIXY-U-103 discipline) ─────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Static-only sentinels can mask SFINAE / consteval / inline-body
// bugs.  The runtime smoke routine instantiates every public template
// against non-constant args so any latent template-evaluation issue
// surfaces under `-fsyntax-only` of any TU that includes SessCT.h.
//
// Cost: instantiations only.  No runtime code path is executed beyond
// a single eq() call that the optimizer trivially constant-folds.

inline void runtime_smoke_test() noexcept {
    using ::crucible::fixy::sess::ct::u052b_self_test::TokenA;
    using P = CTPayload<TokenA>;

    [[maybe_unused]] constexpr bool isP    = is_ct_payload_v<P>;
    [[maybe_unused]] constexpr bool notP   = is_ct_payload_v<int>;
    [[maybe_unused]] constexpr bool cap    = CTPayloadType<P>;
    [[maybe_unused]] constexpr bool trait  = requires_ct_v<TokenA>;
    [[maybe_unused]] constexpr bool concpt = RequiresCT<TokenA>;

    using InnerT = ct_payload_value_type_t<P>;
    using PassT  = ct_payload_value_type_t<int>;

    // Exercise the eq chokepoint against runtime values.  TokenA is
    // trivially copyable and zero-initialized; eq returns true on
    // identical payloads.  noexcept route — runtime cost is the
    // underlying ct::eq byte-compare.
    P lhs{TokenA{}};
    P rhs{TokenA{}};
    [[maybe_unused]] bool equal = eq(lhs, rhs);

    (void) isP; (void) notP; (void) cap; (void) trait; (void) concpt;
    (void) static_cast<InnerT*>(nullptr);
    (void) static_cast<PassT*>(nullptr);
    (void) equal;
}

}  // namespace crucible::fixy::sess::ct
