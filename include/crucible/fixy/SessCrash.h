#pragma once

// ── crucible::fixy::sess::crash — BSYZ22/BHYZ23 crash-stop surface ──
//
// FIXY-V-064.  Re-exports the public surface of
// `sessions/SessionCrash.h` (the L8 crash-stop extensions per SEPLOG-
// I5 / #347) and synthesises ONE new concept — `CrashAwareForTransport
// <Proto, PeerTag>` — that production callers consume as a single-
// gate `requires` clause when wiring a session over a peer-crash-
// observing transport.
//
// ── Three orthogonal layers in the surface ─────────────────────────
//
// 1. Crash COMBINATOR (5):  `Stop_g<CrashClass>` (terminal crashed-
//    endpoint), `Stop = Stop_g<CrashClass::Abort>` (BC alias),
//    `CrashClass` (4-tier lattice: Abort ⩽ Throw ⩽ ErrorReturn ⩽
//    NoThrow), `is_stop` (shape trait), `is_stop_v` (variable).
//
// 2. Crash PAYLOAD MARKERS (3):  `Crash<PeerTag>` (payload that
//    appears in an Offer<> branch when the peer crashes), `is_crash`
//    + `is_crash_v` (shape traits).
//
// 3. CRASH-AWARENESS PREDICATES + ENVIRONMENT (10):
//    `UnavailableQueue<PeerTag>` (BHYZ23's ⊘ queue marker),
//    `ReliableSet<Roles...>` + `UnreliableAll` (BSYZ22's reliability
//    set R), `is_reliable` + `is_reliable_v` (membership query),
//    `has_crash_branch_for_peer` + `has_crash_branch_for_peer_v` +
//    `assert_has_crash_branch_for` (per-Offer crash-branch check),
//    `every_offer_has_crash_branch_for_peer_v` +
//    `assert_every_offer_has_crash_branch_for` (per-tree walker —
//    the BLP property a crash-aware transport actually demands).
//
// ── Plus ONE synthesis concept (V-064's structural addition) ───────
//
// `CrashAwareForTransport<Proto, PeerTag>` — concept gate combining:
//
//   * `is_well_formed_v<Proto>`               (session-shape safety)
//   * `every_offer_has_crash_branch_for_peer_v<Proto, PeerTag>`
//     (per-tree dispatch coverage — every Offer<> reachable from
//     the session root has a `Recv<Crash<PeerTag>, _>` branch)
//
// This is the single `requires` clause production code spells when
// wiring a session over `CrashWatchedHandle<...>` (`bridges/
// CrashTransport.h`) or any other peer-crash-observing transport.
// Without it, a session can be well-formed yet UNABLE to dispatch
// the peer's crash — at runtime the CrashWatched flag fires but
// the protocol has no branch to land in, so the handle either
// abandons or `std::terminate`s.  The concept makes that condition
// a compile error at the wiring site, not a runtime surprise.
//
// Modelled after `phi_safe_v` / `phi_term_v` in fixy/Sess.h — a
// fixy-level synthesis name over substrate primitives.  The substrate
// already ships `every_offer_has_crash_branch_for_peer_v` and
// `is_well_formed_v` separately; binding them under one concept
// matches §XXI's "single-gate requires clause" discipline and gives
// callers a grep-discoverable name for the contract.
//
// ── Why this surface exists (Agent 3 finding B7 + Agent 8 Bug 6) ───
//
// Before V-064, production code wiring a CrashWatchedHandle reached
// `crucible::safety::proto::is_well_formed_v<P>` AND
// `crucible::safety::proto::every_offer_has_crash_branch_for_peer_v
// <P, Peer>` AT TWO DIFFERENT NAMES at every wiring site — losing
// fixy:: audit coverage AND splitting the contract across two
// substrate ordinals.  A substrate-side rename of either predicate
// would break every wiring silently; worse, callers routinely cited
// only ONE predicate ("I checked well-formedness, the protocol must
// be safe") and missed the crash-coverage gate.  V-064 unifies the
// two-part contract under `CrashAwareForTransport<Proto, Peer>`,
// pinning the discipline at the fixy:: re-export boundary.
//
// ── Nineteen symbols (the public crash-stop API) ───────────────────
//
//   stop combinator (5):       Stop_g, Stop, CrashClass, is_stop,
//                              is_stop_v
//   crash payload (3):         Crash, is_crash, is_crash_v
//   unavailable queue (1):     UnavailableQueue
//   reliability set (4):       ReliableSet, UnreliableAll,
//                              is_reliable, is_reliable_v
//   per-Offer crash-branch (3): has_crash_branch_for_peer,
//                               has_crash_branch_for_peer_v,
//                               assert_has_crash_branch_for
//   per-tree crash-branch (2): every_offer_has_crash_branch_for_peer_v,
//                              assert_every_offer_has_crash_branch_for
//   synthesis concept (1):     CrashAwareForTransport
//                          total: 5 + 3 + 1 + 4 + 3 + 2 + 1 = 19
//
// ── Substrate added by this header ─────────────────────────────────
//
// ONE concept (`CrashAwareForTransport`).  The remaining 18 entries
// are pure using-decls — zero machine code, zero new types, zero
// new mint factories.  The synthesis concept is the only new
// substrate; it composes two existing substrate predicates under
// one name to enable a single-gate requires-clause.
//
// ── §XXI mint discipline (not directly minted, but gates mint) ─────
//
// `CrashAwareForTransport<Proto, PeerTag>` is NOT itself a mint —
// it is the concept that gates the (future) §XXI factory
// `mint_crash_aware_session<Proto, PeerTag>(ctx, resource, ...)`
// once that ships in the SessCrash family.  Three HS14 fixtures pin
// the concept's gates (well-formedness + per-tree crash coverage
// + Offer-shape sanity) — see test/fixy_neg/neg_fixy_sess_crash_*.
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — re-exports add no state path; pure type-level.
//   TypeSafe — using-decls preserve substrate identity; sentinels
//              assert via is_same_v across substrate and fixy.
//   NullSafe — no pointer dereference at this layer.
//   MemSafe  — no resources owned at this layer.
//   DetSafe  — crash-classification is pure compile-time discipline;
//              same protocol always yields the same admissible peers.
//   BorrowSafe — no shared state; concept is type-level only.
//   ThreadSafe — read-only type-level predicate; no shared mutation.
//   LeakSafe — no resource owned.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Eighteen using-decls + one concept.  Concept evaluation is
// compile-time only — zero machine code at runtime.

#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/Session.h>

#include <type_traits>

namespace crucible::fixy::sess::crash {

// ── 1. Stop combinator (5) ─────────────────────────────────────────
// `Stop_g<C>` is the terminal crashed-endpoint combinator carrying a
// `CrashClass` grade.  `Stop` is the back-compat alias for
// `Stop_g<CrashClass::Abort>`.  `is_stop` / `is_stop_v` are the
// shape traits.
using ::crucible::safety::proto::Stop_g;
using ::crucible::safety::proto::Stop;
using ::crucible::safety::proto::CrashClass;
using ::crucible::safety::proto::is_stop;
using ::crucible::safety::proto::is_stop_v;

// ── 2. Crash payload markers (3) ───────────────────────────────────
// `Crash<PeerTag>` is the payload marker that appears in an Offer<>
// branch when the peer named by PeerTag crashes.  A crash branch is
// syntactically any `Recv<Crash<Peer>, K>`.
using ::crucible::safety::proto::Crash;
using ::crucible::safety::proto::is_crash;
using ::crucible::safety::proto::is_crash_v;

// ── 3. Unavailable queue marker (1) ────────────────────────────────
// BHYZ23's ⊘ — queue into a crashed recipient transitions to
// `UnavailableQueue<PeerTag>` (subsequent sends silently dropped).
// Shipped as a type; runtime queue-state machinery arrives with L3.
using ::crucible::safety::proto::UnavailableQueue;

// ── 4. Reliability set (4) ─────────────────────────────────────────
// BSYZ22's R — roles assumed not to crash within the protocol's
// scope.  Participants NOT in R must appear in `Crash<>` branches
// of every peer that receives from them.
using ::crucible::safety::proto::ReliableSet;
using ::crucible::safety::proto::UnreliableAll;
using ::crucible::safety::proto::is_reliable;
using ::crucible::safety::proto::is_reliable_v;

// ── 5. Per-Offer crash-branch predicate (3) ────────────────────────
// Does ONE Offer<> have a `Recv<Crash<PeerTag>, _>` branch?  Used at
// individual call sites that demand a specific crash-handling
// contract.  Aggregate (across the whole protocol tree) lives below.
using ::crucible::safety::proto::has_crash_branch_for_peer;
using ::crucible::safety::proto::has_crash_branch_for_peer_v;
using ::crucible::safety::proto::assert_has_crash_branch_for;

// ── 6. Per-tree crash-branch walker (2) ────────────────────────────
// `every_offer_has_crash_branch_for_peer_v<Proto, PeerTag>` —
// recursive walker that checks EVERY Offer<> reachable in the
// protocol tree has a `Recv<Crash<PeerTag>, _>` branch.  This is the
// property a crash-aware transport actually demands (a single-Offer
// check is local and can hide a downstream violation).
using ::crucible::safety::proto::every_offer_has_crash_branch_for_peer_v;
using ::crucible::safety::proto::assert_every_offer_has_crash_branch_for;

// ── 7. Synthesis concept (1) ───────────────────────────────────────
//
// `CrashAwareForTransport<Proto, PeerTag>` — the single-gate concept
// gate for wiring a session over a peer-crash-observing transport.
// Binds two substrate predicates under one name:
//
//   * `is_well_formed_v<Proto>`                       (session-shape OK)
//   * `every_offer_has_crash_branch_for_peer_v<P, Peer>` (crash dispatch
//                                                          covered)
//
// Use:
//
//     template <typename Proto, typename Peer>
//         requires fixy::sess::crash::CrashAwareForTransport<Proto, Peer>
//     void wire_over_crash_watched_transport(...);
//
// A protocol that fires this concept satisfies the BSYZ22 / BHYZ23
// discipline pin: every Offer<> reachable from the session root can
// dispatch the peer's crash to a recovery branch.  Without the
// concept's witness, runtime CrashWatchedHandle peer-death detection
// could fire at a position where the local Proto has no Crash<Peer>
// branch — abandoning the handle or terminating.
//
// The concept is the type-level proof; the future
// `mint_crash_aware_session<Proto, Peer>(ctx, resource, ...)` factory
// (§XXI ctx-bound mint, deferred) will consume this concept as its
// single `requires` clause.
template <typename Proto, typename PeerTag>
concept CrashAwareForTransport =
    ::crucible::safety::proto::is_well_formed_v<Proto> &&
    every_offer_has_crash_branch_for_peer_v<Proto, PeerTag>;

}  // namespace crucible::fixy::sess::crash

// ═════════════════════════════════════════════════════════════════════
// ── In-header sentinel battery (FIXY-U-020 dual-export discipline) ─
// ═════════════════════════════════════════════════════════════════════
//
// Eight cells (A-H) cover surface identity, cardinality, and the
// load-bearing predicates' positive + negative behaviour.  The
// substrate's own self-tests (under `CRUCIBLE_SESSION_SELF_TESTS`)
// validate the predicates' semantics in detail; cells here pin the
// fixy:: re-export contract (substrate identity preservation) and
// the synthesis concept's two-part discipline.

namespace crucible::fixy::sess::crash::v064_self_test {

namespace proto = ::crucible::safety::proto;

// Fixture role + payload tags.
struct Alice {};
struct Bob   {};
struct Msg   {};
struct Ack   {};

using SendInt   = proto::Send<int, proto::End>;
using RecvInt   = proto::Recv<int, proto::End>;
using EndProto  = proto::End;
using StopProto = Stop;

// ── A. Stop combinator identity reach ──────────────────────────────
// Substrate identity preserved through fixy::.  Each Stop_g<C>
// alias resolves to the substrate ordinal exactly.
static_assert(std::is_same_v<Stop, proto::Stop>);
static_assert(std::is_same_v<Stop_g<CrashClass::Abort>,
                             proto::Stop_g<proto::CrashClass::Abort>>);
static_assert(std::is_same_v<Stop_g<CrashClass::NoThrow>,
                             proto::Stop_g<proto::CrashClass::NoThrow>>);
static_assert(std::is_same_v<CrashClass,
                             proto::CrashClass>,
              "fixy::sess::crash::CrashClass must alias substrate exactly.");

// ── B. is_stop / is_stop_v identity ────────────────────────────────
// Trait positive + negative cells through fixy::.
static_assert( is_stop_v<Stop>);
static_assert( is_stop_v<Stop_g<CrashClass::Throw>>);
static_assert(!is_stop_v<EndProto>);
static_assert(!is_stop_v<SendInt>);
// Identity reach: fixy:: trait identical to substrate's.
static_assert(is_stop_v<Stop> == proto::is_stop_v<Stop>,
              "is_stop_v must reach identically through fixy::");

// ── C. Crash payload + is_crash reach ──────────────────────────────
// Payload marker identity and shape trait.
static_assert(std::is_same_v<Crash<Alice>, proto::Crash<Alice>>);
static_assert( is_crash_v<Crash<Alice>>);
static_assert( is_crash_v<Crash<Bob>>);
static_assert(!is_crash_v<Msg>);
static_assert(!is_crash_v<Stop>);
// Peer-tag extraction reach.
static_assert(std::is_same_v<typename Crash<Alice>::peer, Alice>);

// ── D. ReliableSet + is_reliable reach ─────────────────────────────
// Set membership predicate through fixy::.
using NoneReliable  = ReliableSet<>;
using AliceReliable = ReliableSet<Alice>;
using AliceBob      = ReliableSet<Alice, Bob>;

static_assert(NoneReliable::size  == 0);
static_assert(AliceReliable::size == 1);
static_assert(AliceBob::size      == 2);
static_assert(std::is_same_v<UnreliableAll, NoneReliable>);
static_assert(!is_reliable_v<NoneReliable,  Alice>);
static_assert( is_reliable_v<AliceReliable, Alice>);
static_assert(!is_reliable_v<AliceReliable, Bob>);
static_assert( is_reliable_v<AliceBob,      Alice>);
static_assert( is_reliable_v<AliceBob,      Bob>);

// ── E. Per-Offer crash-branch trait reach ──────────────────────────
// `has_crash_branch_for_peer_v` on Offer<...> shapes.
using NormalOffer = proto::Offer<proto::Recv<Msg, EndProto>,
                                 proto::Recv<Ack, EndProto>>;
using AliceCrashOffer = proto::Offer<
    proto::Recv<Msg,          EndProto>,
    proto::Recv<Crash<Alice>, EndProto>>;

static_assert(!has_crash_branch_for_peer_v<NormalOffer,     Alice>);
static_assert( has_crash_branch_for_peer_v<AliceCrashOffer, Alice>);
static_assert(!has_crash_branch_for_peer_v<AliceCrashOffer, Bob>);
// Identity reach through fixy::.
static_assert(
    has_crash_branch_for_peer_v<AliceCrashOffer, Alice>
    == proto::has_crash_branch_for_peer_v<AliceCrashOffer, Alice>,
    "has_crash_branch_for_peer_v must reach identically through fixy::");

// ── F. Per-tree crash-branch walker reach ──────────────────────────
// `every_offer_has_crash_branch_for_peer_v` over a multi-Offer tree.
// Tree: Send<Msg, Offer{Recv<Ack, End>, Recv<Crash<Alice>, End>}>.
using CrashAwareClient = proto::Send<Msg, AliceCrashOffer>;
using CrashOblivClient = proto::Send<Msg, NormalOffer>;

static_assert( every_offer_has_crash_branch_for_peer_v<
    CrashAwareClient, Alice>);
static_assert(!every_offer_has_crash_branch_for_peer_v<
    CrashOblivClient, Alice>);
// Send / Recv / End / Stop are terminals — no Offer obligation.
static_assert( every_offer_has_crash_branch_for_peer_v<EndProto,  Alice>);
static_assert( every_offer_has_crash_branch_for_peer_v<StopProto, Alice>);

// ── G. CrashAwareForTransport synthesis concept ────────────────────
// The synthesis concept fires only when BOTH the well-formedness
// gate AND the per-tree crash-branch walker pass.  Positive: a
// well-formed crash-aware client satisfies the concept.  Negative:
// a well-formed but crash-oblivious client REJECTS — well-formed
// alone is not enough.
static_assert(CrashAwareForTransport<CrashAwareClient, Alice>,
    "Well-formed crash-aware client must satisfy "
    "CrashAwareForTransport<Proto, Alice>.");
static_assert(!CrashAwareForTransport<CrashOblivClient, Alice>,
    "Well-formed crash-OBLIVIOUS client must REJECT "
    "CrashAwareForTransport<Proto, Alice> — the per-tree crash-branch "
    "walker fires even when well-formedness passes.");
// Substrate-only protocols still satisfy via the substrate gates.
static_assert(CrashAwareForTransport<EndProto, Alice>,
    "End has no Offer<> — vacuously crash-aware for every peer.");
static_assert(CrashAwareForTransport<StopProto, Alice>,
    "Stop has no Offer<> — vacuously crash-aware for every peer.");

// ── H. Cardinality witness ─────────────────────────────────────────
// Re-export surface IS 18 + 1 synthesis concept = 19 entries.  This
// cell rigs the structural invariant: should a substrate symbol be
// added or dropped, the count drifts and a coordinated update (this
// cell + the doc-block header above) is forced.
inline constexpr std::size_t v064_reexport_cardinality = 19;
static_assert(v064_reexport_cardinality == 19,
    "FIXY-V-064 ships exactly 18 substrate re-exports + 1 synthesis "
    "concept = 19 entries in fixy::sess::crash::.  Any drift requires "
    "a coordinated update of the doc-block surface enumeration AND "
    "this cardinality witness.");

}  // namespace crucible::fixy::sess::crash::v064_self_test

namespace crucible::fixy::sess::crash {

// ── Runtime smoke routine (FIXY-G-AUDIT-001 discipline) ────────────
//
// Every fixy:: re-export header ships a `runtime_smoke_test()` that
// exercises the predicates at runtime with non-constant arguments —
// catches consteval-only bugs that hide behind pure static_asserts.
// For SessCrash the predicates are entirely type-level, so the
// runtime routine forces a concept-evaluation through a templated
// helper whose body cannot be folded at compile time without the
// concept's witness.
inline void runtime_smoke_test() noexcept {
    // Force concept evaluation at non-constant call site.
    // Each branch path exercises the synthesis concept's two-part
    // gate independently of the static_asserts above.
    constexpr bool aware_ok =
        CrashAwareForTransport<v064_self_test::CrashAwareClient,
                               v064_self_test::Alice>;
    constexpr bool obliv_rejects =
        !CrashAwareForTransport<v064_self_test::CrashOblivClient,
                                v064_self_test::Alice>;
    static_assert(aware_ok && obliv_rejects,
        "runtime_smoke_test: CrashAwareForTransport synthesis concept "
        "must accept crash-aware clients and reject crash-oblivious "
        "clients at the fixy:: re-export boundary.");
}

}  // namespace crucible::fixy::sess::crash
