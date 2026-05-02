#pragma once

// ── crucible::safety::extract::is_borrowed_ref_v ────────────────────
//
// Wrapper-detection predicate for `safety::BorrowedRef<T>` (#1080).
// Mechanical extension of the established 24-header `safety/Is*.h`
// family.
//
// Surface:
//
//   is_borrowed_ref_v<W>     constexpr bool — true iff W is a
//                            BorrowedRef<T> instantiation.
//   IsBorrowedRef<W>         concept gate.
//   borrowed_ref_value_t<W>  extracts T (the referent type).
//
// LookalikeBorrowedRef witness asserts pattern-match is template-
// spec-based, not duck-typing on a `T* ptr_` member shape.
//
// Why separate from IsBorrowed.h.  Mirrors IsConsumerHandle /
// IsProducerHandle split — different templates, different extractors.
// Combining would force every consumer that only needs single-object
// borrow detection to also drag in span-borrow detection.
//
// References: WRAP-Borrowed-Integration-1 (#1086).

#include <crucible/safety/Borrowed.h>

#include <cstdlib>
#include <type_traits>

namespace crucible::safety::extract {

namespace detail {

template <typename T>
struct is_borrowed_ref_impl : std::false_type {
    using element_type = void;
};

template <typename T>
struct is_borrowed_ref_impl<::crucible::safety::BorrowedRef<T>>
    : std::true_type
{
    using element_type = T;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_borrowed_ref_v =
    detail::is_borrowed_ref_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsBorrowedRef = is_borrowed_ref_v<T>;

template <typename T>
    requires is_borrowed_ref_v<T>
using borrowed_ref_value_t =
    typename detail::is_borrowed_ref_impl<std::remove_cvref_t<T>>::element_type;

// ── Self-test ───────────────────────────────────────────────────────

namespace detail::is_borrowed_ref_self_test {

struct Holder { int v = 0; };

using R_int    = ::crucible::safety::BorrowedRef<int>;
using R_holder = ::crucible::safety::BorrowedRef<Holder>;
using R_double = ::crucible::safety::BorrowedRef<double>;
using R_const  = ::crucible::safety::BorrowedRef<const int>;

// Positive cases.
static_assert( is_borrowed_ref_v<R_int>);
static_assert( is_borrowed_ref_v<R_holder>);
static_assert( is_borrowed_ref_v<R_double>);
static_assert( is_borrowed_ref_v<R_const>);

// cvref strip.
static_assert( is_borrowed_ref_v<R_int&>);
static_assert( is_borrowed_ref_v<R_int const&>);
static_assert( is_borrowed_ref_v<R_int&&>);

// Negative cases.
static_assert(!is_borrowed_ref_v<int>);
static_assert(!is_borrowed_ref_v<int*>,           // bare pointer, not wrapped
    "Bare T* MUST NOT satisfy is_borrowed_ref_v — that's the "
    "underlying carrier, not the wrapper.  Without this rejection, "
    "raw pointers slip through BorrowedRef-expecting concept gates.");
static_assert(!is_borrowed_ref_v<int&>);
static_assert(!is_borrowed_ref_v<R_int*>);        // pointer-to-Ref
static_assert(!is_borrowed_ref_v<void>);

// Lookalike rejection — duck-typing must NOT match.
struct LookalikeBorrowedRef { int* ptr_; };
static_assert(!is_borrowed_ref_v<LookalikeBorrowedRef>,
    "is_borrowed_ref_v MUST reject lookalikes.  If this fires, the "
    "partial spec has weakened to duck-typing and any struct holding "
    "a T* would falsely match.");

// Concept gate parity.
static_assert( IsBorrowedRef<R_int>);
static_assert(!IsBorrowedRef<int*>);

// Extractor.
static_assert(std::is_same_v<borrowed_ref_value_t<R_int>,    int>);
static_assert(std::is_same_v<borrowed_ref_value_t<R_holder>, Holder>);
static_assert(std::is_same_v<borrowed_ref_value_t<R_const>,  const int>);

// ── Runtime smoke test ──────────────────────────────────────────────

inline void runtime_smoke_test() {
    if (!is_borrowed_ref_v<R_int>)    std::abort();
    if ( is_borrowed_ref_v<int*>)     std::abort();
    if (!IsBorrowedRef<R_holder>)     std::abort();
    if ( IsBorrowedRef<int>)          std::abort();
    if (!std::is_same_v<borrowed_ref_value_t<R_double>, double>) std::abort();
}

}  // namespace detail::is_borrowed_ref_self_test

}  // namespace crucible::safety::extract
