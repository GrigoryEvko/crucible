#pragma once

// Synchronization is a wrapper-only axis with no template-parameter slot of
// its own, and the dimension-routing mechanism is reserved for axes that have
// one. These permits are therefore a parallel structural taxonomy rather than
// routed grant tags.

#include <crucible/Platform.h>
#include <crucible/safety/HotPath.h>

#include <cstddef>
#include <tuple>
#include <type_traits>

namespace crucible::fixy::sync {

// The tags sit in a nested namespace to keep them clear of the wait-strategy
// names in the parent namespace.

namespace sync_prim {

struct banned_sync_prim_base {};

// `final` closes the taxonomy. It is not load-bearing for the ban itself.
// A derived tag would still satisfy IsBannedSyncPrim because the base-of
// relation walks the whole chain.

struct permit_mutex final : banned_sync_prim_base {};
struct permit_shared_mutex final : banned_sync_prim_base {};
struct permit_recursive_mutex final : banned_sync_prim_base {};
struct permit_timed_mutex final : banned_sync_prim_base {};
struct permit_condition_variable final : banned_sync_prim_base {};
struct permit_condition_variable_any final : banned_sync_prim_base {};
struct permit_pthread_cond final : banned_sync_prim_base {};
struct permit_futex final : banned_sync_prim_base {};
struct permit_eventfd final : banned_sync_prim_base {};
struct permit_poll final : banned_sync_prim_base {};
struct permit_epoll final : banned_sync_prim_base {};
struct permit_atomic_wait final : banned_sync_prim_base {};
struct permit_sleep_for final : banned_sync_prim_base {};
struct permit_thread_yield final : banned_sync_prim_base {};

template <typename T>
concept IsBannedSyncPrim = std::is_base_of_v<banned_sync_prim_base, std::remove_cvref_t<T>>;

template <typename T>
inline constexpr bool is_banned_sync_prim_v = IsBannedSyncPrim<T>;

template <typename... Ts>
inline constexpr bool pack_contains_banned_sync_prim_v = (IsBannedSyncPrim<Ts> || ...);

// Warm and Cold tiers admit every permit. A background-bounded context
// legitimately uses a mutex, a condition variable or a futex. The discipline
// is to keep those off the hot path, not out of the process.

template <::crucible::safety::HotPathTier_v Tier, typename... Ts>
concept HotPathSyncPrimSafe =
    !(Tier == ::crucible::safety::HotPathTier_v::Hot && pack_contains_banned_sync_prim_v<Ts...>);

template <typename... Ts>
concept HotSyncPrimSafe = HotPathSyncPrimSafe<::crucible::safety::HotPathTier_v::Hot, Ts...>;

template <typename... Ts>
concept WarmSyncPrimSafe = HotPathSyncPrimSafe<::crucible::safety::HotPathTier_v::Warm, Ts...>;

template <typename... Ts>
concept ColdSyncPrimSafe = HotPathSyncPrimSafe<::crucible::safety::HotPathTier_v::Cold, Ts...>;

}  // namespace sync_prim

namespace detail::sync_prim_sentinel {

using namespace ::crucible::fixy::sync::sync_prim;

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

static_assert(!IsBannedSyncPrim<int>);
static_assert(!IsBannedSyncPrim<int*>);
static_assert(!IsBannedSyncPrim<void>);
static_assert(!IsBannedSyncPrim<banned_sync_prim_base*>);
struct unrelated_tag {};
static_assert(!IsBannedSyncPrim<unrelated_tag>);

static_assert(std::is_empty_v<permit_mutex>);
static_assert(std::is_empty_v<permit_futex>);
static_assert(std::is_empty_v<permit_atomic_wait>);
static_assert(sizeof(permit_mutex) == 1);
static_assert(sizeof(permit_atomic_wait) == 1);
static_assert(sizeof(permit_thread_yield) == 1);

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

static_assert(IsBannedSyncPrim<permit_mutex&>);
static_assert(IsBannedSyncPrim<permit_mutex const>);
static_assert(IsBannedSyncPrim<permit_mutex const&>);
static_assert(IsBannedSyncPrim<permit_mutex&&>);
static_assert(IsBannedSyncPrim<permit_futex volatile>);
static_assert(IsBannedSyncPrim<permit_atomic_wait const volatile&>);

static_assert(!pack_contains_banned_sync_prim_v<>);

static_assert(pack_contains_banned_sync_prim_v<permit_mutex>);
static_assert(pack_contains_banned_sync_prim_v<permit_futex>);

static_assert(!pack_contains_banned_sync_prim_v<int, double, void*>);
static_assert(!pack_contains_banned_sync_prim_v<unrelated_tag, banned_sync_prim_base*>);

static_assert(pack_contains_banned_sync_prim_v<int, permit_mutex, double>);
static_assert(pack_contains_banned_sync_prim_v<permit_eventfd, unrelated_tag>);
static_assert(pack_contains_banned_sync_prim_v<int, double, permit_thread_yield>);

static_assert(pack_contains_banned_sync_prim_v<int, permit_mutex const&, double>);

static_assert(!HotPathSyncPrimSafe<::crucible::safety::HotPathTier_v::Hot, permit_mutex>);
static_assert(!HotPathSyncPrimSafe<::crucible::safety::HotPathTier_v::Hot, permit_futex>);
static_assert(!HotPathSyncPrimSafe<::crucible::safety::HotPathTier_v::Hot, permit_atomic_wait>);
static_assert(!HotPathSyncPrimSafe<::crucible::safety::HotPathTier_v::Hot, int, permit_mutex, double>);
static_assert(!HotSyncPrimSafe<permit_mutex>);
static_assert(!HotSyncPrimSafe<permit_condition_variable>);

static_assert(HotPathSyncPrimSafe<::crucible::safety::HotPathTier_v::Hot>);
static_assert(HotSyncPrimSafe<>);

static_assert(HotPathSyncPrimSafe<::crucible::safety::HotPathTier_v::Hot, int, double, unrelated_tag>);
static_assert(HotSyncPrimSafe<int, double>);

static_assert(HotPathSyncPrimSafe<::crucible::safety::HotPathTier_v::Warm, permit_mutex>);
static_assert(WarmSyncPrimSafe<permit_mutex>);
static_assert(WarmSyncPrimSafe<permit_futex, permit_atomic_wait>);

static_assert(HotPathSyncPrimSafe<::crucible::safety::HotPathTier_v::Cold, permit_mutex>);
static_assert(ColdSyncPrimSafe<permit_mutex, permit_poll, permit_epoll>);

inline constexpr std::size_t permit_tag_count = []() consteval -> std::size_t {
    using all_permits = std::tuple<permit_mutex, permit_shared_mutex, permit_recursive_mutex, permit_timed_mutex,
                                   permit_condition_variable, permit_condition_variable_any, permit_pthread_cond,
                                   permit_futex, permit_eventfd, permit_poll, permit_epoll, permit_atomic_wait,
                                   permit_sleep_for, permit_thread_yield>;
    return std::tuple_size_v<all_permits>;
}();

static_assert(permit_tag_count == 14, "sync_prim permit-tag cardinality drifted.  Update the "
                                      "cardinality witness AND the sentinel cells in lockstep.");

static_assert(std::is_base_of_v<banned_sync_prim_base, permit_mutex>);
static_assert(std::is_base_of_v<banned_sync_prim_base, permit_thread_yield>);

// The base is concept-positive by reflexivity. That is deliberate. It is a
// member of the permit set that names no particular primitive.
static_assert(IsBannedSyncPrim<banned_sync_prim_base>);

}  // namespace detail::sync_prim_sentinel

}  // namespace crucible::fixy::sync
