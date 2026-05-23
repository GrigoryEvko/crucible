#pragma once

// ── crucible::safety::extract::is_scoped_fence_v ────────────────────
//
// FIXY-V-267 — wrapper-detection predicate for `ScopedFence<S, T>` (the
// V-266 MemoryScope-axis carrier).  Mechanical sibling of
// IsSimdWidthPinned — the partial spec captures the MemoryScope NTTP
// alongside the wrapped type, so downstream dispatchers can read the
// pinned publish scope off the type without instantiating the wrapper.
//
// The detector keys on the wrapper class identity, not the lattice
// value: a `ScopedFence<Cta, T>` and a look-alike struct carrying a
// MemoryScope field are NOT confused.

#include <crucible/safety/ScopedFence.h>

#include <type_traits>

namespace crucible::safety::extract {

using ::crucible::safety::MemoryScope_v;

namespace detail {

template <typename T>
struct is_scoped_fence_impl : std::false_type {
    using value_type = void;
    static constexpr bool has_scope = false;
};

template <MemoryScope_v S, typename U>
struct is_scoped_fence_impl<::crucible::safety::ScopedFence<S, U>>
    : std::true_type {
    using value_type = U;
    static constexpr MemoryScope_v scope = S;
    static constexpr bool has_scope = true;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_scoped_fence_v =
    detail::is_scoped_fence_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsScopedFence = is_scoped_fence_v<T>;

template <typename T>
    requires is_scoped_fence_v<T>
using scoped_fence_value_t =
    typename detail::is_scoped_fence_impl<std::remove_cvref_t<T>>::value_type;

template <typename T>
    requires is_scoped_fence_v<T>
inline constexpr MemoryScope_v scoped_fence_scope_v =
    detail::is_scoped_fence_impl<std::remove_cvref_t<T>>::scope;

// ── Self-test ─────────────────────────────────────────────────────

namespace detail::is_scoped_fence_self_test {

using S_int_thread  = ::crucible::safety::ScopedFence<MemoryScope_v::Thread,  int>;
using S_int_cta     = ::crucible::safety::ScopedFence<MemoryScope_v::Cta,     int>;
using S_int_gpu     = ::crucible::safety::ScopedFence<MemoryScope_v::Gpu,     int>;
using S_int_inner   = ::crucible::safety::ScopedFence<MemoryScope_v::Inner,   int>;
using S_int_system  = ::crucible::safety::ScopedFence<MemoryScope_v::System,  int>;
using S_double_cta  = ::crucible::safety::ScopedFence<MemoryScope_v::Cta,     double>;

static_assert(is_scoped_fence_v<S_int_thread>);
static_assert(is_scoped_fence_v<S_int_cta>);
static_assert(is_scoped_fence_v<S_int_gpu>);
static_assert(is_scoped_fence_v<S_int_inner>);
static_assert(is_scoped_fence_v<S_int_system>);
static_assert(is_scoped_fence_v<S_double_cta>);

static_assert(is_scoped_fence_v<S_int_cta&>);
static_assert(is_scoped_fence_v<S_int_cta const&>);

static_assert(!is_scoped_fence_v<int>);
static_assert(!is_scoped_fence_v<int*>);
static_assert(!is_scoped_fence_v<void>);
static_assert(!is_scoped_fence_v<S_int_cta*>);

struct LookalikeFence { int value; MemoryScope_v scope; };
static_assert(!is_scoped_fence_v<LookalikeFence>);

static_assert(IsScopedFence<S_int_cta>);
static_assert(!IsScopedFence<int>);

static_assert(std::is_same_v<scoped_fence_value_t<S_int_cta>, int>);
static_assert(std::is_same_v<scoped_fence_value_t<S_double_cta>, double>);

static_assert(scoped_fence_scope_v<S_int_thread> == MemoryScope_v::Thread);
static_assert(scoped_fence_scope_v<S_int_cta>    == MemoryScope_v::Cta);
static_assert(scoped_fence_scope_v<S_int_gpu>    == MemoryScope_v::Gpu);
static_assert(scoped_fence_scope_v<S_int_inner>  == MemoryScope_v::Inner);
static_assert(scoped_fence_scope_v<S_int_system> == MemoryScope_v::System);

static_assert(scoped_fence_scope_v<S_int_cta> != scoped_fence_scope_v<S_int_inner>);

}  // namespace detail::is_scoped_fence_self_test

inline bool is_scoped_fence_smoke_test() noexcept {
    using namespace detail::is_scoped_fence_self_test;
    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_scoped_fence_v<S_int_cta>;
        ok = ok && is_scoped_fence_v<S_int_inner>;
        ok = ok && !is_scoped_fence_v<int>;
        ok = ok && IsScopedFence<S_int_cta&&>;
        ok = ok && (scoped_fence_scope_v<S_int_cta>    == MemoryScope_v::Cta);
        ok = ok && (scoped_fence_scope_v<S_int_system> == MemoryScope_v::System);
    }
    return ok;
}

}  // namespace crucible::safety::extract
