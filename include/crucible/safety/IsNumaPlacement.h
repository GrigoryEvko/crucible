#pragma once

// ── crucible::safety::extract::is_numa_placement_v ──────────────────
//
// FOUND-D30 (seventh of batch — third product wrapper).  Wrapper-
// detection predicate for `NumaPlacement<T>`.
//
// Mechanical extension of the IsBudgeted product-wrapper template,
// with a distinguishing layout invariant.  NumaPlacement carries a
// (NumaNodeId, AffinityMask) runtime grade pair where:
//   - NumaNodeId is a 1-byte enum
//   - AffinityMask is a 32-byte array<uint64_t, 4> (256 CPU bits)
// Total grade overhead = kAffinityBytes + 1 (≥ 33 bytes), packed
// to kPlacementBytes (40 bytes) due to alignment of the 8-byte
// AffinityMask words.
//
// The detector is indifferent to the size — partial spec keys on
// the wrapper class.  But layout assertions in tests differ from
// Budgeted/EpochVersioned (16-byte grade) — pinned distinctly.
//
// ── What this header ships ──────────────────────────────────────────
//
//   is_numa_placement_v<T>          Variable template; cv-ref-stripped.
//   IsNumaPlacement<T>              Concept form.
//   numa_placement_value_t<T>       Wrapped element type; constrained.

#include <crucible/safety/NumaPlacement.h>

#include <type_traits>

namespace crucible::safety::extract {

namespace detail {

template <typename T>
struct is_numa_placement_impl : std::false_type {
    using value_type = void;
};

template <typename U>
struct is_numa_placement_impl<::crucible::safety::NumaPlacement<U>>
    : std::true_type
{
    using value_type = U;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_numa_placement_v =
    detail::is_numa_placement_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsNumaPlacement = is_numa_placement_v<T>;

template <typename T>
    requires is_numa_placement_v<T>
using numa_placement_value_t =
    typename detail::is_numa_placement_impl<
        std::remove_cvref_t<T>>::value_type;

// ── Self-test ─────────────────────────────────────────────────────

namespace detail::is_numa_placement_self_test {

using NP_int      = ::crucible::safety::NumaPlacement<int>;
using NP_double   = ::crucible::safety::NumaPlacement<double>;
using NP_char     = ::crucible::safety::NumaPlacement<char>;
using NP_uint64   = ::crucible::safety::NumaPlacement<std::uint64_t>;

static_assert(is_numa_placement_v<NP_int>);
static_assert(is_numa_placement_v<NP_double>);
static_assert(is_numa_placement_v<NP_char>);
static_assert(is_numa_placement_v<NP_uint64>);

static_assert(is_numa_placement_v<NP_int&>);
static_assert(is_numa_placement_v<NP_int const&>);

static_assert(!is_numa_placement_v<int>);
static_assert(!is_numa_placement_v<int*>);
static_assert(!is_numa_placement_v<void>);

struct LookalikeNumaPlacement {
    int value;
    ::crucible::safety::NumaNodeId node_field;
    ::crucible::safety::AffinityMask aff_field;
};
static_assert(!is_numa_placement_v<LookalikeNumaPlacement>);

static_assert(!is_numa_placement_v<NP_int*>);

static_assert(IsNumaPlacement<NP_int>);
static_assert(!IsNumaPlacement<int>);

static_assert(std::is_same_v<numa_placement_value_t<NP_int>,    int>);
static_assert(std::is_same_v<numa_placement_value_t<NP_double>, double>);
static_assert(std::is_same_v<numa_placement_value_t<NP_uint64>, std::uint64_t>);

// Layout invariant — DISTINGUISHED from Budgeted/EpochVersioned.
// NumaPlacement carries 33-byte grade (1 byte NumaNodeId + 32 byte
// AffinityMask), packed to 40 bytes with alignment.
static_assert(sizeof(NP_int)    >= sizeof(int)    + 33);
static_assert(sizeof(NP_double) >= sizeof(double) + 33);

}  // namespace detail::is_numa_placement_self_test

inline bool is_numa_placement_smoke_test() noexcept {
    using namespace detail::is_numa_placement_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_numa_placement_v<NP_int>;
        ok = ok && is_numa_placement_v<NP_double>;
        ok = ok && !is_numa_placement_v<int>;
        ok = ok && IsNumaPlacement<NP_int&&>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
