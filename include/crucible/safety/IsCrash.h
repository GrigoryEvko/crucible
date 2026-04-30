#pragma once

// ── crucible::safety::extract::is_crash_v ───────────────────────────
//
// FOUND-D30 (fourth of batch) — wrapper-detection predicate for
// `Crash<Class, T>`.  Mechanical extension of D21-D24/D30
// CipherTier/ResidencyHeat/Vendor — partial-spec captures the
// CrashClass_v NTTP enum alongside the wrapped type.
//
// CrashClass is a 4-valued chain lattice: Abort=0 (⊥) ... NoThrow=3
// (⊤).  In a -fno-exceptions tree, Throw is structurally banned at
// production call sites; the detector itself is symmetric across all
// four classes so future fleet variants (e.g., a partial -fexceptions
// island for legacy interop) can flip a class on without changing the
// detection contract.

#include <crucible/safety/Crash.h>

#include <type_traits>

namespace crucible::safety::extract {

using ::crucible::safety::CrashClass_v;

namespace detail {

template <typename T>
struct is_crash_impl : std::false_type {
    using value_type = void;
    static constexpr bool has_class = false;
};

template <CrashClass_v Class, typename U>
struct is_crash_impl<::crucible::safety::Crash<Class, U>>
    : std::true_type
{
    using value_type = U;
    static constexpr CrashClass_v crash_class = Class;
    static constexpr bool has_class = true;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_crash_v =
    detail::is_crash_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsCrash = is_crash_v<T>;

template <typename T>
    requires is_crash_v<T>
using crash_value_t =
    typename detail::is_crash_impl<
        std::remove_cvref_t<T>>::value_type;

template <typename T>
    requires is_crash_v<T>
inline constexpr CrashClass_v crash_class_v =
    detail::is_crash_impl<std::remove_cvref_t<T>>::crash_class;

// ── Self-test ─────────────────────────────────────────────────────

namespace detail::is_crash_self_test {

using C_int_abort        = ::crucible::safety::Crash<CrashClass_v::Abort,       int>;
using C_int_throw        = ::crucible::safety::Crash<CrashClass_v::Throw,       int>;
using C_int_error_return = ::crucible::safety::Crash<CrashClass_v::ErrorReturn, int>;
using C_int_no_throw     = ::crucible::safety::Crash<CrashClass_v::NoThrow,     int>;
using C_double_no_throw  = ::crucible::safety::Crash<CrashClass_v::NoThrow,     double>;

static_assert(is_crash_v<C_int_abort>);
static_assert(is_crash_v<C_int_throw>);
static_assert(is_crash_v<C_int_error_return>);
static_assert(is_crash_v<C_int_no_throw>);
static_assert(is_crash_v<C_double_no_throw>);

static_assert(is_crash_v<C_int_no_throw&>);
static_assert(is_crash_v<C_int_no_throw const&>);

static_assert(!is_crash_v<int>);
static_assert(!is_crash_v<int*>);
static_assert(!is_crash_v<void>);

struct LookalikeCrash { int value; CrashClass_v crash_class; };
static_assert(!is_crash_v<LookalikeCrash>);

static_assert(!is_crash_v<C_int_no_throw*>);

static_assert(IsCrash<C_int_no_throw>);
static_assert(!IsCrash<int>);

static_assert(std::is_same_v<crash_value_t<C_int_no_throw>, int>);
static_assert(std::is_same_v<crash_value_t<C_double_no_throw>, double>);

static_assert(crash_class_v<C_int_abort>        == CrashClass_v::Abort);
static_assert(crash_class_v<C_int_throw>        == CrashClass_v::Throw);
static_assert(crash_class_v<C_int_error_return> == CrashClass_v::ErrorReturn);
static_assert(crash_class_v<C_int_no_throw>     == CrashClass_v::NoThrow);

// Chain-lattice ordinal invariant.  CrashLattice INVERTS the spec
// hint (Abort=0 / NoThrow=3 — bottom is weakest).  The detector
// itself does NOT consult this ordering, but a future refactor that
// indexes by underlying value MUST preserve the invariant.
static_assert(static_cast<std::uint8_t>(CrashClass_v::Abort)       == 0);
static_assert(static_cast<std::uint8_t>(CrashClass_v::Throw)       == 1);
static_assert(static_cast<std::uint8_t>(CrashClass_v::ErrorReturn) == 2);
static_assert(static_cast<std::uint8_t>(CrashClass_v::NoThrow)     == 3);

}  // namespace detail::is_crash_self_test

inline bool is_crash_smoke_test() noexcept {
    using namespace detail::is_crash_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_crash_v<C_int_no_throw>;
        ok = ok && is_crash_v<C_int_abort>;
        ok = ok && !is_crash_v<int>;
        ok = ok && IsCrash<C_int_no_throw&&>;
        ok = ok && (crash_class_v<C_int_no_throw>
                    == CrashClass_v::NoThrow);
        ok = ok && (crash_class_v<C_int_abort>
                    == CrashClass_v::Abort);
    }
    return ok;
}

}  // namespace crucible::safety::extract
