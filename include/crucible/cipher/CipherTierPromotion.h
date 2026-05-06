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
#include <crucible/sessions/SessionDelegate.h>

#include <concepts>
#include <expected>
#include <type_traits>
#include <utility>

namespace crucible::cipher {

using ::crucible::safety::CipherTier;
using ::crucible::safety::CipherTierLattice;
using ::crucible::safety::CipherTierTag_v;

template <CipherTierTag_v From, CipherTierTag_v To>
inline constexpr bool can_promote_tier_v =
    CipherTierLattice::leq(From, To);

template <CipherTierTag_v From, CipherTierTag_v To>
inline constexpr bool can_demote_tier_v =
    CipherTierLattice::leq(To, From);

template <CipherTierTag_v From, CipherTierTag_v To, typename T>
concept PromotableTier =
    can_promote_tier_v<From, To> && std::move_constructible<T>;

template <CipherTierTag_v From, CipherTierTag_v To, typename T>
concept DemotableTier =
    can_demote_tier_v<From, To> && std::move_constructible<T>;

template <typename T>
concept RestorableTier = std::move_constructible<T>;

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

    if constexpr (std::same_as<T, ContentHash>) {
        const ContentHash& cold_hash = cold_handle.peek();
        if (!static_cast<bool>(cold_hash)) {
            return std::unexpected(RestoreError::EmptyColdHandle);
        }
        if (cold_hash != content_hash) {
            return std::unexpected(RestoreError::ContentHashMismatch);
        }
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
