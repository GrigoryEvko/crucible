#pragma once

// A tier wrapper refuses to strengthen itself, so a cold value cannot
// relabel itself as replicated in memory. Every real movement between
// tiers goes through one of the factories below.

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

// The two argument orders differ, and that difference is the whole
// content of the pair. The tiers form a chain from cold through warm
// to hot, and the predicate asks whether its first argument is at
// least as strong as its second. A promotion therefore asks whether
// the destination is strong enough to replace the source. A demotion
// asks the reverse.
template <CipherTierTag_v From, CipherTierTag_v To>
inline constexpr bool can_promote_tier_v = ::crucible::decide::tier_replaces(To, From);

template <CipherTierTag_v From, CipherTierTag_v To>
inline constexpr bool can_demote_tier_v = ::crucible::decide::tier_replaces(From, To);

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

template <CipherTierTag_v From, CipherTierTag_v To, typename T>
    requires PromotableTier<From, To, T>
[[nodiscard]] constexpr CipherTier<To, T>
mint_promote(CipherTier<From, T> source) noexcept(std::is_nothrow_move_constructible_v<T>) {
    return CipherTier<To, T>{std::move(source).consume()};
}

template <CipherTierTag_v From, CipherTierTag_v To, typename T>
    requires DemotableTier<From, To, T>
[[nodiscard]] constexpr CipherTier<To, T>
mint_demote(CipherTier<From, T> source) noexcept(std::is_nothrow_move_constructible_v<T>) {
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
        case RestoreError::EmptyContentHash:
            return "EmptyContentHash";
        case RestoreError::EmptyColdHandle:
            return "EmptyColdHandle";
        case RestoreError::ContentHashMismatch:
            return "ContentHashMismatch";
        case RestoreError::BackendUnavailable:
            return "BackendUnavailable";
        default:
            return "<unknown RestoreError>";
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

template <typename T>
using HotPromotePayload = HotTierHandle<T>;

template <typename T>
using HotPromote = ::crucible::safety::proto::Send<HotPromotePayload<T>, ::crucible::safety::proto::End>;

template <typename T, typename K = ::crucible::safety::proto::End>
using HotPromoteDelegate = ::crucible::safety::proto::Delegate<HotPromote<T>, K>;

template <typename T, typename K = ::crucible::safety::proto::End>
using HotPromoteAccept = ::crucible::safety::proto::Accept<HotPromote<T>, K>;

namespace detail::cipher_tier_promotion_self_test {

using HotHash = HotTierHandle<ContentHash>;
using WarmHash = WarmTierHandle<ContentHash>;
using ColdHash = ColdTierHandle<ContentHash>;

static_assert(can_promote_tier_v<CipherTierTag_v::Cold, CipherTierTag_v::Warm>);
static_assert(can_promote_tier_v<CipherTierTag_v::Cold, CipherTierTag_v::Hot>);
static_assert(can_promote_tier_v<CipherTierTag_v::Warm, CipherTierTag_v::Hot>);
static_assert(!can_promote_tier_v<CipherTierTag_v::Hot, CipherTierTag_v::Cold>);

static_assert(can_demote_tier_v<CipherTierTag_v::Hot, CipherTierTag_v::Warm>);
static_assert(can_demote_tier_v<CipherTierTag_v::Hot, CipherTierTag_v::Cold>);
static_assert(can_demote_tier_v<CipherTierTag_v::Warm, CipherTierTag_v::Cold>);
static_assert(!can_demote_tier_v<CipherTierTag_v::Cold, CipherTierTag_v::Hot>);

static_assert(
    std::is_same_v<decltype(mint_promote<CipherTierTag_v::Cold, CipherTierTag_v::Warm>(ColdHash{ContentHash{1}})),
                   WarmHash>);
static_assert(std::is_same_v<
              decltype(mint_demote<CipherTierTag_v::Hot, CipherTierTag_v::Cold>(HotHash{ContentHash{2}})), ColdHash>);

using HotPromoteHash = HotPromote<ContentHash>;
using HotPromoteCarrier = HotPromoteDelegate<ContentHash>;
using HotPromotePeer = HotPromoteAccept<ContentHash>;

static_assert(::crucible::safety::proto::is_well_formed_v<HotPromoteHash>);
static_assert(::crucible::safety::proto::DelegatesTo<HotPromoteCarrier, HotPromoteHash>);
static_assert(::crucible::safety::proto::AcceptsFrom<HotPromotePeer, HotPromoteHash>);
static_assert(std::is_same_v<::crucible::safety::proto::dual_of_t<HotPromoteCarrier>, HotPromotePeer>);

}  // namespace detail::cipher_tier_promotion_self_test

}  // namespace crucible::cipher
