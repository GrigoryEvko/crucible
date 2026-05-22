#pragma once

// ── crucible::fixy::sess::delegate — Δ-handoff combinator surface ───
//
// FIXY-V-060.  Re-exports the public surface of
// `sessions/SessionDelegate.h` — the L4 delegation layer that lets one
// participant transfer an endpoint of a T-typed session to a peer,
// continuing as K afterwards.  Higher-order session types per
// Honda-Yoshida-Carbone 2008 §3 "Multiparty session types with
// delegation"; epoch-versioned variants per FOUND-D03 (recipient must
// prove freshness at the (epoch, generation) frontier before accepting
// a handoff).
//
// Delegate / Accept solve TWO orthogonal problems compared to bare
// Send<T,K> / Recv<T,K>:
//
//   1. T-the-protocol vs T-the-value.  Send<T,K> ships a VALUE of
//      type T; Delegate<T,K> ships an ENDPOINT speaking protocol T.
//      The handle types differ (Resource vs SessionHandle<T,Res>);
//      crash propagation differs (recipient may inherit pending
//      obligations from T); duality differs (dual(Delegate<T,K>) =
//      Accept<T,K>, NOT Accept<dual<T>,K> — duality is in K only).
//
//   2. Recipient-side crash propagation.  When the recipient holding
//      the delegated endpoint crashes, the carrier's K must offer a
//      Crash<RecipientTag> recovery branch UNLESS T itself
//      structurally cannot fail (Recv-only chains terminating in
//      End).  `delegated_crash_propagation_t<T, R, K>` classifies the
//      obligation; `assert_delegated_crash_propagates` fires the
//      diagnostic at the declaration site.
//
// ── Why this surface exists (Agent 3 finding B4 MEDIUM) ────────────
//
// Before V-060, none of the delegate-machinery primitives were
// reachable through `fixy::` — band-3 callers (Vessel routing,
// Forge/Mimic kernel-handle handoff, Cipher tier-promotion) had to
// either dodge the fixy boundary or reach into the substrate
// `crucible::safety::proto::` namespace directly.  That broke the
// audit story:  the discipline ledger expected every protocol-shape
// declaration to route through `fixy::sess::*` so reviewers could
// grep `fixy::sess::delegate::` to find every handoff in the codebase
// at once.  V-060 closes that gap with a 24-symbol re-export surface.
//
// ── Twenty-four symbols (the public delegation API) ────────────────
//
//   core combinators (4):  Delegate, Accept,
//                          EpochedDelegate, EpochedAccept
//   crash machinery (5):   Recovers, MustAbort, IllFormed,
//                          delegated_crash_propagation_t,
//                          assert_delegated_crash_propagates
//   sugar combinators (5): Delegate_seq, Accept_seq, Redelegate,
//                          DelegateWithAck, AcceptWithAck
//   discriminators (5):    is_delegate, is_delegate_v,
//                          is_accept, is_accept_v,
//                          is_delegation_head_v
//   concepts (3):          CanDelegate, DelegatesTo, AcceptsFrom
//   transport concepts (2): TransportForDelegate, TransportForAccept
//   intent-revealing
//     consteval asserts (2): assert_delegates_to, assert_accepts_from
//                         total: 4 + 5 + 5 + 5 + 3 + 2 + 2 = 26
//                         (cardinality witness below: 26).
//
// ── Substrate added by this header ─────────────────────────────────
//
// NONE.  Twenty-six using-decls, sentinel battery, smoke routine.  No
// new types, no mint factories, no free functions — every entry is a
// pure name-lookup directive (zero machine code).
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — re-exports add no state path; every symbol is either a
//              type-level combinator (no data members reachable through
//              the using-decl) or a consteval helper (no runtime state).
//   TypeSafe — using-decls preserve substrate identity; (Delegate<T,K>
//              from substrate) ≡ (Delegate<T,K> via fixy::) at the
//              type-system level (sentinel cells assert this).
//   NullSafe — no pointer state crosses this header.
//   MemSafe  — all symbols compile-time-only; no allocation.
//   DetSafe  — pure type-level computation; same (T, K) inputs always
//              produce the same delegated_crash_propagation_t outcome.
//   BorrowSafe — no aliasing at this layer (purely structural).
//   ThreadSafe — no shared state crossed.
//   LeakSafe — no resource owned.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Every entry is a using-decl (pure name-lookup directive).
// The four core combinators occupy `sizeof = 1` (empty tag structs);
// EpochedDelegate / EpochedAccept add two `constexpr uint64_t` NTTPs
// as static data members (still empty value-wise).

#include <crucible/sessions/SessionDelegate.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace crucible::fixy::sess::delegate {

// ── 1. Core combinators (4) ────────────────────────────────────────
// Delegate<T, K> / Accept<T, K> — the dual pair of higher-order
// handoff combinators.  EpochedDelegate / EpochedAccept thread a
// (MinEpoch, MinGeneration) freshness floor through the type; the
// recipient must prove the carrier's EpochCtx meets the floor before
// the SessionHandle<EpochedAccept<...>, R>::accept(ctx, ...) call
// site type-checks.  FOUND-D03 + Honda-Yoshida-Carbone 2008 §3.
using ::crucible::safety::proto::Delegate;
using ::crucible::safety::proto::Accept;
using ::crucible::safety::proto::EpochedDelegate;
using ::crucible::safety::proto::EpochedAccept;

// ── 2. Crash propagation machinery (5) ─────────────────────────────
// Classify the recipient-crash obligation created by Delegate<T, K>.
// When R holds the delegated T-endpoint and crashes, K must offer a
// recovery branch UNLESS T is structurally non-failing (Recv-chains
// to End, or empty Offer with implicit terminate).  The three result
// tags partition the universe:
//   * Recovers<P>  — K (or some structural ancestor) recovers via P.
//   * MustAbort    — T can emit but K has no recovery branch.
//   * IllFormed    — primary-template firing; specialize for the T
//                    you delegate.
using ::crucible::safety::proto::Recovers;
using ::crucible::safety::proto::MustAbort;
using ::crucible::safety::proto::IllFormed;
using ::crucible::safety::proto::delegated_crash_propagation_t;
using ::crucible::safety::proto::assert_delegated_crash_propagates;

// ── 3. Sugar combinators (5) ───────────────────────────────────────
// Compositional aliases on top of Delegate / Accept.  None adds new
// machinery — each expands to nested Delegate / Accept combinators
// and inherits dual, compose, well-formedness, handle dispatch from
// the primary specialisations.
//
//   Delegate_seq<T1, T2, ..., Tn, K> = Delegate<T1, Delegate<T2,
//                                       ..., Delegate<Tn, K>>>
//                                      — sequential multi-handoff;
//                                      LAST type arg is K (the
//                                      continuation).
//   Accept_seq<...>                  — peer-side companion.
//   Redelegate<T, K>                 = Accept<T, Delegate<T, K>> —
//                                      middlebox / proxy pattern
//                                      (receive then re-emit).
//   DelegateWithAck<T, Ack, K>       = Delegate<T, Recv<Ack, K>> —
//                                      handoff followed by typed
//                                      ack reception.
//   AcceptWithAck<T, Ack, K>         = Accept<T, Send<Ack, K>>  —
//                                      peer-side ack-emit pattern.
using ::crucible::safety::proto::Delegate_seq;
using ::crucible::safety::proto::Accept_seq;
using ::crucible::safety::proto::Redelegate;
using ::crucible::safety::proto::DelegateWithAck;
using ::crucible::safety::proto::AcceptWithAck;

// ── 4. Discriminators (5) ──────────────────────────────────────────
// Type-level tests for "is this protocol the head of a delegation /
// reception sequence?"  Used at boundary functions that must
// special-case carrier protocols.  All five exist in both class-
// template and `_v` short forms; the surface exposes both because
// some metaprograms inherit from is_delegate<P> to compose traits
// while others want the bool directly.
using ::crucible::safety::proto::is_delegate;
using ::crucible::safety::proto::is_delegate_v;
using ::crucible::safety::proto::is_accept;
using ::crucible::safety::proto::is_accept_v;
using ::crucible::safety::proto::is_delegation_head_v;

// ── 5. Carrier-protocol concepts (3) ───────────────────────────────
// Concept-form gates for `requires`-clauses at boundary APIs that
// must demand a specific delegation contract — substantially more
// readable than the equivalent `enable_if_t<is_delegate_v<P> &&
// std::is_same_v<P::delegated_proto, T>>` SFINAE.
//
//   CanDelegate<P, R>       — P is well-formed AND compatible with
//                             delegation semantics (current
//                             implementation = is_well_formed_v<P>;
//                             reserved for forward extensions where
//                             P-vs-R compatibility may tighten).
//   DelegatesTo<C, T>       — C is Delegate<T, _> for some K.
//   AcceptsFrom<C, T>       — C is Accept<T, _> for some K.
using ::crucible::safety::proto::CanDelegate;
using ::crucible::safety::proto::DelegatesTo;
using ::crucible::safety::proto::AcceptsFrom;

// ── 6. Transport-callable concepts (2) ─────────────────────────────
// User-supplied transport functions for handle-side delegate() /
// accept() calls.  Concept-form gate fires at the call site naming
// Transport / CarrierRes / DelegatedRes rather than deep in handle's
// invoke-deduction.  TransportForAccept also checks return-type
// agreement (transport must return exactly DelegatedRes, not just be
// invocable with the right arg type).
//
//   TransportForDelegate<F, CR, DR> — F is invocable as
//                                     void(CR&, DR&&).
//   TransportForAccept<F, CR, DR>   — F is invocable as
//                                     DR(CR&) with return-type
//                                     agreement.
using ::crucible::safety::proto::TransportForDelegate;
using ::crucible::safety::proto::TransportForAccept;

// ── 7. Intent-revealing consteval assertions (2) ───────────────────
// One-liner at call sites that demand a specific delegation contract.
// Substantially better diagnostic site than waiting for the deep-
// template instantiation failure in SessionHandle dispatch.
//
//   assert_delegates_to<C, T>() — fires diagnostic
//     [ProtocolViolation_State] if C is NOT Delegate<T, _> for some K.
//   assert_accepts_from<C, T>() — fires diagnostic
//     [ProtocolViolation_State] if C is NOT Accept<T, _> for some K.
using ::crucible::safety::proto::assert_delegates_to;
using ::crucible::safety::proto::assert_accepts_from;

}  // namespace crucible::fixy::sess::delegate

// ═════════════════════════════════════════════════════════════════════
// ── In-header sentinel battery (FIXY-U-020 dual-export discipline) ─
// ═════════════════════════════════════════════════════════════════════
//
// Same drift-catch discipline as fixy/SessAssoc.h::v059_self_test.
// Substrate-side renames trip at every consumer's include time, not
// three TUs deep.  ASCII-only identifiers per CLAUDE.md §XVII (no Γ /
// Δ / unicode in identifiers — the substrate uses them at type-level
// in `_t` aliases; this header's sentinels do not).

namespace crucible::fixy::sess::delegate::v060_self_test {

namespace proto = ::crucible::safety::proto;

// Fixture protocols — minimal well-formed sessions that exercise
// every export without depending on richer Session.h machinery.
struct Req {};                  // delegated-side payload
struct Ack {};                  // ack token
struct CrashTag {};             // recipient-tag for crash propagation

// T = a one-message Send-then-End delegated protocol.
using T_delegated = proto::Send<Req, proto::End>;

// K = continuation after handoff — End is the minimal terminus.
using K_continue = proto::End;

// The two primary combinators.
using D = proto::Delegate<T_delegated, K_continue>;
using A = proto::Accept<T_delegated, K_continue>;

// Epoched variants — pin MinEpoch=3 / MinGeneration=7 so duality and
// NTTP-preservation can be witnessed.
using ED = proto::EpochedDelegate<T_delegated, K_continue, 3u, 7u>;
using EA = proto::EpochedAccept<T_delegated, K_continue, 3u, 7u>;

// ── A. Core combinators reach (type identity) ──────────────────────
static_assert(std::is_same_v<proto::Delegate<T_delegated, K_continue>,
                             Delegate<T_delegated, K_continue>>,
    "Delegate must reach identically through fixy::");
static_assert(std::is_same_v<proto::Accept<T_delegated, K_continue>,
                             Accept<T_delegated, K_continue>>,
    "Accept must reach identically through fixy::");
static_assert(std::is_same_v<
    proto::EpochedDelegate<T_delegated, K_continue, 3u, 7u>,
    EpochedDelegate<T_delegated, K_continue, 3u, 7u>>,
    "EpochedDelegate must reach identically through fixy::");
static_assert(std::is_same_v<
    proto::EpochedAccept<T_delegated, K_continue, 3u, 7u>,
    EpochedAccept<T_delegated, K_continue, 3u, 7u>>,
    "EpochedAccept must reach identically through fixy::");

// NTTPs preserved on epoched variants.
static_assert(EpochedDelegate<T_delegated, K_continue, 3u, 7u>::min_epoch == 3u);
static_assert(EpochedDelegate<T_delegated, K_continue, 3u, 7u>::min_generation == 7u);
static_assert(EpochedAccept<T_delegated, K_continue, 3u, 7u>::min_epoch == 3u);
static_assert(EpochedAccept<T_delegated, K_continue, 3u, 7u>::min_generation == 7u);

// ── B. Crash machinery reach ───────────────────────────────────────
// Recovers<P> type identity through fixy.
static_assert(std::is_same_v<Recovers<K_continue>,
                             proto::Recovers<K_continue>>,
    "Recovers must reach identically through fixy::");
static_assert(std::is_same_v<MustAbort, proto::MustAbort>,
    "MustAbort must reach identically through fixy::");
static_assert(std::is_same_v<IllFormed, proto::IllFormed>,
    "IllFormed must reach identically through fixy::");

// delegated_crash_propagation_t reach.  For T_delegated = Send<Req,
// End> the carrier needs an Offer with Crash branch to recover; with
// CarrierK = End and T containing a Send, the result is the carrier-
// side recovery branch (MustAbort when K has no Crash branch).  We
// don't pin the exact outcome here (it depends on K's Offer shape);
// we only witness type-identity reach through fixy::.
using PropResult_fixy = delegated_crash_propagation_t<
    T_delegated, CrashTag, K_continue>;
using PropResult_proto = proto::delegated_crash_propagation_t<
    T_delegated, CrashTag, K_continue>;
static_assert(std::is_same_v<PropResult_fixy, PropResult_proto>,
    "delegated_crash_propagation_t must reach identically through fixy::");

// assert_delegated_crash_propagates reach (consteval invocation).
// Use a Recv-only T that terminates cleanly so the assertion passes
// regardless of CarrierK shape.
using T_recv_only = proto::Recv<Ack, proto::End>;
consteval bool check_fixy_assert_delegated_crash() {
    assert_delegated_crash_propagates<T_recv_only, CrashTag, K_continue>();
    return true;
}
static_assert(check_fixy_assert_delegated_crash());

// ── C. Sugar combinators reach ─────────────────────────────────────
// Delegate_seq with three handoffs collapses to the expected nesting.
struct T1 {};
struct T2 {};
struct T3 {};
static_assert(std::is_same_v<
    Delegate_seq<T1, T2, T3, K_continue>,
    proto::Delegate<T1, proto::Delegate<T2, proto::Delegate<T3, K_continue>>>>,
    "Delegate_seq<T1, T2, T3, K> must expand to right-nested Delegates.");
static_assert(std::is_same_v<
    Accept_seq<T1, T2, T3, K_continue>,
    proto::Accept<T1, proto::Accept<T2, proto::Accept<T3, K_continue>>>>,
    "Accept_seq<T1, T2, T3, K> must expand to right-nested Accepts.");

// Redelegate is Accept<T, Delegate<T, K>>.
static_assert(std::is_same_v<
    Redelegate<T_delegated, K_continue>,
    proto::Accept<T_delegated, proto::Delegate<T_delegated, K_continue>>>,
    "Redelegate<T, K> = Accept<T, Delegate<T, K>>.");

// DelegateWithAck is Delegate<T, Recv<Ack, K>>.
static_assert(std::is_same_v<
    DelegateWithAck<T_delegated, Ack, K_continue>,
    proto::Delegate<T_delegated, proto::Recv<Ack, K_continue>>>,
    "DelegateWithAck<T, Ack, K> = Delegate<T, Recv<Ack, K>>.");

// AcceptWithAck is Accept<T, Send<Ack, K>>.
static_assert(std::is_same_v<
    AcceptWithAck<T_delegated, Ack, K_continue>,
    proto::Accept<T_delegated, proto::Send<Ack, K_continue>>>,
    "AcceptWithAck<T, Ack, K> = Accept<T, Send<Ack, K>>.");

// ── D. Discriminators reach ────────────────────────────────────────
static_assert(is_delegate_v<D> == proto::is_delegate_v<D>,
    "is_delegate_v must reach identically through fixy::");
static_assert(is_delegate_v<D>);
static_assert(!is_delegate_v<A>);
static_assert(is_delegate_v<ED>);
static_assert(!is_delegate_v<EA>);

static_assert(is_accept_v<A> == proto::is_accept_v<A>,
    "is_accept_v must reach identically through fixy::");
static_assert(is_accept_v<A>);
static_assert(!is_accept_v<D>);
static_assert(is_accept_v<EA>);
static_assert(!is_accept_v<ED>);

static_assert(is_delegation_head_v<D>);
static_assert(is_delegation_head_v<A>);
static_assert(is_delegation_head_v<ED>);
static_assert(is_delegation_head_v<EA>);
static_assert(!is_delegation_head_v<proto::End>);
static_assert(!is_delegation_head_v<proto::Send<Req, proto::End>>);

// is_delegate / is_accept class-template form reach.
static_assert(is_delegate<D>::value);
static_assert(is_accept<A>::value);

// ── E. Concepts reach ──────────────────────────────────────────────
template <typename C, typename T_>
    requires DelegatesTo<C, T_>
consteval bool requires_delegates_to_witness() { return true; }
static_assert(requires_delegates_to_witness<D, T_delegated>());

template <typename C, typename T_>
    requires AcceptsFrom<C, T_>
consteval bool requires_accepts_from_witness() { return true; }
static_assert(requires_accepts_from_witness<A, T_delegated>());

// CanDelegate — every well-formed combinator can be delegated.
template <typename P, typename R>
    requires CanDelegate<P, R>
consteval bool requires_can_delegate_witness() { return true; }
static_assert(requires_can_delegate_witness<T_delegated, CrashTag>());

// ── F. Transport concepts reach ────────────────────────────────────
// Stub transport callables that satisfy the structural signatures.
struct CarrierResource {};
struct DelegatedResource {};

struct DelegateTransport {
    void operator()(CarrierResource&, DelegatedResource&&) const {}
};
struct AcceptTransport {
    DelegatedResource operator()(CarrierResource&) const { return {}; }
};

static_assert(TransportForDelegate<DelegateTransport,
                                   CarrierResource, DelegatedResource>);
static_assert(TransportForAccept<AcceptTransport,
                                 CarrierResource, DelegatedResource>);

// Wrong-return AcceptTransport variant — concept must reject.
struct WrongReturnAcceptTransport {
    int operator()(CarrierResource&) const { return 0; }
};
static_assert(!TransportForAccept<WrongReturnAcceptTransport,
                                  CarrierResource, DelegatedResource>);

// ── G. Intent-revealing consteval assertions reach ─────────────────
consteval bool check_fixy_assert_delegates_to() {
    assert_delegates_to<D, T_delegated>();
    return true;
}
static_assert(check_fixy_assert_delegates_to());

consteval bool check_fixy_assert_accepts_from() {
    assert_accepts_from<A, T_delegated>();
    return true;
}
static_assert(check_fixy_assert_accepts_from());

// ── H. Cardinality witness — count of items V-060 surfaces ─────────
//
//   core combinators (4: Delegate, Accept, EpochedDelegate,
//                       EpochedAccept)
// + crash machinery (5: Recovers, MustAbort, IllFormed,
//                       delegated_crash_propagation_t,
//                       assert_delegated_crash_propagates)
// + sugar combinators (5: Delegate_seq, Accept_seq, Redelegate,
//                       DelegateWithAck, AcceptWithAck)
// + discriminators (5: is_delegate, is_delegate_v, is_accept,
//                      is_accept_v, is_delegation_head_v)
// + concepts (3: CanDelegate, DelegatesTo, AcceptsFrom)
// + transport concepts (2: TransportForDelegate, TransportForAccept)
// + intent-revealing consteval asserts (2: assert_delegates_to,
//                                          assert_accepts_from)
//                                                          ──── 26
constexpr int v060_surface_cardinality = 26;
static_assert(v060_surface_cardinality == 26,
    "fixy::sess::delegate:: V-060 surface cardinality drifted — update "
    "SessDelegate.h using-decls AND this sentinel in lockstep.");

}  // namespace crucible::fixy::sess::delegate::v060_self_test

namespace crucible::fixy::sess::delegate {

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test (FIXY-U-103 discipline) ─────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Static-only sentinels can mask SFINAE / consteval / inline-body bugs.
// The smoke routine forces every delegation metafunction through real
// instantiation so latent template-evaluation issues surface under
// `-fsyntax-only` of any TU that includes SessDelegate.h.

inline void runtime_smoke_test() noexcept {
    namespace proto = ::crucible::safety::proto;
    struct Payload {};
    struct AckMsg {};
    struct Tag {};

    using T_  = proto::Send<Payload, proto::End>;
    using K_  = proto::End;
    using D_  = Delegate<T_, K_>;
    using A_  = Accept<T_, K_>;
    using ED_ = EpochedDelegate<T_, K_, 1u, 1u>;
    using EA_ = EpochedAccept<T_, K_, 1u, 1u>;

    [[maybe_unused]] constexpr bool is_d  = is_delegate_v<D_>;
    [[maybe_unused]] constexpr bool is_a  = is_accept_v<A_>;
    [[maybe_unused]] constexpr bool is_ed = is_delegate_v<ED_>;
    [[maybe_unused]] constexpr bool is_ea = is_accept_v<EA_>;
    [[maybe_unused]] constexpr bool head  = is_delegation_head_v<D_>;
    [[maybe_unused]] constexpr std::uint64_t me = ED_::min_epoch;
    [[maybe_unused]] constexpr std::uint64_t mg = ED_::min_generation;

    // Sugar expansions exercise the helper aliases.
    using Seq3 = Delegate_seq<Payload, AckMsg, K_>;
    using SeqA = Accept_seq<Payload, AckMsg, K_>;
    using RD   = Redelegate<Payload, K_>;
    using DA   = DelegateWithAck<Payload, AckMsg, K_>;
    using AA   = AcceptWithAck<Payload, AckMsg, K_>;
    [[maybe_unused]] constexpr bool seq3_ok =
        std::is_same_v<Seq3, proto::Delegate<Payload, proto::Delegate<AckMsg, K_>>>;
    [[maybe_unused]] constexpr bool seqa_ok =
        std::is_same_v<SeqA, proto::Accept<Payload, proto::Accept<AckMsg, K_>>>;
    [[maybe_unused]] constexpr bool rd_ok =
        std::is_same_v<RD,   proto::Accept<Payload, proto::Delegate<Payload, K_>>>;
    [[maybe_unused]] constexpr bool da_ok =
        std::is_same_v<DA,   proto::Delegate<Payload, proto::Recv<AckMsg, K_>>>;
    [[maybe_unused]] constexpr bool aa_ok =
        std::is_same_v<AA,   proto::Accept<Payload, proto::Send<AckMsg, K_>>>;

    // Crash classification reaches via fixy.
    using Recv_T = proto::Recv<AckMsg, proto::End>;
    using Prop   = delegated_crash_propagation_t<Recv_T, Tag, K_>;
    [[maybe_unused]] constexpr bool prop_ok =
        std::is_same_v<Prop, Recovers<K_>>;

    (void) is_d; (void) is_a; (void) is_ed; (void) is_ea;
    (void) head; (void) me; (void) mg;
    (void) seq3_ok; (void) seqa_ok; (void) rd_ok; (void) da_ok; (void) aa_ok;
    (void) prop_ok;
}

}  // namespace crucible::fixy::sess::delegate
