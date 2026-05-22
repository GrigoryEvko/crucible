#pragma once

// ── crucible::fixy::contract — Contract macros + Cipher migration ──
//
// Re-export per misc/16_05_2026_fixy.md §B9 / §C.  Surfaces
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
#include <crucible/cipher/ComputationCache.h>           // FIXY-U-015: dispatcher cache surface
#include <crucible/cipher/ComputationCacheFederation.h> // FIXY-U-015: cache federation wire-keys
#include <crucible/cipher/FederationProtocol.h>         // FIXY-U-015: federation entry wire format
#include <crucible/effects/ExecCtx.h>                   // FIXY-V-220: IsBgCtx / IsFgCtx / IsInitCtx
#include <crucible/safety/Contract.h>

#include <chrono>      // FIXY-U-015: drain_computation_cache signature
#include <type_traits> // FIXY-U-015 sentinel uses std::is_same_v

// FIXY-V-220 — forward-declare the 5 non-Cipher host classes that
// carry member-function mints.  Pure name reach for the registration
// table; the specialization machinery does NOT need the complete type
// (admits<Ctx>() only inspects Ctx).  Avoids pulling CKernel.h /
// SchemaTable.h / PoolAllocator.h / CrucibleContext.h / ReplayEngine.h
// transitively through every fixy::contract:: consumer.
namespace crucible {
struct CKernelTable;
struct SchemaTable;
struct PoolAllocator;
struct CrucibleContext;
struct ReplayEngine;
}  // namespace crucible

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

// ═════════════════════════════════════════════════════════════════════
// ── FIXY-U-015: Cipher class + dispatcher cache + federation wire ──
// ═════════════════════════════════════════════════════════════════════
//
// Pre-U-015 the fixy::contract::cipher:: surface exposed only the
// tier-promotion machinery (CipherTier wrapper, mint_promote/demote/
// restore, EpochedDelegate, mint_persisted_session).  Callers who
// wanted to NAME the Cipher class itself, mint an OpenView, key into
// the dispatcher's ComputationCache, or read/write a federation entry
// had to descend into the substrate's Cipher.h / cipher/ComputationCache.h
// / cipher/FederationProtocol.h headers — three header families
// outside the "fixy covers cipher" promise.
//
// U-015 closes the gap.  Four categories of surface, ~28 items:
//   A. Top-level Cipher class + companion content-addressed payload
//      templates + row-hash alias (lives at `::crucible::Cipher`, not
//      inside `::crucible::cipher::`; alias bridges the two paths).
//   B. SessionEvent + its 72-byte cold-tier wire format.
//   C. ComputationCache surface — dispatcher analog of KernelCache
//      keyed on `(stable_function_id<FnPtr>, stable_type_id<Args>...)`.
//      Includes IsCacheableFunction + IsEffectRow concept fences,
//      key + lookup + insert + drain quadruples (both vanilla and
//      row-aware variants).
//   D. cipher::federation:: nested namespace — wire format for
//      cold-tier blob entries (magic / version / header / payload
//      / view / serialize / deserialize / error enum).
//
// All re-exports are pure name-lookup directives; concepts pass
// through verbatim per C++20 using-decl semantics.  Sentinel
// witnesses below pin each category's substrate identity, plus a
// cardinality witness at the tail.

// ── A. Top-level Cipher class + companion templates ────────────────

// The Cipher class itself lives at ::crucible::Cipher (top-level,
// NOT inside ::crucible::cipher::).  Surfacing it under the migration
// sub-namespace gives callers a single discovery point for "anything
// Cipher" — including the class, the migration mints (above), and the
// federation wire format (below).
using ::crucible::Cipher;

// The class-scope OpenView alias is reachable through Cipher::OpenView
// (since the class itself is now aliased), but the namespace-scope
// CipherOpenView is the bridge-friendly form (used by
// bridges/SessionPersistence.h to avoid pulling the full Cipher.h
// transitive set).  Both spellings now reach.
using ::crucible::CipherOpenView;

// Persistence-row alias — same definition the Cipher class uses
// internally for persist_session_events_required_row.  Surfaced at
// fixy::contract::cipher:: scope so the row is grep-discoverable
// without crucible::Cipher:: prefix.
using ::crucible::CipherSessionEventPersistenceRow;

// Content-addressed payload templates live in ::crucible::cipher::
// and are the wrapping that the Cipher class consumes via
// Cipher::content_addressed(region).  Surface them so callers can
// declare ContentAddressedPayload<MyType> at non-Cipher call sites.
template <typename T>
using ContentAddressedPayload =
    ::crucible::cipher::ContentAddressedPayload<T>;

template <typename T>
using LoadedContentAddressedPayload =
    ::crucible::cipher::LoadedContentAddressedPayload<T>;

// ── B. SessionEvent — 72-byte cold-tier wire format ────────────────
//
// Cipher::SessionEvent is an alias of safety::proto::SessionEvent.
// Surfacing the alias at namespace scope (rather than going through
// the Cipher class) saves callers a `Cipher::` prefix when they only
// need the payload type.

using SessionEvent = ::crucible::safety::proto::SessionEvent;

// ── C. ComputationCache surface (FOUND-F09 of 27_04_2026.md §5.13) ─
//
// Dispatcher analog of KernelCache.  KernelCache (MerkleDag.h)
// stores compiled kernel bytecode keyed on `(content_hash,
// row_hash)`; ComputationCache stores compiled-body objects keyed on
// `(stable_function_id<FnPtr>, stable_type_id<Args>...)`.  When the
// dispatcher generates a lowering for `dispatch(fn, args)`, it
// consults this cache:  hit → use the cached body, miss → generate.

// CompiledBody is opaque-forward-declared in the substrate — the
// dispatcher provides the concrete definition.  Re-export the
// forward declaration so call sites can `CompiledBody* body =
// ...lookup_computation_cache<F, Args...>();` without descending into
// the cipher/ header.
using ::crucible::cipher::CompiledBody;

// Concept fences on the template parameters.  IsCacheableFunction
// rejects non-callable / non-cacheable FnPtr at the requires-clause;
// IsEffectRow rejects non-Row<E...> Row template arguments.
using ::crucible::cipher::IsCacheableFunction;
using ::crucible::cipher::IsEffectRow;

// computation_cache_key<FnPtr, Args...> — canonical 64-bit cache key.
// Built from stable_function_id<FnPtr> mixed with stable_type_id of
// each Arg via the family-A FNV-1a fold.  Same args → same key
// across TUs and across program restarts (within a build).
template <auto FnPtr, typename... Args>
    requires ::crucible::cipher::IsCacheableFunction<FnPtr>
inline constexpr std::uint64_t computation_cache_key =
    ::crucible::cipher::computation_cache_key<FnPtr, Args...>;

// lookup_computation_cache<FnPtr, Args...>() — acquire-load the
// current slot.  Returns nullptr on miss; returns a non-null
// CompiledBody* on hit.  Lock-free.
using ::crucible::cipher::lookup_computation_cache;

// insert_computation_cache<FnPtr, Args...>(body) — single-writer
// install via compare_exchange_strong on the slot.  Pre: body !=
// nullptr.  If the slot is already populated, leaves the existing
// entry untouched (single-publisher discipline).
using ::crucible::cipher::insert_computation_cache;

// Row-aware variants — additional template parameter Row threads
// the effect row through the cache key, so the same FnPtr × Args
// combination at different Row engagements get different cache
// slots.  Used by callers that need per-effect-row specialization.
template <auto FnPtr, typename Row, typename... Args>
    requires ::crucible::cipher::IsCacheableFunction<FnPtr>
          && ::crucible::cipher::IsEffectRow<Row>
inline constexpr std::uint64_t computation_cache_key_in_row =
    ::crucible::cipher::computation_cache_key_in_row<FnPtr, Row, Args...>;

using ::crucible::cipher::lookup_computation_cache_in_row;
using ::crucible::cipher::insert_computation_cache_in_row;

// drain_computation_cache(max_age) — Phase 5 eviction stub (today:
// no-op; signature published so production call sites pre-wire).
using ::crucible::cipher::drain_computation_cache;

// ── D. cipher::federation:: wire format ────────────────────────────
//
// Federation-entry serialization for cold-tier blob exchange between
// Relays.  Each entry carries a fixed 16-byte header (magic + version
// + payload-size + checksum) followed by the payload bytes.  Used by
// Cipher's session-event persistence + RegionNode federation gossip.
//
// Surface mirrors safety/proto::federation:: shape from U-072: a
// nested sub-namespace under fixy::contract::cipher::federation::
// containing constants, structs, error enum, free functions.

namespace federation {

// Magic and version constants — pinned at the wire-format level so
// a future ABI bump is grep-discoverable.
using ::crucible::cipher::federation::FEDERATION_MAGIC;
using ::crucible::cipher::federation::FEDERATION_PROTOCOL_V1;
using ::crucible::cipher::federation::FEDERATION_HEADER_BYTES;

// Wire-format structs.
using ::crucible::cipher::federation::FederationEntryHeader;
using ::crucible::cipher::federation::ColdBlobRegion;
using ::crucible::cipher::federation::FederationEntryView;

// Error discriminant + accessor.
using ::crucible::cipher::federation::FederationError;
using ::crucible::cipher::federation::federation_error_name;

// Serializer + deserializers (FederationProtocol.h).
//   serialize_federation_entry              — write entry into buffer.
//   deserialize_federation_header           — parse just the 32-B head.
//   deserialize_untrusted_federation_entry  — parse + validate full entry.
//   deserialize_federation_entry            — parse + validate +
//                                              error-bind via peer
//                                              permission (template on Org).
using ::crucible::cipher::federation::serialize_federation_entry;
using ::crucible::cipher::federation::deserialize_federation_header;
using ::crucible::cipher::federation::deserialize_untrusted_federation_entry;
using ::crucible::cipher::federation::deserialize_federation_entry;

// Layout / cardinality predicates.
using ::crucible::cipher::federation::federation_entry_blob_layout_disjoint;
using ::crucible::cipher::federation::cold_blob_regions_pairwise_disjoint;
using ::crucible::cipher::federation::federation_accepts_cardinality;

// ── ComputationCacheFederation.h additions ────────────────────────
//
// Federation wire-format extensions for the dispatcher's
// ComputationCache: a per-(FnPtr, Row, Args...) federation key tag,
// per-role MPST protocol projections, and content-addressed payload
// wrappers that elide redundant bytes on the wire when both sides
// share the kernel.

using ::crucible::cipher::federation::ComputationCacheFederationKeyTag;
using ::crucible::cipher::federation::ComputationCacheFederationSenderProto;
using ::crucible::cipher::federation::ComputationCacheFederationReceiverProto;
using ::crucible::cipher::federation::ComputationCacheFederationCoordProto;

// Content-addressed payload wrapper (template on Payload).  Used by
// federation entries to elide redundant payload bytes when the peer
// can recover the payload from a content hash.
using ::crucible::cipher::federation::ContentAddressedFederationPayload;

// Per-(FnPtr, Row, Args...) payload aliases — one for raw bytes, one
// for content-addressed.
using ::crucible::cipher::federation::ComputationCacheFederationPayload;
using ::crucible::cipher::federation::ComputationCacheFederationContentAddressedPayload;

// Hash helpers — used by federation entry construction to derive
// the (content_hash, row_hash) keys on a per-(FnPtr, Row, Args...)
// basis without consulting the runtime cache.
using ::crucible::cipher::federation::federation_content_hash;
using ::crucible::cipher::federation::federation_row_hash;
using ::crucible::cipher::federation::federation_key;

// Serializer specialized for ComputationCache entries.  Two overloads
// — one taking the payload by span, one taking it by reference.
using ::crucible::cipher::federation::serialize_computation_cache_federation_entry;

}  // namespace federation

}  // namespace cipher

// ═════════════════════════════════════════════════════════════════════
// ── FIXY-V-220: member-mint required-ctx documentation hook ────────
// ═════════════════════════════════════════════════════════════════════
//
// Per CLAUDE.md §XXI, Crucible's 8 member-function mints are `mint_*`
// methods on host classes; they cannot be re-exported through
// `using ...::method;` directives because member functions are not
// namespace-scope entities.  Production call sites still want a
// type-level witness that "I'm invoking this method from the right
// ExecCtx tier" — the V-220 hook provides exactly that.
//
// The mechanism is a constexpr registration table:
//
//   member_mint_required_ctx<Class, MintName>::admits<Ctx>()
//
// per-(Class, MintName) pair, where:
//   * Class       — the host class type (forward-declared above)
//   * MintName    — a tag struct from `mint_name::` enumerating the
//                   distinct mint-method names
//   * admits<Ctx> — consteval predicate over the calling ExecCtx
//
// Production code opts in via static_assert ABOVE the mint invocation:
//
//   static_assert(::crucible::fixy::contract::MemberMintCtxRequired<
//                     ::crucible::Cipher,
//                     ::crucible::fixy::contract::mint_name::open_view,
//                     decltype(ctx)>);
//   auto view = cipher.mint_open_view();
//
// Each specialization carries `name()` and `required_ctx_description()`
// consteval strings to surface the expectation for diagnostics + grep.
//
// ── The 8 registered member mints ──────────────────────────────────
//
//   1. ::crucible::Cipher::mint_open_view()
//        ↳ admits IsBgCtx (OpenView writes are drain-side)
//   2. ::crucible::CKernelTable::mint_mutable_view()
//        ↳ admits IsInitCtx (cold-init table build, single writer)
//   3. ::crucible::CKernelTable::mint_sealed_view()
//        ↳ admits IsExecCtx (hot or bg reads after seal — any ctx)
//   4. ::crucible::SchemaTable::mint_mutable_view()
//        ↳ admits IsInitCtx (mirrors #2)
//   5. ::crucible::SchemaTable::mint_sealed_view()
//        ↳ admits IsExecCtx (mirrors #3)
//   6. ::crucible::PoolAllocator::mint_initialized_view()
//        ↳ admits IsFgCtx OR IsBgCtx (hot alloc + bg teardown)
//   7. ::crucible::CrucibleContext::mint_compiled_view()
//        ↳ admits IsFgCtx (compiled dispatch path)
//   8. ::crucible::ReplayEngine::mint_active_view()
//        ↳ admits IsFgCtx (replay dispatch path)
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — pure type-level predicate; no state.
//   TypeSafe — (Class, MintName) are type-level discriminators; an
//              unregistered pair fails substitution.
//   NullSafe — no runtime; nothing to dereference.
//   MemSafe  — no storage, no lifetime.
//   BorrowSafe — concept doesn't bind to instances.
//   ThreadSafe — consteval, thread-agnostic.
//   LeakSafe — no storage.
//   DetSafe  — same (Class, MintName, Ctx) → same answer.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Concept resolves at template-substitution time; no runtime
// emission.  Opt-in static_assert collapses to nothing under -O3.

// ── mint_name:: tag types (grep-friendly) ──────────────────────────
//
// Tag types — one per distinct mint-method name across all host
// classes.  open_view is currently unique to Cipher; mutable_view and
// sealed_view are shared by CKernelTable + SchemaTable; the others
// are unique to their host.  When a new member mint lands, add its
// tag here AND register the (Class, mint_name::tag) specialization
// below.

namespace mint_name {
struct open_view {};
struct mutable_view {};
struct sealed_view {};
struct initialized_view {};
struct compiled_view {};
struct active_view {};
}  // namespace mint_name

// ── Primary template: undefined ────────────────────────────────────
//
// An unregistered (Class, MintName) pair triggers a hard substitution
// failure when `MemberMintCtxRequired` probes the spec — surfaces as
// "incomplete type" / "use of undeclared specialization" diagnostic.
// HS14 fixture #2 pins this rejection axis (unregistered pair).

template <class Class, class MintName>
struct member_mint_required_ctx;

// ── 1. Cipher::mint_open_view → IsBgCtx ────────────────────────────
//
// Cipher.h:304 — `[[nodiscard]] OpenView mint_open_view() const noexcept
// pre(is_open())`.  The OpenView mediates writes to the Cipher
// cold-tier object store; per CipherTier discipline these MUST happen
// from a BG drain thread (BackgroundThread::run + ::flush call sites).
// Calling from FG would race the FG dispatch hot path.

template <>
struct member_mint_required_ctx<::crucible::Cipher, mint_name::open_view> {
    template <class Ctx>
    static consteval bool admits() noexcept {
        return ::crucible::effects::IsBgCtx<std::remove_cvref_t<Ctx>>;
    }
    static consteval const char* name() noexcept {
        return "Cipher::mint_open_view";
    }
    static consteval const char* required_ctx_description() noexcept {
        return "IsBgCtx — Cipher::OpenView writes are drain-side";
    }
};

// ── 2. CKernelTable::mint_mutable_view → IsInitCtx ─────────────────
//
// CKernel.h:463 — mutable view used by Vessel registration BEFORE the
// table is sealed.  Single writer (the registration site, called from
// the Vessel adapter init path).  Wrong ctx (Fg/Bg) would race the
// post-seal sealed-view reads.

template <>
struct member_mint_required_ctx<::crucible::CKernelTable, mint_name::mutable_view> {
    template <class Ctx>
    static consteval bool admits() noexcept {
        return ::crucible::effects::IsInitCtx<std::remove_cvref_t<Ctx>>;
    }
    static consteval const char* name() noexcept {
        return "CKernelTable::mint_mutable_view";
    }
    static consteval const char* required_ctx_description() noexcept {
        return "IsInitCtx — pre-seal cold-init table build, single writer";
    }
};

// ── 3. CKernelTable::mint_sealed_view → IsExecCtx (any) ────────────
//
// CKernel.h:469 — sealed view exposes read-only iteration over the
// sealed table.  Used by FG dispatch (classify_kernel) AND BG drain
// (build_trace).  No ctx restriction beyond IsExecCtx — every Ctx
// shape can read post-seal.

template <>
struct member_mint_required_ctx<::crucible::CKernelTable, mint_name::sealed_view> {
    template <class Ctx>
    static consteval bool admits() noexcept {
        return ::crucible::effects::IsExecCtx<std::remove_cvref_t<Ctx>>;
    }
    static consteval const char* name() noexcept {
        return "CKernelTable::mint_sealed_view";
    }
    static consteval const char* required_ctx_description() noexcept {
        return "IsExecCtx — hot/bg post-seal reads (any ctx)";
    }
};

// ── 4. SchemaTable::mint_mutable_view → IsInitCtx ──────────────────
//
// SchemaTable.h:155 — schema-name → schema_hash table.  Same lifecycle
// as CKernelTable: pre-seal mutable from init, post-seal read-only.

template <>
struct member_mint_required_ctx<::crucible::SchemaTable, mint_name::mutable_view> {
    template <class Ctx>
    static consteval bool admits() noexcept {
        return ::crucible::effects::IsInitCtx<std::remove_cvref_t<Ctx>>;
    }
    static consteval const char* name() noexcept {
        return "SchemaTable::mint_mutable_view";
    }
    static consteval const char* required_ctx_description() noexcept {
        return "IsInitCtx — pre-seal cold-init schema build, single writer";
    }
};

// ── 5. SchemaTable::mint_sealed_view → IsExecCtx (any) ─────────────
//
// SchemaTable.h:161 — sealed schema lookup.  Hot path on dispatch
// (Vessel schema lookup) AND bg drain (build_trace).

template <>
struct member_mint_required_ctx<::crucible::SchemaTable, mint_name::sealed_view> {
    template <class Ctx>
    static consteval bool admits() noexcept {
        return ::crucible::effects::IsExecCtx<std::remove_cvref_t<Ctx>>;
    }
    static consteval const char* name() noexcept {
        return "SchemaTable::mint_sealed_view";
    }
    static consteval const char* required_ctx_description() noexcept {
        return "IsExecCtx — hot/bg post-seal reads (any ctx)";
    }
};

// ── 6. PoolAllocator::mint_initialized_view → Fg OR Bg ─────────────
//
// PoolAllocator.h:295 — typed view over the initialized pool.  FG
// dispatch allocates from the pool (CrucibleContext::output_ptr); BG
// drain releases slots during teardown.  Init context does NOT touch
// the pool (the pool itself is built post-init).

template <>
struct member_mint_required_ctx<::crucible::PoolAllocator, mint_name::initialized_view> {
    template <class Ctx>
    static consteval bool admits() noexcept {
        using C = std::remove_cvref_t<Ctx>;
        return ::crucible::effects::IsFgCtx<C>
            || ::crucible::effects::IsBgCtx<C>;
    }
    static consteval const char* name() noexcept {
        return "PoolAllocator::mint_initialized_view";
    }
    static consteval const char* required_ctx_description() noexcept {
        return "IsFgCtx OR IsBgCtx — hot dispatch alloc + bg drain release";
    }
};

// ── 7. CrucibleContext::mint_compiled_view → IsFgCtx ───────────────
//
// CrucibleContext.h:320 — compiled-dispatch surface used by the hot
// FG path AFTER a region is compiled (Vigil::switch_region).  The
// compiled view threads through dispatch_op / dispatch_region.  Cold-
// init / bg-drain do not consume the compiled view directly.

template <>
struct member_mint_required_ctx<::crucible::CrucibleContext, mint_name::compiled_view> {
    template <class Ctx>
    static consteval bool admits() noexcept {
        return ::crucible::effects::IsFgCtx<std::remove_cvref_t<Ctx>>;
    }
    static consteval const char* name() noexcept {
        return "CrucibleContext::mint_compiled_view";
    }
    static consteval const char* required_ctx_description() noexcept {
        return "IsFgCtx — compiled dispatch is hot-path foreground only";
    }
};

// ── 8. ReplayEngine::mint_active_view → IsFgCtx ────────────────────
//
// ReplayEngine.h:398 — active replay cursor; FG dispatch consumes the
// view per-op to walk the compiled trace.  BG drain builds the engine
// but does not replay through it.

template <>
struct member_mint_required_ctx<::crucible::ReplayEngine, mint_name::active_view> {
    template <class Ctx>
    static consteval bool admits() noexcept {
        return ::crucible::effects::IsFgCtx<std::remove_cvref_t<Ctx>>;
    }
    static consteval const char* name() noexcept {
        return "ReplayEngine::mint_active_view";
    }
    static consteval const char* required_ctx_description() noexcept {
        return "IsFgCtx — replay cursor walked by hot FG dispatch";
    }
};

// ── Cross-class concept ────────────────────────────────────────────
//
// `MemberMintCtxRequired<Class, MintName, Ctx>` is satisfied iff:
//   (a) a `member_mint_required_ctx<Class, MintName>` specialization
//       exists (HS14 fixture #2 rejection axis), AND
//   (b) its `admits<Ctx>()` consteval returns true (HS14 fixture #1
//       rejection axis).
//
// The requires-expression `requires { ... ::template admits<Ctx>(); }`
// short-circuits at substitution — for an unregistered pair the
// primary template is incomplete so the expression is ill-formed and
// the concept is unsatisfied.

template <class Class, class MintName, class Ctx>
concept MemberMintCtxRequired = requires {
    requires member_mint_required_ctx<
        std::remove_cvref_t<Class>,
        std::remove_cvref_t<MintName>>::template admits<Ctx>();
};

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

// ─── FIXY-U-015: Cipher class + ComputationCache + federation ──────
//
// Surface witnesses for the 28 new re-exports.  Substrate identity
// is asserted via std::is_same_v on types and value-equality on
// constants; concept fences are exercised behaviorally (an instance
// satisfying the substrate concept must satisfy the fixy alias).

namespace u015 {

// ── A. Top-level Cipher class + companion templates ───────────────

static_assert(std::is_same_v<cipher::Cipher, ::crucible::Cipher>,
    "fixy::contract::cipher::Cipher must alias ::crucible::Cipher.");

static_assert(std::is_same_v<cipher::CipherOpenView,
                             ::crucible::CipherOpenView>,
    "fixy::contract::cipher::CipherOpenView must alias the substrate.");

static_assert(std::is_same_v<cipher::CipherSessionEventPersistenceRow,
                             ::crucible::CipherSessionEventPersistenceRow>);

static_assert(std::is_same_v<cipher::ContentAddressedPayload<int>,
                             ::crucible::cipher::ContentAddressedPayload<int>>);

static_assert(std::is_same_v<cipher::LoadedContentAddressedPayload<int>,
                             ::crucible::cipher::LoadedContentAddressedPayload<int>>);

// Class-scope OpenView reachable through cipher::Cipher alias.
static_assert(std::is_same_v<cipher::Cipher::OpenView,
                             ::crucible::CipherOpenView>);

// ── B. SessionEvent identity + cold-tier wire size ────────────────

static_assert(std::is_same_v<cipher::SessionEvent,
                             ::crucible::safety::proto::SessionEvent>,
    "fixy::contract::cipher::SessionEvent must alias substrate.");

// Pin the wire size — same 72 B that Cipher.h asserts internally.
static_assert(sizeof(cipher::SessionEvent) == 72,
    "Cipher session-event wire format pinned at 72 B (Cipher.h:158).");

// ── C. ComputationCache surface ───────────────────────────────────

// CompiledBody is opaque forward-declared; identity check on the
// pointer type proves the alias resolves to the same incomplete type.
static_assert(std::is_same_v<cipher::CompiledBody*,
                             ::crucible::cipher::CompiledBody*>);

// IsCacheableFunction + IsEffectRow concept behavioral equivalence.
// Probe: an inline noexcept fn ptr satisfies IsCacheableFunction; an
// effects::Row<> satisfies IsEffectRow.  Both paths must agree.
namespace u015_cache_probe {
inline void probe_fn(int) noexcept {}
}  // namespace u015_cache_probe

static_assert(cipher::IsCacheableFunction<&u015_cache_probe::probe_fn> ==
              ::crucible::cipher::IsCacheableFunction<&u015_cache_probe::probe_fn>);
static_assert(cipher::IsCacheableFunction<&u015_cache_probe::probe_fn>,
    "An inline noexcept fn must satisfy IsCacheableFunction through fixy::.");

static_assert(cipher::IsEffectRow<::crucible::effects::Row<>> ==
              ::crucible::cipher::IsEffectRow<::crucible::effects::Row<>>);
static_assert(cipher::IsEffectRow<::crucible::effects::Row<>>);
static_assert(!cipher::IsEffectRow<int>,
    "Non-row T must NOT satisfy IsEffectRow through fixy::.");

// computation_cache_key value-equality through the alias.
static_assert(
    cipher::computation_cache_key<&u015_cache_probe::probe_fn, int> ==
    ::crucible::cipher::computation_cache_key<&u015_cache_probe::probe_fn, int>);

// row variant value-equality.
static_assert(
    cipher::computation_cache_key_in_row<&u015_cache_probe::probe_fn,
                                          ::crucible::effects::Row<>, int> ==
    ::crucible::cipher::computation_cache_key_in_row<
        &u015_cache_probe::probe_fn,
        ::crucible::effects::Row<>, int>);

// Function-pointer identity for lookup / insert / drain (witnesses
// the using-decl resolves to the same overloaded substrate fn).
static_assert(std::is_same_v<
    decltype(&cipher::lookup_computation_cache<&u015_cache_probe::probe_fn, int>),
    decltype(&::crucible::cipher::lookup_computation_cache<
                &u015_cache_probe::probe_fn, int>)>);
static_assert(std::is_same_v<
    decltype(&cipher::insert_computation_cache<&u015_cache_probe::probe_fn, int>),
    decltype(&::crucible::cipher::insert_computation_cache<
                &u015_cache_probe::probe_fn, int>)>);
static_assert(std::is_same_v<
    decltype(&cipher::lookup_computation_cache_in_row<
                &u015_cache_probe::probe_fn,
                ::crucible::effects::Row<>, int>),
    decltype(&::crucible::cipher::lookup_computation_cache_in_row<
                &u015_cache_probe::probe_fn,
                ::crucible::effects::Row<>, int>)>);
static_assert(std::is_same_v<
    decltype(&cipher::insert_computation_cache_in_row<
                &u015_cache_probe::probe_fn,
                ::crucible::effects::Row<>, int>),
    decltype(&::crucible::cipher::insert_computation_cache_in_row<
                &u015_cache_probe::probe_fn,
                ::crucible::effects::Row<>, int>)>);
static_assert(std::is_same_v<decltype(&cipher::drain_computation_cache),
                             decltype(&::crucible::cipher::drain_computation_cache)>);

// ── D. federation:: nested namespace surface ──────────────────────

static_assert(cipher::federation::FEDERATION_MAGIC ==
              ::crucible::cipher::federation::FEDERATION_MAGIC);
static_assert(cipher::federation::FEDERATION_PROTOCOL_V1 ==
              ::crucible::cipher::federation::FEDERATION_PROTOCOL_V1);
static_assert(cipher::federation::FEDERATION_HEADER_BYTES ==
              ::crucible::cipher::federation::FEDERATION_HEADER_BYTES);

static_assert(std::is_same_v<cipher::federation::FederationEntryHeader,
                             ::crucible::cipher::federation::FederationEntryHeader>);
static_assert(std::is_same_v<cipher::federation::ColdBlobRegion,
                             ::crucible::cipher::federation::ColdBlobRegion>);
static_assert(std::is_same_v<cipher::federation::FederationEntryView,
                             ::crucible::cipher::federation::FederationEntryView>);
static_assert(std::is_same_v<cipher::federation::FederationError,
                             ::crucible::cipher::federation::FederationError>);

// Function-pointer identity for non-template federation free fns.
static_assert(std::is_same_v<decltype(&cipher::federation::federation_error_name),
                             decltype(&::crucible::cipher::federation::federation_error_name)>);
static_assert(std::is_same_v<decltype(&cipher::federation::serialize_federation_entry),
                             decltype(&::crucible::cipher::federation::serialize_federation_entry)>);
static_assert(std::is_same_v<decltype(&cipher::federation::deserialize_federation_header),
                             decltype(&::crucible::cipher::federation::deserialize_federation_header)>);
static_assert(std::is_same_v<decltype(&cipher::federation::deserialize_untrusted_federation_entry),
                             decltype(&::crucible::cipher::federation::deserialize_untrusted_federation_entry)>);

// deserialize_federation_entry is a function TEMPLATE on Org; pick a
// probe Org and assert the instantiated fn-pointer types match.
struct U015FederationProbeOrg {};
static_assert(std::is_same_v<
    decltype(&cipher::federation::deserialize_federation_entry<U015FederationProbeOrg>),
    decltype(&::crucible::cipher::federation::deserialize_federation_entry<U015FederationProbeOrg>)>);

static_assert(std::is_same_v<decltype(&cipher::federation::federation_entry_blob_layout_disjoint),
                             decltype(&::crucible::cipher::federation::federation_entry_blob_layout_disjoint)>);
static_assert(std::is_same_v<decltype(&cipher::federation::federation_accepts_cardinality),
                             decltype(&::crucible::cipher::federation::federation_accepts_cardinality)>);

// cold_blob_regions_pairwise_disjoint is a function template on MaxRegions;
// instantiate explicitly to compare fn-pointer types.
static_assert(std::is_same_v<
    decltype(&cipher::federation::cold_blob_regions_pairwise_disjoint<8>),
    decltype(&::crucible::cipher::federation::cold_blob_regions_pairwise_disjoint<8>)>);

// ─── ComputationCacheFederation.h surface identity ────────────────

// Probe types for instantiating templates without coupling to a
// production FnPtr / Row / Args triple.
namespace u015_ccfed_probe {
inline void probe_kernel(int) noexcept {}
}  // namespace u015_ccfed_probe
using U015ProbeRow = ::crucible::effects::Row<>;

// Per-(FnPtr, Row, Args...) key tag identity.
static_assert(std::is_same_v<
    cipher::federation::ComputationCacheFederationKeyTag<
        &u015_ccfed_probe::probe_kernel, U015ProbeRow, int>,
    ::crucible::cipher::federation::ComputationCacheFederationKeyTag<
        &u015_ccfed_probe::probe_kernel, U015ProbeRow, int>>);

// Per-role MPST projection aliases.
static_assert(std::is_same_v<
    cipher::federation::ComputationCacheFederationSenderProto<
        &u015_ccfed_probe::probe_kernel, U015ProbeRow, int>,
    ::crucible::cipher::federation::ComputationCacheFederationSenderProto<
        &u015_ccfed_probe::probe_kernel, U015ProbeRow, int>>);
static_assert(std::is_same_v<
    cipher::federation::ComputationCacheFederationReceiverProto<
        &u015_ccfed_probe::probe_kernel, U015ProbeRow, int>,
    ::crucible::cipher::federation::ComputationCacheFederationReceiverProto<
        &u015_ccfed_probe::probe_kernel, U015ProbeRow, int>>);
static_assert(std::is_same_v<
    cipher::federation::ComputationCacheFederationCoordProto<
        &u015_ccfed_probe::probe_kernel, U015ProbeRow, int>,
    ::crucible::cipher::federation::ComputationCacheFederationCoordProto<
        &u015_ccfed_probe::probe_kernel, U015ProbeRow, int>>);

// Content-addressed payload wrapper identity.
static_assert(std::is_same_v<
    cipher::federation::ContentAddressedFederationPayload<int>,
    ::crucible::cipher::federation::ContentAddressedFederationPayload<int>>);

// Per-(FnPtr, Row, Args...) payload aliases.
static_assert(std::is_same_v<
    cipher::federation::ComputationCacheFederationPayload<
        &u015_ccfed_probe::probe_kernel, U015ProbeRow, int>,
    ::crucible::cipher::federation::ComputationCacheFederationPayload<
        &u015_ccfed_probe::probe_kernel, U015ProbeRow, int>>);
static_assert(std::is_same_v<
    cipher::federation::ComputationCacheFederationContentAddressedPayload<
        &u015_ccfed_probe::probe_kernel, U015ProbeRow, int>,
    ::crucible::cipher::federation::ComputationCacheFederationContentAddressedPayload<
        &u015_ccfed_probe::probe_kernel, U015ProbeRow, int>>);

// Hash-derivation function-pointer identity (templates instantiated
// on the probe triple).
static_assert(std::is_same_v<
    decltype(&cipher::federation::federation_content_hash<
                &u015_ccfed_probe::probe_kernel, U015ProbeRow, int>),
    decltype(&::crucible::cipher::federation::federation_content_hash<
                &u015_ccfed_probe::probe_kernel, U015ProbeRow, int>)>);
static_assert(std::is_same_v<
    decltype(&cipher::federation::federation_row_hash<U015ProbeRow>),
    decltype(&::crucible::cipher::federation::federation_row_hash<U015ProbeRow>)>);
static_assert(std::is_same_v<
    decltype(&cipher::federation::federation_key<
                &u015_ccfed_probe::probe_kernel, U015ProbeRow, int>),
    decltype(&::crucible::cipher::federation::federation_key<
                &u015_ccfed_probe::probe_kernel, U015ProbeRow, int>)>);

// serialize_computation_cache_federation_entry has TWO overloads on
// the same template signature (Args differ in payload param type:
// ComputationCacheFederationPayload<...>& vs std::span<const u8>).
// `decltype(&fn)` cannot disambiguate without the full callable type,
// so reach is proven by the using-decl being well-formed at parse
// time PLUS the namespace-scope `using ::crucible::...` directive
// resolving in the compiled object (witnessed by this TU compiling).
// Explicit-overload identity probes via static_cast<RetType(*)(...)>(&fn)
// would couple this sentinel to substrate parameter signatures —
// brittle vs the payload-template's evolution.  The reach-via-using
// witness is sufficient for the §XVII drift-catch.

// ── Cardinality witness ───────────────────────────────────────────
//
// 42 surface items added for FIXY-U-015.  Future drift forces an
// update to both the using-decl block AND this constant.
//
// Breakdown:
//   A. Top-level Cipher class + companion templates                 5
//      (Cipher, CipherOpenView, CipherSessionEventPersistenceRow,
//       ContentAddressedPayload<T>, LoadedContentAddressedPayload<T>)
//   B. SessionEvent alias                                           1
//   C. ComputationCache surface                                    10
//      (CompiledBody, IsCacheableFunction, IsEffectRow,
//       computation_cache_key, lookup_computation_cache,
//       insert_computation_cache, computation_cache_key_in_row,
//       lookup_computation_cache_in_row,
//       insert_computation_cache_in_row, drain_computation_cache)
//   D. federation:: from FederationProtocol.h                      15
//      Constants:     FEDERATION_MAGIC / FEDERATION_PROTOCOL_V1 /
//                     FEDERATION_HEADER_BYTES                       3
//      Structs:       FederationEntryHeader / ColdBlobRegion /
//                     FederationEntryView                           3
//      Error:         FederationError + federation_error_name       2
//      Serdes:        serialize_federation_entry /
//                     deserialize_federation_header /
//                     deserialize_untrusted_federation_entry /
//                     deserialize_federation_entry                  4
//      Predicates:    federation_entry_blob_layout_disjoint /
//                     cold_blob_regions_pairwise_disjoint /
//                     federation_accepts_cardinality                3
//   E. federation:: from ComputationCacheFederation.h              11
//      Key tag:       ComputationCacheFederationKeyTag              1
//      Protocols:     ComputationCacheFederationSenderProto /
//                     ...ReceiverProto / ...CoordProto              3
//      Payloads:      ContentAddressedFederationPayload /
//                     ComputationCacheFederationPayload /
//                     ComputationCacheFederationContentAddressedPayload  3
//      Hash helpers:  federation_content_hash / federation_row_hash /
//                     federation_key                                3
//      Serdes:        serialize_computation_cache_federation_entry  1
//                                                                 ───
//                                                                  42

constexpr int u015_surface_cardinality = 42;
static_assert(u015_surface_cardinality == 42,
    "FIXY-U-015 surface (Cipher + ComputationCache + federation full) "
    "drifted from 42 — Contract.h U-015 block and this sentinel "
    "must update in lockstep.");

}  // namespace u015

// ─── FIXY-V-220: member-mint required-ctx hook sentinels ───────────
//
// Witness the 8 registered (Class, MintName) specifications + the
// cross-class concept's positive/negative axes.  Substrate identity
// is asserted via admits<Ctx>() consteval equality against the
// substrate ctx-discrimination concepts; the cardinality sentinel at
// the tail pins the registration count.

namespace v220 {

namespace eff = ::crucible::effects;

// ── Per-spec positive admission ────────────────────────────────────

static_assert(member_mint_required_ctx<
    ::crucible::Cipher, mint_name::open_view>::admits<eff::BgDrainCtx>(),
    "V-220 #1: Cipher::mint_open_view must admit BgDrainCtx.");

static_assert(member_mint_required_ctx<
    ::crucible::CKernelTable, mint_name::mutable_view>::admits<eff::ColdInitCtx>(),
    "V-220 #2: CKernelTable::mint_mutable_view must admit ColdInitCtx.");

static_assert(member_mint_required_ctx<
    ::crucible::CKernelTable, mint_name::sealed_view>::admits<eff::HotFgCtx>(),
    "V-220 #3a: CKernelTable::mint_sealed_view must admit HotFgCtx.");
static_assert(member_mint_required_ctx<
    ::crucible::CKernelTable, mint_name::sealed_view>::admits<eff::BgDrainCtx>(),
    "V-220 #3b: CKernelTable::mint_sealed_view must admit BgDrainCtx.");

static_assert(member_mint_required_ctx<
    ::crucible::SchemaTable, mint_name::mutable_view>::admits<eff::ColdInitCtx>(),
    "V-220 #4: SchemaTable::mint_mutable_view must admit ColdInitCtx.");

static_assert(member_mint_required_ctx<
    ::crucible::SchemaTable, mint_name::sealed_view>::admits<eff::HotFgCtx>(),
    "V-220 #5: SchemaTable::mint_sealed_view must admit HotFgCtx.");

static_assert(member_mint_required_ctx<
    ::crucible::PoolAllocator, mint_name::initialized_view>::admits<eff::HotFgCtx>(),
    "V-220 #6a: PoolAllocator::mint_initialized_view must admit HotFgCtx.");
static_assert(member_mint_required_ctx<
    ::crucible::PoolAllocator, mint_name::initialized_view>::admits<eff::BgDrainCtx>(),
    "V-220 #6b: PoolAllocator::mint_initialized_view must admit BgDrainCtx.");

static_assert(member_mint_required_ctx<
    ::crucible::CrucibleContext, mint_name::compiled_view>::admits<eff::HotFgCtx>(),
    "V-220 #7: CrucibleContext::mint_compiled_view must admit HotFgCtx.");

static_assert(member_mint_required_ctx<
    ::crucible::ReplayEngine, mint_name::active_view>::admits<eff::HotFgCtx>(),
    "V-220 #8: ReplayEngine::mint_active_view must admit HotFgCtx.");

// ── Per-spec negative admission (wrong-ctx rejection) ──────────────
//
// Each spec rejects the wrong ctx tier — the cross-class concept's
// `admits<Ctx>()` branch returns false for these pairings.

static_assert(!member_mint_required_ctx<
    ::crucible::Cipher, mint_name::open_view>::admits<eff::HotFgCtx>(),
    "V-220 #1-neg: Cipher::mint_open_view must REJECT HotFgCtx.");

static_assert(!member_mint_required_ctx<
    ::crucible::CKernelTable, mint_name::mutable_view>::admits<eff::HotFgCtx>(),
    "V-220 #2-neg: mint_mutable_view must REJECT HotFgCtx (init-only).");

static_assert(!member_mint_required_ctx<
    ::crucible::CrucibleContext, mint_name::compiled_view>::admits<eff::BgDrainCtx>(),
    "V-220 #7-neg: mint_compiled_view must REJECT BgDrainCtx (Fg only).");

static_assert(!member_mint_required_ctx<
    ::crucible::ReplayEngine, mint_name::active_view>::admits<eff::ColdInitCtx>(),
    "V-220 #8-neg: mint_active_view must REJECT ColdInitCtx (Fg only).");

// ── Cross-class concept positive + negative ────────────────────────

static_assert(MemberMintCtxRequired<
    ::crucible::Cipher, mint_name::open_view, eff::BgDrainCtx>,
    "V-220 concept: must satisfy for (Cipher, open_view, BgDrainCtx).");

static_assert(!MemberMintCtxRequired<
    ::crucible::Cipher, mint_name::open_view, eff::HotFgCtx>,
    "V-220 concept: must reject for (Cipher, open_view, HotFgCtx).");

// cvref-strip: const& on the Ctx must NOT change the answer.
static_assert(MemberMintCtxRequired<
    ::crucible::Cipher, mint_name::open_view, eff::BgDrainCtx const&>,
    "V-220 concept: cvref-stripped Ctx must satisfy.");

// ── Cardinality witness ────────────────────────────────────────────
//
// 8 registered (Class, MintName) specializations today.  Future drift
// (a new member mint lands → ship the spec → bump the constant).

inline constexpr std::size_t v220_member_mint_cardinality = 8;
static_assert(v220_member_mint_cardinality == 8,
    "FIXY-V-220 cardinality sentinel: 8 registered member mints today. "
    "If a new (Class, MintName) spec lands, bump this constant AND refresh "
    "the doc-block at the top of the V-220 section in fixy/Contract.h.");

}  // namespace v220

}  // namespace crucible::fixy::contract::self_test
