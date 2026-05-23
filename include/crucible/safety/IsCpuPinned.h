#pragma once

// ── crucible::safety::extract::is_cpu_pinned_v ──────────────────────
//
// FIXY-V-187 — wrapper-detection predicate for `CpuPinned<Mask, Posture,
// T>` (the V-182 affinity × pinning-posture proof token).  Mechanical
// sibling of IsClockSource / IsSchedClass — the partial spec captures the
// AffinityMask + PinningPosture NTTPs alongside the wrapped type, so a TSC
// reader (V-190) can read the pinned mask + posture off the type without
// instantiating the wrapper.
//
// The detector keys on the wrapper class identity, not the lattice value:
// a `CpuPinned<...>` and a look-alike struct carrying a mask field are NOT
// confused.  The `IsCpuPinned` concept is the "proof required" gate the
// rdtsc-reader fixture leans on.

#include <crucible/safety/CpuPinned.h>

#include <type_traits>

namespace crucible::safety::extract {

using ::crucible::algebra::lattices::AffinityMask;
using ::crucible::safety::PinningPosture;

namespace detail {

template <typename T>
struct is_cpu_pinned_impl : std::false_type {
    using value_type = void;
    static constexpr bool has_pin = false;
};

template <AffinityMask Mask, PinningPosture Posture, typename U>
struct is_cpu_pinned_impl<::crucible::safety::CpuPinned<Mask, Posture, U>>
    : std::true_type {
    using value_type = U;
    static constexpr AffinityMask   mask    = Mask;
    static constexpr PinningPosture posture = Posture;
    static constexpr bool           has_pin = true;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_cpu_pinned_v =
    detail::is_cpu_pinned_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsCpuPinned = is_cpu_pinned_v<T>;

template <typename T>
    requires is_cpu_pinned_v<T>
using cpu_pinned_value_t =
    typename detail::is_cpu_pinned_impl<std::remove_cvref_t<T>>::value_type;

template <typename T>
    requires is_cpu_pinned_v<T>
inline constexpr PinningPosture cpu_pinned_posture_v =
    detail::is_cpu_pinned_impl<std::remove_cvref_t<T>>::posture;

// ── Self-test ─────────────────────────────────────────────────────

namespace detail::is_cpu_pinned_self_test {

inline constexpr AffinityMask kC0 = AffinityMask::single(0);
inline constexpr AffinityMask kC3 = AffinityMask::single(3);

using P_int = ::crucible::safety::CpuPinned<kC0, PinningPosture::PinnedExplicit, int>;
using A_int = ::crucible::safety::CpuPinned<kC0, PinningPosture::PinnedAuto,     int>;
using P_dbl = ::crucible::safety::CpuPinned<kC3, PinningPosture::PinnedExplicit, double>;

static_assert(is_cpu_pinned_v<P_int>);
static_assert(is_cpu_pinned_v<A_int>);
static_assert(is_cpu_pinned_v<P_int&>);
static_assert(is_cpu_pinned_v<P_int const&>);

static_assert(!is_cpu_pinned_v<int>);
static_assert(!is_cpu_pinned_v<void>);
static_assert(!is_cpu_pinned_v<P_int*>);

struct LookalikePin { int value; PinningPosture posture; };
static_assert(!is_cpu_pinned_v<LookalikePin>);

static_assert(IsCpuPinned<P_int>);
static_assert(!IsCpuPinned<int>);

static_assert(std::is_same_v<cpu_pinned_value_t<P_int>, int>);
static_assert(std::is_same_v<cpu_pinned_value_t<P_dbl>, double>);

static_assert(cpu_pinned_posture_v<P_int> == PinningPosture::PinnedExplicit);
static_assert(cpu_pinned_posture_v<A_int> == PinningPosture::PinnedAuto);
static_assert(cpu_pinned_posture_v<P_int> != cpu_pinned_posture_v<A_int>);

// The pinned mask is recoverable from the type.
static_assert(detail::is_cpu_pinned_impl<P_int>::mask == kC0);
static_assert(detail::is_cpu_pinned_impl<P_dbl>::mask == kC3);

}  // namespace detail::is_cpu_pinned_self_test

inline bool is_cpu_pinned_smoke_test() noexcept {
    using namespace detail::is_cpu_pinned_self_test;
    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_cpu_pinned_v<P_int>;
        ok = ok && is_cpu_pinned_v<A_int>;
        ok = ok && !is_cpu_pinned_v<int>;
        ok = ok && IsCpuPinned<P_int&&>;
        ok = ok && (cpu_pinned_posture_v<P_int> == PinningPosture::PinnedExplicit);
        ok = ok && (cpu_pinned_posture_v<A_int> == PinningPosture::PinnedAuto);
    }
    return ok;
}

}  // namespace crucible::safety::extract
