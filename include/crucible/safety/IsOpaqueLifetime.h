#pragma once

// ── crucible::safety::extract::is_opaque_lifetime_v ─────────────────
//
// FOUND-D23 — wrapper-detection predicate for `OpaqueLifetime<Scope, T>`.
// Mechanical extension of D21/D22 — partial-spec captures the
// Lifetime_v NTTP enum value alongside the wrapped type.
//
// ── What this header ships ──────────────────────────────────────────
//
//   is_opaque_lifetime_v<T>      Variable template; cv-ref-stripped.
//   IsOpaqueLifetime<T>           Concept form.
//   opaque_lifetime_value_t<T>    Wrapped element type; constrained.
//   opaque_lifetime_scope_v<T>    Pinned Lifetime_v scope; constrained.

#include <crucible/safety/OpaqueLifetime.h>

#include <type_traits>

namespace crucible::safety::extract {

// Re-export Lifetime_v so dispatcher call sites don't need to spell
// `algebra::lattices::Lifetime::PER_FLEET`.
using ::crucible::safety::Lifetime_v;

namespace detail {

template <typename T>
struct is_opaque_lifetime_impl : std::false_type {
    using value_type = void;
    static constexpr bool has_scope = false;
};

template <Lifetime_v Scope, typename U>
struct is_opaque_lifetime_impl<::crucible::safety::OpaqueLifetime<Scope, U>>
    : std::true_type
{
    using value_type = U;
    static constexpr Lifetime_v scope = Scope;
    static constexpr bool has_scope = true;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_opaque_lifetime_v =
    detail::is_opaque_lifetime_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsOpaqueLifetime = is_opaque_lifetime_v<T>;

template <typename T>
    requires is_opaque_lifetime_v<T>
using opaque_lifetime_value_t =
    typename detail::is_opaque_lifetime_impl<
        std::remove_cvref_t<T>>::value_type;

template <typename T>
    requires is_opaque_lifetime_v<T>
inline constexpr Lifetime_v opaque_lifetime_scope_v =
    detail::is_opaque_lifetime_impl<std::remove_cvref_t<T>>::scope;

// ── Self-test ─────────────────────────────────────────────────────

namespace detail::is_opaque_lifetime_self_test {

using OL_int_fleet =
    ::crucible::safety::OpaqueLifetime<Lifetime_v::PER_FLEET, int>;
using OL_double_request =
    ::crucible::safety::OpaqueLifetime<Lifetime_v::PER_REQUEST, double>;
using OL_int_program =
    ::crucible::safety::OpaqueLifetime<Lifetime_v::PER_PROGRAM, int>;

static_assert(is_opaque_lifetime_v<OL_int_fleet>);
static_assert(is_opaque_lifetime_v<OL_double_request>);
static_assert(is_opaque_lifetime_v<OL_int_program>);

static_assert(is_opaque_lifetime_v<OL_int_fleet&>);
static_assert(is_opaque_lifetime_v<OL_int_fleet const&>);

static_assert(!is_opaque_lifetime_v<int>);
static_assert(!is_opaque_lifetime_v<int*>);
static_assert(!is_opaque_lifetime_v<void>);

struct LookalikeLifetime { int value; Lifetime_v scope; };
static_assert(!is_opaque_lifetime_v<LookalikeLifetime>);

static_assert(IsOpaqueLifetime<OL_int_fleet>);
static_assert(!IsOpaqueLifetime<int>);

static_assert(std::is_same_v<opaque_lifetime_value_t<OL_int_fleet>, int>);
static_assert(std::is_same_v<
    opaque_lifetime_value_t<OL_double_request>, double>);

static_assert(opaque_lifetime_scope_v<OL_int_fleet>
              == Lifetime_v::PER_FLEET);
static_assert(opaque_lifetime_scope_v<OL_double_request>
              == Lifetime_v::PER_REQUEST);
static_assert(opaque_lifetime_scope_v<OL_int_program>
              == Lifetime_v::PER_PROGRAM);

}  // namespace detail::is_opaque_lifetime_self_test

inline bool is_opaque_lifetime_smoke_test() noexcept {
    using namespace detail::is_opaque_lifetime_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_opaque_lifetime_v<OL_int_fleet>;
        ok = ok && !is_opaque_lifetime_v<int>;
        ok = ok && IsOpaqueLifetime<OL_int_fleet&&>;
        ok = ok && (opaque_lifetime_scope_v<OL_int_fleet>
                    == Lifetime_v::PER_FLEET);
    }
    return ok;
}

}  // namespace crucible::safety::extract
