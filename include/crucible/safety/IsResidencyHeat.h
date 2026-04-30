#pragma once

// ── crucible::safety::extract::is_residency_heat_v ──────────────────
//
// FOUND-D30 (second of batch) — wrapper-detection predicate for
// `ResidencyHeat<Tier, T>`.  Mechanical extension of D21-D24/D30
// CipherTier — partial-spec captures the ResidencyHeatTag_v NTTP
// enum alongside the wrapped type.

#include <crucible/safety/ResidencyHeat.h>

#include <type_traits>

namespace crucible::safety::extract {

using ::crucible::safety::ResidencyHeatTag_v;

namespace detail {

template <typename T>
struct is_residency_heat_impl : std::false_type {
    using value_type = void;
    static constexpr bool has_tag = false;
};

template <ResidencyHeatTag_v Tier, typename U>
struct is_residency_heat_impl<::crucible::safety::ResidencyHeat<Tier, U>>
    : std::true_type
{
    using value_type = U;
    static constexpr ResidencyHeatTag_v tier = Tier;
    static constexpr bool has_tag = true;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_residency_heat_v =
    detail::is_residency_heat_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsResidencyHeat = is_residency_heat_v<T>;

template <typename T>
    requires is_residency_heat_v<T>
using residency_heat_value_t =
    typename detail::is_residency_heat_impl<
        std::remove_cvref_t<T>>::value_type;

template <typename T>
    requires is_residency_heat_v<T>
inline constexpr ResidencyHeatTag_v residency_heat_tag_v =
    detail::is_residency_heat_impl<std::remove_cvref_t<T>>::tier;

// ── Self-test ─────────────────────────────────────────────────────

namespace detail::is_residency_heat_self_test {

using RH_int_hot =
    ::crucible::safety::ResidencyHeat<ResidencyHeatTag_v::Hot, int>;
using RH_double_warm =
    ::crucible::safety::ResidencyHeat<ResidencyHeatTag_v::Warm, double>;
using RH_int_cold =
    ::crucible::safety::ResidencyHeat<ResidencyHeatTag_v::Cold, int>;

static_assert(is_residency_heat_v<RH_int_hot>);
static_assert(is_residency_heat_v<RH_double_warm>);
static_assert(is_residency_heat_v<RH_int_cold>);
static_assert(is_residency_heat_v<RH_int_hot&>);
static_assert(is_residency_heat_v<RH_int_hot const&>);

static_assert(!is_residency_heat_v<int>);
static_assert(!is_residency_heat_v<int*>);
static_assert(!is_residency_heat_v<void>);
static_assert(!is_residency_heat_v<RH_int_hot*>);

struct LookalikeResidencyHeat { int value; ResidencyHeatTag_v tier; };
static_assert(!is_residency_heat_v<LookalikeResidencyHeat>);

static_assert(IsResidencyHeat<RH_int_hot>);
static_assert(!IsResidencyHeat<int>);

static_assert(std::is_same_v<residency_heat_value_t<RH_int_hot>, int>);
static_assert(std::is_same_v<residency_heat_value_t<RH_double_warm>, double>);

static_assert(residency_heat_tag_v<RH_int_hot> == ResidencyHeatTag_v::Hot);
static_assert(residency_heat_tag_v<RH_double_warm>
              == ResidencyHeatTag_v::Warm);
static_assert(residency_heat_tag_v<RH_int_cold> == ResidencyHeatTag_v::Cold);

}  // namespace detail::is_residency_heat_self_test

inline bool is_residency_heat_smoke_test() noexcept {
    using namespace detail::is_residency_heat_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_residency_heat_v<RH_int_hot>;
        ok = ok && !is_residency_heat_v<int>;
        ok = ok && IsResidencyHeat<RH_int_hot&&>;
        ok = ok && (residency_heat_tag_v<RH_int_hot>
                    == ResidencyHeatTag_v::Hot);
    }
    return ok;
}

}  // namespace crucible::safety::extract
