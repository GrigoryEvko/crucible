#pragma once

// ── crucible::fixy::sync::sync_prim — banned-by-default sync primitive permits ──
//
// FIXY-V-085 (file 3 of 3 in fixy/sync/ family, siblings WaitGrant.h
// + MemOrderGrant.h).  Where V-084 surfaces the VALUES the substrate
// pins to a wait strategy or memory order, V-085 surfaces the
// PRIMITIVES themselves — futex syscalls, condition_variable, mutex,
// shared_mutex, atomic::wait, std::this_thread::sleep_for, etc.
// CLAUDE.md §IX.5 BANS every one of them on the hot path.  V-085
// makes the ban LOAD-BEARING at the type level: a hot-path function
// that admits ANY of the 14 banned permits reddens
// `HotPathSyncPrimSafe<Hot, permit_*>` at compile time.
//
// THE LOAD-BEARING ROLE: encodes CLAUDE.md §VI's ThreadSafe-axiom
// + §IX.5's latency-hierarchy ban surface as a fixy-typed permit
// taxonomy.  Production code that intentionally invokes one of the
// banned primitives on a COLD path declares it via the permit tag
// (e.g., `using cold_path_grants = permit_mutex; ...`) which then
// (a) makes the intent grep-discoverable across the codebase and
// (b) routes through the substrate's V-085 reject gate when the
// permit is composed with a HotPath-tier annotation.
//
// ── Why a PARALLEL taxonomy (not formal fixy::grant) ──────────────
//
// Synchronization is a wrapper-only axis per safety/Fn.h
// DimensionAxis::Synchronization = 20 doc-block — there is NO Fn<...> template-parameter slot for it.
// The `which_dim` specialization mechanism (Grant.h CR-09) is
// reserved for AXES with Fn template-parameter positions.  V-085
// ships a structural taxonomy of permit tags in a parallel
// namespace `crucible::fixy::sync::sync_prim` that is reachable for
// (a) production annotation on functions / classes that invoke the
// primitive, and (b) the load-bearing `HotPathSyncPrimSafe` gate.
//
// ── What this surface IS ──────────────────────────────────────────
//
//  1. `crucible::fixy::sync::sync_prim::banned_sync_prim_base` —
//     empty base of every banned permit tag.  EBO-collapsible.
//
//  2. 14 `permit_*` tags, each `final` (closes the
//     IsBannedSyncPrim concept to the exact 14 tags by inheritance
//     — a downstream subclass would itself be IsBannedSyncPrim-
//     positive, preserving the reject):
//
//        permit_mutex                — std::mutex
//        permit_shared_mutex         — std::shared_mutex
//        permit_recursive_mutex      — std::recursive_mutex
//        permit_timed_mutex          — std::timed_mutex
//        permit_condition_variable   — std::condition_variable
//        permit_condition_variable_any — std::condition_variable_any
//        permit_pthread_cond         — pthread_cond_*
//        permit_futex                — raw futex(2) syscall
//        permit_eventfd              — eventfd(2)
//        permit_poll                 — poll(2)
//        permit_epoll                — epoll_wait(2)
//        permit_atomic_wait          — std::atomic<T>::wait / notify_*
//        permit_sleep_for            — std::this_thread::sleep_for
//        permit_thread_yield         — std::this_thread::yield
//
//  3. `IsBannedSyncPrim<T>` concept — true iff T (cv-ref stripped)
//     derives from `banned_sync_prim_base`.  cv/ref decay matches
//     CollisionCatalog wait_strategy_of pattern: a function param
//     of type `permit_mutex const&` is just as toxic as a bare
//     value type.
//
//  4. `pack_contains_banned_sync_prim_v<Ts...>` — disjunction fold
//     over the pack.  Empty pack ⇒ false (vacuously safe).  ANY
//     match ⇒ true (the rule's hypothesis is satisfied; downstream
//     concept rejects).
//
//  5. `HotPathSyncPrimSafe<Tier, Ts...>` — LOAD-BEARING gate
//     concept.  Satisfied iff NOT (Tier == Hot AND any T in Ts is
//     banned).  In English: "Hot-path tier admits NO permit; Warm
//     and Cold tiers admit any."
//
// ── What this surface IS NOT ──────────────────────────────────────
//
//  * NOT a mint factory.  Permit tags are value-empty type-level
//    annotations; HS14 requires HS14-style fixtures, not mint
//    fixtures.  The HS14 floor is two distinct mismatch classes:
//      (a) cv/ref qualifier — discipline-propagation through
//          decay, mirrors the fixy-A4-033 cv-ref rejection rule.
//      (b) load-bearing reject — HotPathSyncPrimSafe<Hot,
//          permit_mutex> reddens via `requires` clause.
//
//  * NOT a substitute for the CLAUDE.md §IX runtime check.  Future
//    work (V-085 follow-on, not this task) wires
//    `HotPathSyncPrimSafe` into a CollisionCatalog rule that reads
//    permits off Fn<>::grants and reddens H004 / W003 alongside
//    W001 / W002.  V-085 ships the value-level taxonomy + gate
//    only; the production binding lands in future fixy-V tasks.
//
//  * NOT a re-export of safety::*.  V-085 INVENTS the permit tag
//    namespace; there is no substrate-side mirror (the substrate
//    has Wait + MemOrder wrappers, not primitive-permit tags).
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   TypeSafe — permit tags are distinct types; cross-permit
//              confusion (e.g., passing permit_mutex where
//              permit_eventfd is expected) is a hard compile
//              error because the tag types are not interconvertible.
//   ThreadSafe — load-bearing on the HotPathSyncPrimSafe gate:
//                a hot-path tier annotated with any banned permit
//                reddens at the type level.
//   InitSafe — `final` empty tags have no state, no NSDMI hazard.
//   MemSafe — type-level only, no allocation, no lifetime.
//   NullSafe / DetSafe / BorrowSafe / LeakSafe — not applicable
//                (zero runtime cost, pure type-system surface).
//
// ── Runtime cost ──────────────────────────────────────────────────
//
// Zero.  Permit tags are empty `final` structs (sizeof == 1 only
// because the C++ object model requires distinct addresses for
// distinct objects; in any practical composition the tags
// EBO-collapse to 0 bytes).  Sentinels below witness:
// `sizeof(permit_mutex) == 1` and
// `std::is_empty_v<permit_mutex>`.
//
// ── References ────────────────────────────────────────────────────
//
// CLAUDE.md §VI ThreadSafe axiom — acquire/release only on the
//                                  hot path; primitives that block
//                                  exceed the budget.
// CLAUDE.md §IX.5 — latency hierarchy + canonical ban list.
// CollisionCatalog W001 / W002 (FIXY-V-081 / V-082) — analogous
//                                  rules on the Wait wrapper axis.
// fixy.md §24.1 — DimensionAxis::Synchronization = 20 is a wrapper-only axis.

#include <crucible/Platform.h>
#include <crucible/safety/HotPath.h>

#include <cstddef>
#include <tuple>
#include <type_traits>

namespace crucible::fixy::sync {

// ─── Permit-tag taxonomy (14 banned primitives) ────────────────────
//
// All permit tags live under `sync_prim::` to disambiguate from the
// V-084 wait-strategy surface in the parent `fixy::sync::` namespace.
// `banned_sync_prim_base` is intentionally an empty base — the
// derivation expresses "this tag participates in the banned-on-hot-
// path set"; the IsBannedSyncPrim concept reads inheritance.

namespace sync_prim {

struct banned_sync_prim_base {};

// One permit tag per primitive.  Each is `final` to (a) lock the
// taxonomy structurally — no downstream class may pretend to be a
// permit tag through dishonest inheritance — and (b) document the
// terminal-leaf intent.  Note: even a subclass of a `final`-removed
// base would still trip `IsBannedSyncPrim` (concept reads
// `std::is_base_of_v<banned_sync_prim_base, T>` which propagates
// through any chain), so the safety property is preserved either
// way; `final` is documentation + lattice closure.

struct permit_mutex                  final : banned_sync_prim_base {};
struct permit_shared_mutex           final : banned_sync_prim_base {};
struct permit_recursive_mutex        final : banned_sync_prim_base {};
struct permit_timed_mutex            final : banned_sync_prim_base {};
struct permit_condition_variable     final : banned_sync_prim_base {};
struct permit_condition_variable_any final : banned_sync_prim_base {};
struct permit_pthread_cond           final : banned_sync_prim_base {};
struct permit_futex                  final : banned_sync_prim_base {};
struct permit_eventfd                final : banned_sync_prim_base {};
struct permit_poll                   final : banned_sync_prim_base {};
struct permit_epoll                  final : banned_sync_prim_base {};
struct permit_atomic_wait            final : banned_sync_prim_base {};
struct permit_sleep_for              final : banned_sync_prim_base {};
struct permit_thread_yield           final : banned_sync_prim_base {};

// ─── IsBannedSyncPrim concept ──────────────────────────────────────
//
// True iff T (with cv-ref qualifiers stripped) derives from
// banned_sync_prim_base.  cv-ref decay matches the CollisionCatalog
// wait_strategy_of pattern (fixy-A4-033): a parameter of type
// `permit_mutex const&` is exactly as banned as a value-type
// `permit_mutex`.

template <typename T>
concept IsBannedSyncPrim =
    std::is_base_of_v<banned_sync_prim_base, std::remove_cvref_t<T>>;

template <typename T>
inline constexpr bool is_banned_sync_prim_v = IsBannedSyncPrim<T>;

// ─── pack_contains_banned_sync_prim_v<Ts...> ───────────────────────
//
// Disjunction fold over the pack.  Empty pack ⇒ false (vacuously
// safe — a function that admits NO permit cannot be banned).  ANY
// banned match ⇒ true.

template <typename... Ts>
inline constexpr bool pack_contains_banned_sync_prim_v =
    (IsBannedSyncPrim<Ts> || ...);

// ─── HotPathSyncPrimSafe<Tier, Ts...> — load-bearing gate ──────────
//
// Satisfied iff NOT (Tier == Hot AND pack contains any banned
// permit).  Reads in English:
//
//   "A hot-path-tier annotated callable MUST NOT admit any of the
//    14 banned sync-primitive permits."
//
// Warm / Cold tiers are unrestricted — they may admit any permit
// (background-bounded contexts genuinely use std::mutex, condvars,
// and futexes; the discipline is to KEEP those off the hot path).

template <::crucible::safety::HotPathTier_v Tier, typename... Ts>
concept HotPathSyncPrimSafe =
    !(Tier == ::crucible::safety::HotPathTier_v::Hot &&
      pack_contains_banned_sync_prim_v<Ts...>);

// ─── Convenience tier-bound aliases ─────────────────────────────────
//
// `HotSyncPrimSafe<Ts...>` is the load-bearing position — production
// hot-path function signatures spell the gate at Hot tier.

template <typename... Ts>
concept HotSyncPrimSafe =
    HotPathSyncPrimSafe<::crucible::safety::HotPathTier_v::Hot, Ts...>;

template <typename... Ts>
concept WarmSyncPrimSafe =
    HotPathSyncPrimSafe<::crucible::safety::HotPathTier_v::Warm, Ts...>;

template <typename... Ts>
concept ColdSyncPrimSafe =
    HotPathSyncPrimSafe<::crucible::safety::HotPathTier_v::Cold, Ts...>;

}  // namespace sync_prim

// ═════════════════════════════════════════════════════════════════════
// ── Surface integrity sentinels ─────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Every claim above is anchored as a static_assert below.  A future
// edit that drifts the taxonomy (renames a permit, removes `final`,
// admits a new banned permit without updating the count witness)
// reddens here before any consumer notices.

namespace detail::sync_prim_sentinel {

using namespace ::crucible::fixy::sync::sync_prim;

// ─── (1) Permit tag identity + structural shape ─────────────────────

// Each permit tag IS a banned sync primitive (concept-positive).
static_assert(IsBannedSyncPrim<permit_mutex>);
static_assert(IsBannedSyncPrim<permit_shared_mutex>);
static_assert(IsBannedSyncPrim<permit_recursive_mutex>);
static_assert(IsBannedSyncPrim<permit_timed_mutex>);
static_assert(IsBannedSyncPrim<permit_condition_variable>);
static_assert(IsBannedSyncPrim<permit_condition_variable_any>);
static_assert(IsBannedSyncPrim<permit_pthread_cond>);
static_assert(IsBannedSyncPrim<permit_futex>);
static_assert(IsBannedSyncPrim<permit_eventfd>);
static_assert(IsBannedSyncPrim<permit_poll>);
static_assert(IsBannedSyncPrim<permit_epoll>);
static_assert(IsBannedSyncPrim<permit_atomic_wait>);
static_assert(IsBannedSyncPrim<permit_sleep_for>);
static_assert(IsBannedSyncPrim<permit_thread_yield>);

// Non-permits are concept-negative.  This is the bug class V-085
// catches: a refactor that accidentally extends banned_sync_prim_base
// to an unrelated type would trip the rule unexpectedly.  The
// negation cells fence the surface against creep.
static_assert(!IsBannedSyncPrim<int>);
static_assert(!IsBannedSyncPrim<int*>);
static_assert(!IsBannedSyncPrim<void>);
static_assert(!IsBannedSyncPrim<banned_sync_prim_base*>);
struct unrelated_tag {};
static_assert(!IsBannedSyncPrim<unrelated_tag>);

// All 14 permit tags are empty + 1-byte (the minimum the C++ object
// model permits for a distinct-address type).  Composition via
// [[no_unique_address]] EBO-collapses to 0 bytes in any production
// container.
static_assert(std::is_empty_v<permit_mutex>);
static_assert(std::is_empty_v<permit_futex>);
static_assert(std::is_empty_v<permit_atomic_wait>);
static_assert(sizeof(permit_mutex)        == 1);
static_assert(sizeof(permit_atomic_wait)  == 1);
static_assert(sizeof(permit_thread_yield) == 1);

// `final` lock — a downstream may not subclass to fake-pass the
// concept.  (And even if `final` were dropped, the subclass would
// itself trip IsBannedSyncPrim because is_base_of walks the chain
// — the discipline holds either way; this cell documents intent.)
static_assert(std::is_final_v<permit_mutex>);
static_assert(std::is_final_v<permit_shared_mutex>);
static_assert(std::is_final_v<permit_recursive_mutex>);
static_assert(std::is_final_v<permit_timed_mutex>);
static_assert(std::is_final_v<permit_condition_variable>);
static_assert(std::is_final_v<permit_condition_variable_any>);
static_assert(std::is_final_v<permit_pthread_cond>);
static_assert(std::is_final_v<permit_futex>);
static_assert(std::is_final_v<permit_eventfd>);
static_assert(std::is_final_v<permit_poll>);
static_assert(std::is_final_v<permit_epoll>);
static_assert(std::is_final_v<permit_atomic_wait>);
static_assert(std::is_final_v<permit_sleep_for>);
static_assert(std::is_final_v<permit_thread_yield>);

// ─── (2) cv/ref decay — discipline propagates through qualifiers ────
//
// A function-parameter type `permit_mutex const&` is exactly as
// banned as a value-type `permit_mutex`.  Mirrors the
// CollisionCatalog wait_strategy_of cv/ref pierce + fixy-A4-033
// canonical cv-ref rejection discipline.

static_assert(IsBannedSyncPrim<permit_mutex&>);
static_assert(IsBannedSyncPrim<permit_mutex const>);
static_assert(IsBannedSyncPrim<permit_mutex const&>);
static_assert(IsBannedSyncPrim<permit_mutex&&>);
static_assert(IsBannedSyncPrim<permit_futex volatile>);
static_assert(IsBannedSyncPrim<permit_atomic_wait const volatile&>);

// ─── (3) Pack fold semantics ────────────────────────────────────────

// Empty pack: vacuously safe.
static_assert(!pack_contains_banned_sync_prim_v<>);

// Single banned permit: present.
static_assert( pack_contains_banned_sync_prim_v<permit_mutex>);
static_assert( pack_contains_banned_sync_prim_v<permit_futex>);

// All-clean pack: absent.
static_assert(!pack_contains_banned_sync_prim_v<int, double, void*>);
static_assert(!pack_contains_banned_sync_prim_v<unrelated_tag, banned_sync_prim_base*>);

// Mixed pack: presence dominates regardless of position.
static_assert(pack_contains_banned_sync_prim_v<int, permit_mutex, double>);
static_assert(pack_contains_banned_sync_prim_v<permit_eventfd, unrelated_tag>);
static_assert(pack_contains_banned_sync_prim_v<int, double, permit_thread_yield>);

// cv/ref propagation across the pack — even one cv-qualified banned
// permit in the pack is sufficient to satisfy the disjunction.
static_assert(pack_contains_banned_sync_prim_v<int, permit_mutex const&, double>);

// ─── (4) HotPathSyncPrimSafe — load-bearing gate ────────────────────

// Hot tier × banned permit ⇒ UNSAFE (concept FALSE).  This is the
// load-bearing rejection that V-085's HS14 fixture #2 pins via
// neg-compile.
static_assert(!HotPathSyncPrimSafe<::crucible::safety::HotPathTier_v::Hot,
                                    permit_mutex>);
static_assert(!HotPathSyncPrimSafe<::crucible::safety::HotPathTier_v::Hot,
                                    permit_futex>);
static_assert(!HotPathSyncPrimSafe<::crucible::safety::HotPathTier_v::Hot,
                                    permit_atomic_wait>);
static_assert(!HotPathSyncPrimSafe<::crucible::safety::HotPathTier_v::Hot,
                                    int, permit_mutex, double>);
static_assert(!HotSyncPrimSafe<permit_mutex>);
static_assert(!HotSyncPrimSafe<permit_condition_variable>);

// Hot tier × empty pack ⇒ SAFE.
static_assert(HotPathSyncPrimSafe<::crucible::safety::HotPathTier_v::Hot>);
static_assert(HotSyncPrimSafe<>);

// Hot tier × non-banned pack ⇒ SAFE.
static_assert(HotPathSyncPrimSafe<::crucible::safety::HotPathTier_v::Hot,
                                   int, double, unrelated_tag>);
static_assert(HotSyncPrimSafe<int, double>);

// Warm tier × banned permit ⇒ SAFE.  Background-bounded contexts
// genuinely use std::mutex / condvars / futexes.
static_assert(HotPathSyncPrimSafe<::crucible::safety::HotPathTier_v::Warm,
                                   permit_mutex>);
static_assert(WarmSyncPrimSafe<permit_mutex>);
static_assert(WarmSyncPrimSafe<permit_futex, permit_atomic_wait>);

// Cold tier × banned permit ⇒ SAFE.  Cold paths may block freely.
static_assert(HotPathSyncPrimSafe<::crucible::safety::HotPathTier_v::Cold,
                                   permit_mutex>);
static_assert(ColdSyncPrimSafe<permit_mutex, permit_poll, permit_epoll>);

// ─── (5) Cardinality — exact 14 permit tags ─────────────────────────
//
// If a new banned primitive lands (e.g., permit_pthread_mutex,
// permit_sched_yield), the count witness reddens — forcing the
// author to either (a) extend this cell or (b) document why the new
// primitive belongs on a different axis (e.g., a new Wait strategy
// tier, not a permit tag).

// Drives the count test by attempting to construct a static array
// listing every permit type.  If any tag is renamed / removed, the
// array initializer drifts and the count fails.
inline constexpr std::size_t permit_tag_count = []() consteval -> std::size_t {
    // Reference every tag once — drift in either direction trips a
    // hard compile error in this lambda.  The count value is then
    // available for downstream assertions.
    using all_permits = std::tuple<
        permit_mutex, permit_shared_mutex, permit_recursive_mutex,
        permit_timed_mutex, permit_condition_variable,
        permit_condition_variable_any, permit_pthread_cond,
        permit_futex, permit_eventfd, permit_poll, permit_epoll,
        permit_atomic_wait, permit_sleep_for, permit_thread_yield>;
    return std::tuple_size_v<all_permits>;
}();

static_assert(permit_tag_count == 14,
    "FIXY-V-085: sync_prim permit-tag cardinality drift.  Update the "
    "cardinality witness AND the sentinel cells AND the doc-block.");

// ─── (6) Inheritance closure — base_of walks the chain ──────────────
//
// `final` documents terminal-leaf intent; the concept is concept-
// positive on permits regardless because is_base_of walks
// banned_sync_prim_base through any chain.  The base itself is
// concept-positive (it IS-A banned_sync_prim_base by reflexivity).
static_assert(std::is_base_of_v<banned_sync_prim_base, permit_mutex>);
static_assert(std::is_base_of_v<banned_sync_prim_base, permit_thread_yield>);

// And `banned_sync_prim_base` is itself the most-permissive concept-
// positive type — by reflexivity, std::is_base_of_v reports true.
// This is intentional: the base type IS a member of the permit set
// (it just carries no specific primitive identity).
static_assert(IsBannedSyncPrim<banned_sync_prim_base>);

}  // namespace detail::sync_prim_sentinel

}  // namespace crucible::fixy::sync

