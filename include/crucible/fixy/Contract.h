#pragma once

// ── crucible::fixy::contract — Contract macros + Cipher migration ──
//
// Phase C re-export per misc/16_05_2026_fixy.md §B9 / §C.  Surfaces
// the consteval-aware contract macro pair (`CRUCIBLE_PRE` /
// `CRUCIBLE_POST`) AND the Cipher migration discipline (tier
// promotion / demotion / restore + epoch-pinned delegate + persisted
// session) under a single coherent fixy:: surface.  Callers who
// include only the fixy umbrella never have to descend into
// `safety/Contract.h` + `cipher/CipherTierPromotion.h` + `sessions/
// SessionDelegate.h` + `bridges/SessionPersistence.h` to write a
// contract-bounded migration.
//
// **Purely additive.**  No new types, no logic.  Macros are
// preserved via `#include` (macros do not respect namespaces);
// type-level aliases pull substrate names into `fixy::contract::*`
// (general) and `fixy::contract::cipher::*` (Cipher-specific).
//
// ── Two surfaces in one umbrella ────────────────────────────────────
//
//   fixy::contract::*           — the contract macro pair
//                                 (CRUCIBLE_PRE / CRUCIBLE_POST)
//                                 and shared substrate
//   fixy::contract::cipher::*   — Cipher-tier migration primitives:
//                                   - CipherTier<Tier, T>
//                                   - HotTierHandle / WarmTierHandle /
//                                     ColdTierHandle aliases
//                                   - mint_promote<From, To>(...)
//                                   - mint_demote<From, To>(...)
//                                   - mint_restore(...) (Cold → Warm
//                                     with content-hash check)
//                                   - can_promote_tier_v /
//                                     can_demote_tier_v admission
//                                   - EpochedDelegate<T, K, MinE,
//                                     MinG> for migration-bounded
//                                     delegate paths
//                                   - mint_persisted_session(ctx,
//                                     ...) for crash-recovery replay
//
// Per CLAUDE.md §XXI: every mint factory preserves the substrate's
// `[[nodiscard]] constexpr noexcept` qualifiers and `requires` gate.
//
// ── Substrate consumed ──────────────────────────────────────────────
//
//   safety/Contract.h               — CRUCIBLE_PRE + CRUCIBLE_POST
//                                     macros (umbrella over Pre.h
//                                     and Post.h)
//   safety/CipherTier.h             — CipherTier<Tier, T> wrapper
//                                     and tier-tag types
//   cipher/CipherTierPromotion.h    — mint_promote / mint_demote /
//                                     mint_restore + admission gates
//   sessions/SessionDelegate.h      — EpochedDelegate<T, K, MinE,
//                                     MinG> (already re-exported via
//                                     fixy/Sess.h; aliased here for
//                                     migration-flow grep
//                                     discoverability)
//   bridges/SessionPersistence.h    — mint_persisted_session (also
//                                     re-exported via fixy/Bridge.h;
//                                     aliased here for migration-
//                                     flow grep discoverability)
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   InitSafe — re-exports introduce no new state path.
//   TypeSafe — using-declarations preserve substrate concept gates
//              (PromotableTier, DemotableTier, RestorableTier).
//   NullSafe — Cipher handles inherit substrate pointer discipline.
//   MemSafe  — value-type wrappers (CipherTier<Tier, T>) carry their
//              storage; no heap from the wrap.
//   BorrowSafe — mint_promote/demote consume their source by value
//                (single-owner discipline; aliasing impossible).
//   ThreadSafe — wrappers carry no atomics; underlying storage T
//                inherits its own discipline.
//   LeakSafe — substrate uses std::move, no leaks introduced.
//   DetSafe  — tier promotion preserves bit-exact T contents.
//
// ── Cost ────────────────────────────────────────────────────────────
//
// Zero.  Pure name-lookup re-export.  Macros expand identically to
// their substrate definitions (textually included from
// safety/Contract.h).

// fixy-A2-014: SessionPersistence.h no longer transitively pulls Cipher.h;
// fixy/Contract.h ships Cipher migration aliases (CipherTier, mint_promote)
// that need the complete class for static_assert checks.  Restore the
// pull at the umbrella so consumers see no surface change.
#include <crucible/Cipher.h>
#include <crucible/bridges/SessionPersistence.h>
#include <crucible/cipher/CipherTierPromotion.h>
#include <crucible/safety/Contract.h>

namespace crucible::fixy::contract {

// ═════════════════════════════════════════════════════════════════════
// ── Cipher migration sub-namespace ─────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// All Cipher-tier migration primitives live under
// `fixy::contract::cipher::*` so call sites separate "general
// contract discipline" (macros, future invariant aliases) from
// "Cipher state migration" cleanly.  Per audit doc §B9.

namespace cipher {

// ── Tier wrapper + tag enum ─────────────────────────────────────────

using ::crucible::safety::CipherTier;
using ::crucible::safety::CipherTierLattice;
using ::crucible::safety::CipherTierTag_v;

// ── Per-tier handle aliases (Hot / Warm / Cold) ─────────────────────

template <typename T>
using HotTierHandle  = ::crucible::cipher::HotTierHandle<T>;

template <typename T>
using WarmTierHandle = ::crucible::cipher::WarmTierHandle<T>;

template <typename T>
using ColdTierHandle = ::crucible::cipher::ColdTierHandle<T>;

// ── Admission gates (CONTRACT-117) ──────────────────────────────────
//
// Single-integer-compare static predicates over the CipherTierLattice
// chain ordinal (Cold=0 < Warm=1 < Hot=2).  Used by mint_promote /
// mint_demote requires-clauses; surfaced for callers that want to
// write `if constexpr (can_promote_tier_v<From, To>)` branches.

template <CipherTierTag_v From, CipherTierTag_v To>
inline constexpr bool can_promote_tier_v =
    ::crucible::cipher::can_promote_tier_v<From, To>;

template <CipherTierTag_v From, CipherTierTag_v To>
inline constexpr bool can_demote_tier_v =
    ::crucible::cipher::can_demote_tier_v<From, To>;

// ── Mint factories (CLAUDE.md §XXI) ─────────────────────────────────
//
// `mint_promote<From, To>(source)` — From → stronger To.  requires
//                                    can_promote_tier_v<From, To>.
// `mint_demote<From, To>(source)`  — From → weaker To.  requires
//                                    can_demote_tier_v<From, To>.
// `mint_restore(cold, hash)`       — Cold → Warm with content-hash
//                                    check; returns expected<Warm,
//                                    RestoreError>.

using ::crucible::cipher::mint_promote;
using ::crucible::cipher::mint_demote;
using ::crucible::cipher::mint_restore;

// ── RestoreError diagnostic + accessor ──────────────────────────────

using ::crucible::cipher::RestoreError;
using ::crucible::cipher::restore_error_name;

// ── Hot-promote delegate protocol primitives ────────────────────────
//
// CipherTierPromotion.h ships a tiny session-protocol family for
// hot-promotion delegation between Relays (Phase 5 RAID replication
// path).  Re-exporting for callers that want to thread the Cipher
// migration through a session boundary.

template <typename T>
using HotPromotePayload  = ::crucible::cipher::HotPromotePayload<T>;

template <typename T>
using HotPromote         = ::crucible::cipher::HotPromote<T>;

template <typename T, typename K = ::crucible::safety::proto::End>
using HotPromoteDelegate = ::crucible::cipher::HotPromoteDelegate<T, K>;

template <typename T, typename K = ::crucible::safety::proto::End>
using HotPromoteAccept   = ::crucible::cipher::HotPromoteAccept<T, K>;

// ── EpochedDelegate (sessions/SessionDelegate.h) ────────────────────
//
// Migration-bounded delegate path: any session captured via this
// delegate is anchored to a (MinEpoch, MinGen) version pair and
// REJECTS replay below the floor.  Threaded through Cipher migration
// to guarantee a recovered session cannot land on a Relay whose
// epoch/generation is behind the captured snapshot.  Already
// re-exported via fixy/Sess.h::EpochedDelegate; aliased here for
// migration-flow grep discoverability per audit doc §B9.

template <typename T,
          typename K = ::crucible::safety::proto::End,
          unsigned MinEpoch = 0,
          unsigned MinGeneration = 0>
using EpochedDelegate =
    ::crucible::safety::proto::EpochedDelegate<T, K, MinEpoch, MinGeneration>;

// ── mint_persisted_session (bridges/SessionPersistence.h) ───────────
//
// Cipher cold-tier roll-forward wrap.  Composes RecordingSessionHandle
// with Cipher::OpenView so every recorded event is persisted for
// crash-recovery replay across the reincarnation boundary.  Already
// re-exported via fixy/Bridge.h; aliased here for migration-flow
// grep discoverability per audit doc §B9.

using ::crucible::safety::proto::mint_persisted_session;

}  // namespace cipher

}  // namespace crucible::fixy::contract

// ─── Self-test — identity check for the re-export ───────────────────

namespace crucible::fixy::contract::self_test {

// CipherTier wrapper identity.
static_assert(std::is_same_v<
    cipher::CipherTier<
        ::crucible::safety::CipherTierTag_v::Hot, int>,
    ::crucible::safety::CipherTier<
        ::crucible::safety::CipherTierTag_v::Hot, int>>,
    "fixy::contract::cipher::CipherTier must alias safety::CipherTier.");

// Per-tier handle identity.
static_assert(std::is_same_v<
    cipher::HotTierHandle<int>,
    ::crucible::safety::cipher_tier::Hot<int>>,
    "fixy::contract::cipher::HotTierHandle must alias "
    "safety::cipher_tier::Hot.");

static_assert(std::is_same_v<
    cipher::WarmTierHandle<int>,
    ::crucible::safety::cipher_tier::Warm<int>>);

static_assert(std::is_same_v<
    cipher::ColdTierHandle<int>,
    ::crucible::safety::cipher_tier::Cold<int>>);

// Admission gates preserve substrate values.
static_assert(cipher::can_promote_tier_v<
    ::crucible::safety::CipherTierTag_v::Cold,
    ::crucible::safety::CipherTierTag_v::Hot>);

static_assert(!cipher::can_promote_tier_v<
    ::crucible::safety::CipherTierTag_v::Hot,
    ::crucible::safety::CipherTierTag_v::Cold>);

static_assert(cipher::can_demote_tier_v<
    ::crucible::safety::CipherTierTag_v::Hot,
    ::crucible::safety::CipherTierTag_v::Cold>);

// EpochedDelegate identity.
static_assert(std::is_same_v<
    cipher::EpochedDelegate<
        ::crucible::safety::proto::Send<int,
            ::crucible::safety::proto::End>,
        ::crucible::safety::proto::End, 0, 0>,
    ::crucible::safety::proto::EpochedDelegate<
        ::crucible::safety::proto::Send<int,
            ::crucible::safety::proto::End>,
        ::crucible::safety::proto::End, 0, 0>>,
    "fixy::contract::cipher::EpochedDelegate must alias "
    "safety::proto::EpochedDelegate.");

}  // namespace crucible::fixy::contract::self_test
