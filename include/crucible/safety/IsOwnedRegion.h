#pragma once

// ── crucible::safety::extract::is_owned_region_v ────────────────────
//
// Wrapper-detection predicate for `OwnedRegion<T, Tag>`.  Part of the
// FOUND-D series (FOUND-D03 of 28_04_2026_effects.md §10 +
// 27_04_2026.md §5.5).  The dispatcher reads this predicate to
// recognize OwnedRegion-typed function parameters and route them to
// the OwnedRegion-shaped lowering specializations (UnaryTransform,
// Reduction, ConsumerEndpoint per §5.6).
//
// ── What this header ships ──────────────────────────────────────────
//
//   is_owned_region_v<T>    Variable template: true iff T (after
//                            cv-ref stripping) is a specialization of
//                            OwnedRegion<U, Tag> for some U and Tag.
//   IsOwnedRegion<T>         Concept form for `requires`-clauses.
//   owned_region_value_t<T>  Alias to U (the element type) when T is
//                            an OwnedRegion; ill-formed otherwise.
//   owned_region_tag_t<T>    Alias to Tag when T is an OwnedRegion;
//                            ill-formed otherwise.
//
// ── Pattern ─────────────────────────────────────────────────────────
//
// Canonical primary-false-template + partial-specialization-true
// pattern, mirroring is_permission_v in permissions/Permission.h.
// `std::remove_cvref_t<T>` strips reference categories (`T`, `T&`,
// `T const&`, `T&&`) before the trait check so call sites can write
// the predicate against the parameter type directly without manual
// decay.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   InitSafe / NullSafe / MemSafe / BorrowSafe / ThreadSafe / LeakSafe
//     — N/A; pure consteval predicate.
//   TypeSafe — partial specialization is the only true case;
//              everything else is false.  No silent conversions.
//   DetSafe — same T → same value, deterministically; no hidden
//              state.

#include <crucible/safety/OwnedRegion.h>

#include <type_traits>

namespace crucible::safety::extract {

// ═════════════════════════════════════════════════════════════════════
// ── detail: primary + partial specialization ──────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail {

template <typename T>
struct is_owned_region_impl : std::false_type {
    using value_type = void;
    using tag_type   = void;
};

template <typename T, typename Tag>
struct is_owned_region_impl<::crucible::safety::OwnedRegion<T, Tag>>
    : std::true_type
{
    using value_type = T;
    using tag_type   = Tag;
};

}  // namespace detail

// ═════════════════════════════════════════════════════════════════════
// ── Public surface ─────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename T>
inline constexpr bool is_owned_region_v =
    detail::is_owned_region_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsOwnedRegion = is_owned_region_v<T>;

// owned_region_value_t / owned_region_tag_t are constrained on
// is_owned_region_v to produce a clean compile error rather than
// `void` for non-OwnedRegion arguments.

template <typename T>
    requires is_owned_region_v<T>
using owned_region_value_t =
    typename detail::is_owned_region_impl<std::remove_cvref_t<T>>::value_type;

template <typename T>
    requires is_owned_region_v<T>
using owned_region_tag_t =
    typename detail::is_owned_region_impl<std::remove_cvref_t<T>>::tag_type;

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Adversarial cases verified at compile time.  Every claim is
// duplicated by the sentinel TU under the project's full warning
// matrix.

namespace detail::is_owned_region_self_test {

struct test_tag_a {};
struct test_tag_b {};

using OR_int_a    = ::crucible::safety::OwnedRegion<int, test_tag_a>;
using OR_double_a = ::crucible::safety::OwnedRegion<double, test_tag_a>;
using OR_int_b    = ::crucible::safety::OwnedRegion<int, test_tag_b>;

// ── Positive cases ────────────────────────────────────────────────

static_assert(is_owned_region_v<OR_int_a>);
static_assert(is_owned_region_v<OR_double_a>);
static_assert(is_owned_region_v<OR_int_b>);

// Cv-ref stripping — every reference category resolves identically.
static_assert(is_owned_region_v<OR_int_a&>);
static_assert(is_owned_region_v<OR_int_a&&>);
static_assert(is_owned_region_v<OR_int_a const&>);
static_assert(is_owned_region_v<OR_int_a const>);
static_assert(is_owned_region_v<OR_int_a const&&>);

// ── Negative cases ────────────────────────────────────────────────

static_assert(!is_owned_region_v<int>);
static_assert(!is_owned_region_v<int*>);
static_assert(!is_owned_region_v<int&>);
static_assert(!is_owned_region_v<void>);
static_assert(!is_owned_region_v<test_tag_a>);

// A struct that has the same shape but is not OwnedRegion is rejected.
struct LookalikeRegion { int* base; std::size_t count; };
static_assert(!is_owned_region_v<LookalikeRegion>);

// ── Concept form ─────────────────────────────────────────────────

static_assert(IsOwnedRegion<OR_int_a>);
static_assert(IsOwnedRegion<OR_int_a&&>);
static_assert(!IsOwnedRegion<int>);

// ── Element type / tag extraction ────────────────────────────────

static_assert(std::is_same_v<owned_region_value_t<OR_int_a>, int>);
static_assert(std::is_same_v<owned_region_value_t<OR_double_a>, double>);
static_assert(std::is_same_v<owned_region_tag_t<OR_int_a>, test_tag_a>);
static_assert(std::is_same_v<owned_region_tag_t<OR_int_b>, test_tag_b>);

// Cv-ref stripping — value_type / tag_type both unwrap.
static_assert(std::is_same_v<
    owned_region_value_t<OR_int_a const&>, int>);
static_assert(std::is_same_v<
    owned_region_tag_t<OR_int_a&&>, test_tag_a>);

// Distinct (T, Tag) → distinct trait specializations; element types
// agree only when they actually do.
static_assert(std::is_same_v<
    owned_region_value_t<OR_int_a>,
    owned_region_value_t<OR_int_b>>);
static_assert(!std::is_same_v<
    owned_region_tag_t<OR_int_a>,
    owned_region_tag_t<OR_int_b>>);

}  // namespace detail::is_owned_region_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Per the runtime-smoke-test discipline.  The trait is purely
// type-level; "exercising it at runtime" means computing the
// predicate value with a non-constant flag flow and confirming the
// trait's claims are not optimized into something else.

inline bool runtime_smoke_test() noexcept {
    using namespace detail::is_owned_region_self_test;

    // Volatile-bounded loop ensures the trait reads survive
    // dead-code elimination.
    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_owned_region_v<OR_int_a>;
        ok = ok && !is_owned_region_v<int>;
        ok = ok && IsOwnedRegion<OR_int_a&&>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
