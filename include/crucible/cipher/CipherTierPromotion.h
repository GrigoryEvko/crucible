#pragma once

// ── CipherTier promotion / demotion mint sites ─────────────────────
//
// CipherTier<Tier, T> deliberately forbids arbitrary strengthening:
// a Cold value cannot call `relax<Hot>()` and claim RAM-replicated
// residency.  Real tier movement therefore needs named mint sites.
// These factories are the compile-time boundary for that movement:
//
//   mint_promote<Cold, Warm/Hot>(...)  — backend materialized a hotter tier.
//   mint_demote<Hot/Warm, Cold>(...)   — eviction or archive path downgraded.
//   mint_restore(...)                  — cold handle restored into Warm.
//
// Phase 5 still owns the actual peer-RAM RAID and S3/GCS backends; this
// header ships the type-level API now so callers cannot hand-roll tier
// casts while those backends are being wired.

#include <crucible/Types.h>
#include <crucible/safety/CipherTier.h>
#include <crucible/safety/Decide.h>
#include <crucible/sessions/SessionDelegate.h>

#include <concepts>
#include <expected>
#include <type_traits>
#include <utility>

namespace crucible::cipher {

using ::crucible::safety::CipherTier;
using ::crucible::safety::CipherTierLattice;
using ::crucible::safety::CipherTierTag_v;

// CONTRACT-117 (Cipher tier-transition cite): promote/demote admission VC
// is discharged via `decide::tier_replaces(stronger, weaker)` —
// "stronger candidate is at-least-as-strong as the weaker required tier."
// CipherTierLattice's chain ordinal convention (Cold=0 < Warm=1 < Hot=2,
// per algebra/lattices/CipherTierLattice.h §"Direction convention") makes
// this a single integer compare on the underlying type.
//
// Promote (From → stronger To): "To replaces From" ≡ tier_replaces(To, From).
// Demote (From → weaker To):    "From replaces To" ≡ tier_replaces(From, To).
//
// Cite-pair contributed under the project-wide convention so a single
// review-discoverable VC discharges every chain-lattice replacement
// (KernelCache promote, Cipher publish_hot/warm/cold, Forge Phase E
// recipe admission, BackgroundThread phase promotion).
template <CipherTierTag_v From, CipherTierTag_v To>
inline constexpr bool can_promote_tier_v =
    ::crucible::decide::tier_replaces(To, From);

template <CipherTierTag_v From, CipherTierTag_v To>
inline constexpr bool can_demote_tier_v =
    ::crucible::decide::tier_replaces(From, To);

template <CipherTierTag_v From, CipherTierTag_v To, typename T>
concept PromotableTier =
    can_promote_tier_v<From, To> && std::move_constructible<T>;

template <CipherTierTag_v From, CipherTierTag_v To, typename T>
concept DemotableTier =
    can_demote_tier_v<From, To> && std::move_constructible<T>;

// ── Verification-scope honesty (fixy-CR-10) ─────────────────────────
//
// Pre-CR-10, `mint_restore` ran the supplied `content_hash` against
// `cold_handle.peek()` only inside an `if constexpr (same_as<T,
// ContentHash>)` branch.  For any T != ContentHash (every production
// payload — TraceRing buffer, KernelCache entry, weight tensor, ...)
// the supplied hash was accepted blindly and the cold-to-warm
// promotion succeeded WITHOUT verifying that the cold blob's bytes
// actually hashed to the claimed value.
//
// The closure is a customization point:
// `content_hash_projection<T>::project(const T&) -> ContentHash`.
// Any restorable T MUST specialize this trait (or ship an
// equivalently-named static accessor).  The built-in specialization
// for `T = ContentHash` returns the value itself.  mint_restore now
// gates uniformly on this projection: the cold-payload-projected
// hash MUST equal the supplied `content_hash` argument, or the mint
// returns `RestoreError::ContentHashMismatch`.  T types without a
// projection fail at the `RestorableHashed` concept gate.
//
// What the projection cannot replace: BYTE-LEVEL integrity of the
// cold blob coming off durable storage.  The projection is an
// in-memory consistency check between the supplied caller-claimed
// hash and what the cold handle's in-memory representation reports.
// The byte-level "did the disk lie to us" check belongs in the
// Phase-5 backend that materializes `ColdTierHandle<T>` from S3/GCS
// before mint_restore is ever called.  That layer is the byte-hash
// authority; mint_restore is the post-materialization gate.

template <typename T>
struct content_hash_projection;

template <>
struct content_hash_projection<ContentHash> {
    [[nodiscard]] static constexpr ContentHash project(
        const ContentHash& value) noexcept {
        return value;
    }
};

template <typename T>
concept RestorableHashed = requires(const T& v) {
    { content_hash_projection<T>::project(v) }
        -> std::same_as<ContentHash>;
};

template <typename T>
concept RestorableTier =
    std::move_constructible<T> && RestorableHashed<T>;

template <CipherTierTag_v From, CipherTierTag_v To, typename T>
    requires PromotableTier<From, To, T>
[[nodiscard]] constexpr CipherTier<To, T>
mint_promote(CipherTier<From, T> source)
    noexcept(std::is_nothrow_move_constructible_v<T>)
{
    return CipherTier<To, T>{std::move(source).consume()};
}

template <CipherTierTag_v From, CipherTierTag_v To, typename T>
    requires DemotableTier<From, To, T>
[[nodiscard]] constexpr CipherTier<To, T>
mint_demote(CipherTier<From, T> source)
    noexcept(std::is_nothrow_move_constructible_v<T>)
{
    return CipherTier<To, T>{std::move(source).consume()};
}

enum class RestoreError : std::uint8_t {
    EmptyContentHash,
    EmptyColdHandle,
    ContentHashMismatch,
    BackendUnavailable,
};

[[nodiscard]] consteval const char* restore_error_name(RestoreError error) noexcept {
    switch (error) {
        case RestoreError::EmptyContentHash:    return "EmptyContentHash";
        case RestoreError::EmptyColdHandle:     return "EmptyColdHandle";
        case RestoreError::ContentHashMismatch: return "ContentHashMismatch";
        case RestoreError::BackendUnavailable:  return "BackendUnavailable";
        default:                                return "<unknown RestoreError>";
    }
}

template <typename T>
using ColdTierHandle = ::crucible::safety::cipher_tier::Cold<T>;

template <typename T>
using WarmTierHandle = ::crucible::safety::cipher_tier::Warm<T>;

template <typename T>
using HotTierHandle = ::crucible::safety::cipher_tier::Hot<T>;

template <typename T>
    requires RestorableTier<T>
[[nodiscard]] constexpr std::expected<WarmTierHandle<T>, RestoreError>
mint_restore(ColdTierHandle<T> cold_handle, ContentHash content_hash)
    noexcept(std::is_nothrow_move_constructible_v<T>)
{
    if (!static_cast<bool>(content_hash)) {
        return std::unexpected(RestoreError::EmptyContentHash);
    }

    // fixy-CR-10: uniform projection — fires for EVERY restorable T,
    // not only T = ContentHash.  Production payloads must opt in via
    // `content_hash_projection<T>::project` (see header doc-block).
    const ContentHash cold_projected =
        content_hash_projection<T>::project(cold_handle.peek());
    if (!static_cast<bool>(cold_projected)) {
        return std::unexpected(RestoreError::EmptyColdHandle);
    }
    if (cold_projected != content_hash) {
        return std::unexpected(RestoreError::ContentHashMismatch);
    }

    return mint_promote<CipherTierTag_v::Cold, CipherTierTag_v::Warm>(
        std::move(cold_handle));
}

template <typename T>
using HotPromotePayload = HotTierHandle<T>;

template <typename T>
using HotPromote =
    ::crucible::safety::proto::Send<
        HotPromotePayload<T>,
        ::crucible::safety::proto::End>;

template <typename T, typename K = ::crucible::safety::proto::End>
using HotPromoteDelegate =
    ::crucible::safety::proto::Delegate<HotPromote<T>, K>;

template <typename T, typename K = ::crucible::safety::proto::End>
using HotPromoteAccept =
    ::crucible::safety::proto::Accept<HotPromote<T>, K>;

namespace detail::cipher_tier_promotion_self_test {

using HotHash  = HotTierHandle<ContentHash>;
using WarmHash = WarmTierHandle<ContentHash>;
using ColdHash = ColdTierHandle<ContentHash>;

static_assert(can_promote_tier_v<CipherTierTag_v::Cold,
                                 CipherTierTag_v::Warm>);
static_assert(can_promote_tier_v<CipherTierTag_v::Cold,
                                 CipherTierTag_v::Hot>);
static_assert(can_promote_tier_v<CipherTierTag_v::Warm,
                                 CipherTierTag_v::Hot>);
static_assert(!can_promote_tier_v<CipherTierTag_v::Hot,
                                  CipherTierTag_v::Cold>);

static_assert(can_demote_tier_v<CipherTierTag_v::Hot,
                                CipherTierTag_v::Warm>);
static_assert(can_demote_tier_v<CipherTierTag_v::Hot,
                                CipherTierTag_v::Cold>);
static_assert(can_demote_tier_v<CipherTierTag_v::Warm,
                                CipherTierTag_v::Cold>);
static_assert(!can_demote_tier_v<CipherTierTag_v::Cold,
                                 CipherTierTag_v::Hot>);

static_assert(std::is_same_v<
    decltype(mint_promote<CipherTierTag_v::Cold, CipherTierTag_v::Warm>(
        ColdHash{ContentHash{1}})),
    WarmHash>);
static_assert(std::is_same_v<
    decltype(mint_demote<CipherTierTag_v::Hot, CipherTierTag_v::Cold>(
        HotHash{ContentHash{2}})),
    ColdHash>);

using HotPromoteHash = HotPromote<ContentHash>;
using HotPromoteCarrier = HotPromoteDelegate<ContentHash>;
using HotPromotePeer = HotPromoteAccept<ContentHash>;

static_assert(::crucible::safety::proto::is_well_formed_v<HotPromoteHash>);
static_assert(::crucible::safety::proto::DelegatesTo<
    HotPromoteCarrier, HotPromoteHash>);
static_assert(::crucible::safety::proto::AcceptsFrom<
    HotPromotePeer, HotPromoteHash>);
static_assert(std::is_same_v<
    ::crucible::safety::proto::dual_of_t<HotPromoteCarrier>,
    HotPromotePeer>);

}  // namespace detail::cipher_tier_promotion_self_test

}  // namespace crucible::cipher
