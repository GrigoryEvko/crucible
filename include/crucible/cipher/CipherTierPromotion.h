#pragma once

// A tier band refuses to strengthen itself, so a cold value cannot
// relabel itself as replicated in memory. Every real movement between
// tiers goes through one of the factories below.

#include <crucible/Types.h>
#include <fixy/Bands.h>
#include <fixy/session/Delegate.h>
#include <fixy/session/Protocol.h>
#include <foundation/contracts/Decide.h>
#include <foundation/reflect/EnumName.h>

#include <concepts>
#include <expected>
#include <type_traits>
#include <utility>

namespace crucible::cipher {

using ::fixy::CipherTier;
using ::fixy::CipherTierLattice;
using ::fixy::CipherTierTag_v;

// The two argument orders differ, and that difference is the whole
// content of the pair. The tiers form a chain from cold through warm
// to hot, and the predicate asks whether its first argument is at
// least as strong as its second. A promotion therefore asks whether
// the destination is strong enough to replace the source. A demotion
// asks the reverse.
template <CipherTierTag_v From, CipherTierTag_v To>
inline constexpr bool can_promote_tier_v = ::foundation::decide::tier_replaces(To, From);

template <CipherTierTag_v From, CipherTierTag_v To>
inline constexpr bool can_demote_tier_v = ::foundation::decide::tier_replaces(From, To);

template <CipherTierTag_v From, CipherTierTag_v To, typename T>
concept PromotableTier = can_promote_tier_v<From, To> && std::move_constructible<T>;

template <CipherTierTag_v From, CipherTierTag_v To, typename T>
concept DemotableTier = can_demote_tier_v<From, To> && std::move_constructible<T>;

// A restorable payload specializes this trait, and the concept below
// refuses any type that does not. The restore gate therefore runs for
// every payload rather than for the hash type alone.
//
// The gate compares the hash the caller claims against the one the
// cold handle reports in memory. It says nothing about the bytes that
// came off durable storage. Whoever materializes a cold handle out of
// that storage is the byte-level authority, and this runs after that
// step, not instead of it.

template <typename T>
struct content_hash_projection;

template <>
struct content_hash_projection<ContentHash> {
    [[nodiscard]] static constexpr ContentHash project(const ContentHash& value) noexcept { return value; }
};

template <typename T>
concept RestorableHashed = requires(const T& v) {
    { content_hash_projection<T>::project(v) } -> std::same_as<ContentHash>;
};

template <typename T>
concept RestorableTier = std::move_constructible<T> && RestorableHashed<T>;

// A promotion and a demotion each move the value into the band of the
// destination tier.  The band's own door, mint_band, asserts the tier,
// and these two are the only sites that assert a tier the source did
// not hold.
template <CipherTierTag_v From, CipherTierTag_v To, typename T>
    requires PromotableTier<From, To, T>
[[nodiscard]] constexpr CipherTier<To, T>
mint_promote(CipherTier<From, T> source) noexcept(std::is_nothrow_move_constructible_v<T>) {
    return ::fixy::mint_band<CipherTier<To, T>>(std::move(source).consume());
}

template <CipherTierTag_v From, CipherTierTag_v To, typename T>
    requires DemotableTier<From, To, T>
[[nodiscard]] constexpr CipherTier<To, T>
mint_demote(CipherTier<From, T> source) noexcept(std::is_nothrow_move_constructible_v<T>) {
    return ::fixy::mint_band<CipherTier<To, T>>(std::move(source).consume());
}

// A diagnostic prints a refusal with ::foundation::reflect::enum_name.
enum class RestoreError : std::uint8_t {
    EmptyContentHash,
    EmptyColdHandle,
    ContentHashMismatch,
    BackendUnavailable,
};

template <typename T>
using ColdTierHandle = ::fixy::cipher_tier::Cold<T>;

template <typename T>
using WarmTierHandle = ::fixy::cipher_tier::Warm<T>;

template <typename T>
using HotTierHandle = ::fixy::cipher_tier::Hot<T>;

template <typename T>
    requires RestorableTier<T>
[[nodiscard]] constexpr std::expected<WarmTierHandle<T>, RestoreError>
mint_restore(ColdTierHandle<T> cold_handle,
             ContentHash content_hash) noexcept(std::is_nothrow_move_constructible_v<T>) {
    if (!static_cast<bool>(content_hash)) {
        return std::unexpected(RestoreError::EmptyContentHash);
    }

    const ContentHash cold_projected = content_hash_projection<T>::project(cold_handle.peek());
    if (!static_cast<bool>(cold_projected)) {
        return std::unexpected(RestoreError::EmptyColdHandle);
    }
    if (cold_projected != content_hash) {
        return std::unexpected(RestoreError::ContentHashMismatch);
    }

    return mint_promote<CipherTierTag_v::Cold, CipherTierTag_v::Warm>(std::move(cold_handle));
}

// The protocol of a hand-off of a hot replica: one hot-tier value and
// End.  HotPromoteDelegate and HotPromoteAccept state the hand-off of an
// endpoint of that protocol in a protocol type.  A hand-off that runs is
// a Send of the DelegatedSession that fixy/session/Delegate.h mints, and
// no mint admits a protocol that holds one of the two heads.
template <typename T>
using HotPromotePayload = HotTierHandle<T>;

template <typename T>
using HotPromote = ::fixy::session::Send<HotPromotePayload<T>, ::fixy::session::End>;

template <typename T, typename K = ::fixy::session::End>
using HotPromoteDelegate = ::fixy::session::Delegate<HotPromote<T>, K>;

template <typename T, typename K = ::fixy::session::End>
using HotPromoteAccept = ::fixy::session::Accept<HotPromote<T>, K>;

}  // namespace crucible::cipher
