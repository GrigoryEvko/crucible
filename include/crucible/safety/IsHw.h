#pragma once

// ── crucible::safety::extract::is_hw_v ──────────────────────────────
//
// FIXY-V-254 — wrapper-detection predicate for `Hw<Tier, T>` (the
// V-254 HwInstruction-axis carrier).  Mechanical sibling of IsVendor /
// IsResidencyHeat — the partial spec captures the HwInstruction NTTP
// tier alongside the wrapped type, so downstream dispatchers can read
// the pinned instruction-capability tier off the type without
// instantiating the wrapper.
//
// The detector keys on the wrapper class identity, not the lattice
// value: an `Hw<Vectorizable, T>` and a look-alike struct carrying an
// HwInstruction field are NOT confused.

#include <crucible/safety/Hw.h>

#include <type_traits>

namespace crucible::safety::extract {

using ::crucible::safety::HwInstruction_v;

namespace detail {

template <typename T>
struct is_hw_impl : std::false_type {
    using value_type = void;
    static constexpr bool has_tier = false;
};

template <HwInstruction_v Tier, typename U>
struct is_hw_impl<::crucible::safety::Hw<Tier, U>> : std::true_type {
    using value_type = U;
    static constexpr HwInstruction_v tier = Tier;
    static constexpr bool has_tier = true;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_hw_v =
    detail::is_hw_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsHw = is_hw_v<T>;

template <typename T>
    requires is_hw_v<T>
using hw_value_t =
    typename detail::is_hw_impl<std::remove_cvref_t<T>>::value_type;

template <typename T>
    requires is_hw_v<T>
inline constexpr HwInstruction_v hw_tier_v =
    detail::is_hw_impl<std::remove_cvref_t<T>>::tier;

// ── Self-test ─────────────────────────────────────────────────────

namespace detail::is_hw_self_test {

using H_int_none   = ::crucible::safety::Hw<HwInstruction_v::NoneAllowed,         int>;
using H_int_scalar = ::crucible::safety::Hw<HwInstruction_v::Scalar,             int>;
using H_int_vec    = ::crucible::safety::Hw<HwInstruction_v::Vectorizable,       int>;
using H_int_tsc    = ::crucible::safety::Hw<HwInstruction_v::NonDeterministicTsc, int>;
using H_int_msr    = ::crucible::safety::Hw<HwInstruction_v::PrivilegedMsr,      int>;
using H_double_vec = ::crucible::safety::Hw<HwInstruction_v::Vectorizable,       double>;

static_assert(is_hw_v<H_int_none>);
static_assert(is_hw_v<H_int_scalar>);
static_assert(is_hw_v<H_int_vec>);
static_assert(is_hw_v<H_int_tsc>);
static_assert(is_hw_v<H_int_msr>);
static_assert(is_hw_v<H_double_vec>);

static_assert(is_hw_v<H_int_vec&>);
static_assert(is_hw_v<H_int_vec const&>);

static_assert(!is_hw_v<int>);
static_assert(!is_hw_v<int*>);
static_assert(!is_hw_v<void>);
static_assert(!is_hw_v<H_int_vec*>);

struct LookalikeHw { int value; HwInstruction_v tier; };
static_assert(!is_hw_v<LookalikeHw>);

static_assert(IsHw<H_int_vec>);
static_assert(!IsHw<int>);

static_assert(std::is_same_v<hw_value_t<H_int_vec>, int>);
static_assert(std::is_same_v<hw_value_t<H_double_vec>, double>);

static_assert(hw_tier_v<H_int_none>   == HwInstruction_v::NoneAllowed);
static_assert(hw_tier_v<H_int_scalar> == HwInstruction_v::Scalar);
static_assert(hw_tier_v<H_int_vec>    == HwInstruction_v::Vectorizable);
static_assert(hw_tier_v<H_int_tsc>    == HwInstruction_v::NonDeterministicTsc);
static_assert(hw_tier_v<H_int_msr>    == HwInstruction_v::PrivilegedMsr);

static_assert(hw_tier_v<H_int_scalar> != hw_tier_v<H_int_vec>);

}  // namespace detail::is_hw_self_test

inline bool is_hw_smoke_test() noexcept {
    using namespace detail::is_hw_self_test;
    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_hw_v<H_int_vec>;
        ok = ok && is_hw_v<H_int_scalar>;
        ok = ok && !is_hw_v<int>;
        ok = ok && IsHw<H_int_vec&&>;
        ok = ok && (hw_tier_v<H_int_vec> == HwInstruction_v::Vectorizable);
        ok = ok && (hw_tier_v<H_int_msr> == HwInstruction_v::PrivilegedMsr);
    }
    return ok;
}

}  // namespace crucible::safety::extract
