#pragma once

// ── crucible::safety::extract::is_simd_width_pinned_v ───────────────
//
// FIXY-V-256 — wrapper-detection predicate for `SimdWidthPinned<W, T>`
// (the V-256 SimdIsa-axis carrier).  Mechanical sibling of IsHw /
// IsVendor — the partial spec captures the SimdIsa NTTP alongside the
// wrapped type, so downstream dispatchers can read the pinned ISA
// capability off the type without instantiating the wrapper.
//
// The detector keys on the wrapper class identity, not the lattice
// value: a `SimdWidthPinned<Avx2, T>` and a look-alike struct carrying a
// SimdIsa field are NOT confused.

#include <crucible/safety/SimdWidthPinned.h>

#include <type_traits>

namespace crucible::safety::extract {

using ::crucible::safety::SimdIsa_v;

namespace detail {

template <typename T>
struct is_simd_width_pinned_impl : std::false_type {
    using value_type = void;
    static constexpr bool has_isa = false;
};

template <SimdIsa_v W, typename U>
struct is_simd_width_pinned_impl<::crucible::safety::SimdWidthPinned<W, U>>
    : std::true_type {
    using value_type = U;
    static constexpr SimdIsa_v isa = W;
    static constexpr bool has_isa = true;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_simd_width_pinned_v =
    detail::is_simd_width_pinned_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsSimdWidthPinned = is_simd_width_pinned_v<T>;

template <typename T>
    requires is_simd_width_pinned_v<T>
using simd_width_pinned_value_t =
    typename detail::is_simd_width_pinned_impl<std::remove_cvref_t<T>>::value_type;

template <typename T>
    requires is_simd_width_pinned_v<T>
inline constexpr SimdIsa_v simd_width_pinned_isa_v =
    detail::is_simd_width_pinned_impl<std::remove_cvref_t<T>>::isa;

// ── Self-test ─────────────────────────────────────────────────────

namespace detail::is_simd_width_pinned_self_test {

using S_int_scalar = ::crucible::safety::SimdWidthPinned<SimdIsa_v::Scalar,   int>;
using S_int_avx2   = ::crucible::safety::SimdWidthPinned<SimdIsa_v::Avx2,     int>;
using S_int_avx512 = ::crucible::safety::SimdWidthPinned<SimdIsa_v::Avx512Bw, int>;
using S_int_neon   = ::crucible::safety::SimdWidthPinned<SimdIsa_v::Neon,     int>;
using S_int_port   = ::crucible::safety::SimdWidthPinned<SimdIsa_v::Portable, int>;
using S_double_avx = ::crucible::safety::SimdWidthPinned<SimdIsa_v::Avx2,     double>;

static_assert(is_simd_width_pinned_v<S_int_scalar>);
static_assert(is_simd_width_pinned_v<S_int_avx2>);
static_assert(is_simd_width_pinned_v<S_int_avx512>);
static_assert(is_simd_width_pinned_v<S_int_neon>);
static_assert(is_simd_width_pinned_v<S_int_port>);
static_assert(is_simd_width_pinned_v<S_double_avx>);

static_assert(is_simd_width_pinned_v<S_int_avx2&>);
static_assert(is_simd_width_pinned_v<S_int_avx2 const&>);

static_assert(!is_simd_width_pinned_v<int>);
static_assert(!is_simd_width_pinned_v<int*>);
static_assert(!is_simd_width_pinned_v<void>);
static_assert(!is_simd_width_pinned_v<S_int_avx2*>);

struct LookalikeSimd { int value; SimdIsa_v isa; };
static_assert(!is_simd_width_pinned_v<LookalikeSimd>);

static_assert(IsSimdWidthPinned<S_int_avx2>);
static_assert(!IsSimdWidthPinned<int>);

static_assert(std::is_same_v<simd_width_pinned_value_t<S_int_avx2>, int>);
static_assert(std::is_same_v<simd_width_pinned_value_t<S_double_avx>, double>);

static_assert(simd_width_pinned_isa_v<S_int_scalar> == SimdIsa_v::Scalar);
static_assert(simd_width_pinned_isa_v<S_int_avx2>   == SimdIsa_v::Avx2);
static_assert(simd_width_pinned_isa_v<S_int_avx512> == SimdIsa_v::Avx512Bw);
static_assert(simd_width_pinned_isa_v<S_int_neon>   == SimdIsa_v::Neon);
static_assert(simd_width_pinned_isa_v<S_int_port>   == SimdIsa_v::Portable);

static_assert(simd_width_pinned_isa_v<S_int_avx2> != simd_width_pinned_isa_v<S_int_neon>);

}  // namespace detail::is_simd_width_pinned_self_test

inline bool is_simd_width_pinned_smoke_test() noexcept {
    using namespace detail::is_simd_width_pinned_self_test;
    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_simd_width_pinned_v<S_int_avx2>;
        ok = ok && is_simd_width_pinned_v<S_int_neon>;
        ok = ok && !is_simd_width_pinned_v<int>;
        ok = ok && IsSimdWidthPinned<S_int_avx2&&>;
        ok = ok && (simd_width_pinned_isa_v<S_int_avx2> == SimdIsa_v::Avx2);
        ok = ok && (simd_width_pinned_isa_v<S_int_port> == SimdIsa_v::Portable);
    }
    return ok;
}

}  // namespace crucible::safety::extract
