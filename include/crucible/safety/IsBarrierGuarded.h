#pragma once

// ── crucible::safety::extract::is_barrier_guarded_v ─────────────────
//
// FIXY-V-255 — wrapper-detection predicate for `BarrierGuarded<Tier, T>`
// (the V-255 BarrierStrength-axis carrier).  Mechanical sibling of
// IsHw / IsVendor — the partial spec captures the BarrierStrength NTTP
// tier alongside the wrapped type, so downstream dispatchers can read
// the pinned publication-fence tier off the type without instantiating
// the wrapper.
//
// The detector keys on the wrapper class identity, not the lattice
// value: a `BarrierGuarded<AcqRel, T>` and a look-alike struct carrying
// a BarrierStrength field are NOT confused.

#include <crucible/safety/BarrierGuarded.h>

#include <type_traits>

namespace crucible::safety::extract {

using ::crucible::safety::BarrierStrength_v;

namespace detail {

template <typename T>
struct is_barrier_guarded_impl : std::false_type {
    using value_type = void;
    static constexpr bool has_tier = false;
};

template <BarrierStrength_v Tier, typename U>
struct is_barrier_guarded_impl<::crucible::safety::BarrierGuarded<Tier, U>>
    : std::true_type {
    using value_type = U;
    static constexpr BarrierStrength_v tier = Tier;
    static constexpr bool has_tier = true;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_barrier_guarded_v =
    detail::is_barrier_guarded_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsBarrierGuarded = is_barrier_guarded_v<T>;

template <typename T>
    requires is_barrier_guarded_v<T>
using barrier_guarded_value_t =
    typename detail::is_barrier_guarded_impl<std::remove_cvref_t<T>>::value_type;

template <typename T>
    requires is_barrier_guarded_v<T>
inline constexpr BarrierStrength_v barrier_guarded_tier_v =
    detail::is_barrier_guarded_impl<std::remove_cvref_t<T>>::tier;

// ── Self-test ─────────────────────────────────────────────────────

namespace detail::is_barrier_guarded_self_test {

using B_int_none   = ::crucible::safety::BarrierGuarded<BarrierStrength_v::None,        int>;
using B_int_acq    = ::crucible::safety::BarrierGuarded<BarrierStrength_v::AcquireLoad, int>;
using B_int_acqrel = ::crucible::safety::BarrierGuarded<BarrierStrength_v::AcqRel,      int>;
using B_int_seqcst = ::crucible::safety::BarrierGuarded<BarrierStrength_v::SeqCst,      int>;
using B_int_full   = ::crucible::safety::BarrierGuarded<BarrierStrength_v::FullFence,   int>;
using B_double_acq = ::crucible::safety::BarrierGuarded<BarrierStrength_v::AcqRel,      double>;

static_assert(is_barrier_guarded_v<B_int_none>);
static_assert(is_barrier_guarded_v<B_int_acq>);
static_assert(is_barrier_guarded_v<B_int_acqrel>);
static_assert(is_barrier_guarded_v<B_int_seqcst>);
static_assert(is_barrier_guarded_v<B_int_full>);
static_assert(is_barrier_guarded_v<B_double_acq>);

static_assert(is_barrier_guarded_v<B_int_acqrel&>);
static_assert(is_barrier_guarded_v<B_int_acqrel const&>);

static_assert(!is_barrier_guarded_v<int>);
static_assert(!is_barrier_guarded_v<int*>);
static_assert(!is_barrier_guarded_v<void>);
static_assert(!is_barrier_guarded_v<B_int_acqrel*>);

struct LookalikeBarrier { int value; BarrierStrength_v tier; };
static_assert(!is_barrier_guarded_v<LookalikeBarrier>);

static_assert(IsBarrierGuarded<B_int_acqrel>);
static_assert(!IsBarrierGuarded<int>);

static_assert(std::is_same_v<barrier_guarded_value_t<B_int_acqrel>, int>);
static_assert(std::is_same_v<barrier_guarded_value_t<B_double_acq>, double>);

static_assert(barrier_guarded_tier_v<B_int_none>   == BarrierStrength_v::None);
static_assert(barrier_guarded_tier_v<B_int_acq>    == BarrierStrength_v::AcquireLoad);
static_assert(barrier_guarded_tier_v<B_int_acqrel> == BarrierStrength_v::AcqRel);
static_assert(barrier_guarded_tier_v<B_int_seqcst> == BarrierStrength_v::SeqCst);
static_assert(barrier_guarded_tier_v<B_int_full>   == BarrierStrength_v::FullFence);

static_assert(barrier_guarded_tier_v<B_int_acq> != barrier_guarded_tier_v<B_int_seqcst>);

}  // namespace detail::is_barrier_guarded_self_test

inline bool is_barrier_guarded_smoke_test() noexcept {
    using namespace detail::is_barrier_guarded_self_test;
    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_barrier_guarded_v<B_int_acqrel>;
        ok = ok && is_barrier_guarded_v<B_int_seqcst>;
        ok = ok && !is_barrier_guarded_v<int>;
        ok = ok && IsBarrierGuarded<B_int_acqrel&&>;
        ok = ok && (barrier_guarded_tier_v<B_int_acqrel> == BarrierStrength_v::AcqRel);
        ok = ok && (barrier_guarded_tier_v<B_int_full>   == BarrierStrength_v::FullFence);
    }
    return ok;
}

}  // namespace crucible::safety::extract
