#pragma once

// ── crucible::safety::extract::is_hot_path_v ────────────────────────
//
// FOUND-D25 — wrapper-detection predicate for `HotPath<Tier, T>`.
// Mechanical extension of the D21-D24 / D30-batch detector template.

#include <crucible/safety/HotPath.h>

#include <type_traits>

namespace crucible::safety::extract {

using ::crucible::safety::HotPathTier_v;

namespace detail {

template <typename T>
struct is_hot_path_impl : std::false_type {
    using value_type = void;
};

template <HotPathTier_v Tier, typename U>
struct is_hot_path_impl<::crucible::safety::HotPath<Tier, U>>
    : std::true_type
{
    using value_type = U;
    static constexpr HotPathTier_v tier = Tier;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_hot_path_v =
    detail::is_hot_path_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsHotPath = is_hot_path_v<T>;

template <typename T>
    requires is_hot_path_v<T>
using hot_path_value_t =
    typename detail::is_hot_path_impl<
        std::remove_cvref_t<T>>::value_type;

template <typename T>
    requires is_hot_path_v<T>
inline constexpr HotPathTier_v hot_path_tier_v =
    detail::is_hot_path_impl<std::remove_cvref_t<T>>::tier;

namespace detail::is_hot_path_self_test {

using HP_int_hot   = ::crucible::safety::HotPath<HotPathTier_v::Hot,  int>;
using HP_int_warm  = ::crucible::safety::HotPath<HotPathTier_v::Warm, int>;
using HP_int_cold  = ::crucible::safety::HotPath<HotPathTier_v::Cold, int>;
using HP_double_hot = ::crucible::safety::HotPath<HotPathTier_v::Hot, double>;

static_assert(is_hot_path_v<HP_int_hot>);
static_assert(is_hot_path_v<HP_int_warm>);
static_assert(is_hot_path_v<HP_int_cold>);
static_assert(is_hot_path_v<HP_double_hot>);
static_assert(is_hot_path_v<HP_int_hot&>);
static_assert(is_hot_path_v<HP_int_hot const&>);

static_assert(!is_hot_path_v<int>);
static_assert(!is_hot_path_v<int*>);
static_assert(!is_hot_path_v<void>);
static_assert(!is_hot_path_v<HP_int_hot*>);

struct LookalikeHotPath { int value; HotPathTier_v tier; };
static_assert(!is_hot_path_v<LookalikeHotPath>);

static_assert(IsHotPath<HP_int_hot>);
static_assert(!IsHotPath<int>);

static_assert(std::is_same_v<hot_path_value_t<HP_int_hot>, int>);
static_assert(std::is_same_v<hot_path_value_t<HP_double_hot>, double>);

static_assert(hot_path_tier_v<HP_int_hot>  == HotPathTier_v::Hot);
static_assert(hot_path_tier_v<HP_int_warm> == HotPathTier_v::Warm);
static_assert(hot_path_tier_v<HP_int_cold> == HotPathTier_v::Cold);

}  // namespace detail::is_hot_path_self_test

inline bool is_hot_path_smoke_test() noexcept {
    using namespace detail::is_hot_path_self_test;
    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_hot_path_v<HP_int_hot>;
        ok = ok && !is_hot_path_v<int>;
        ok = ok && IsHotPath<HP_int_hot&&>;
        ok = ok && (hot_path_tier_v<HP_int_hot> == HotPathTier_v::Hot);
    }
    return ok;
}

}  // namespace crucible::safety::extract
