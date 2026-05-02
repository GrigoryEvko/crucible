#pragma once

// ── crucible::safety::extract::is_borrowed_v ────────────────────────
//
// Wrapper-detection predicate for `safety::Borrowed<T, Source>`
// (#1080).  Mechanical extension of the established 24-header
// `safety/Is*.h` family.
//
// Surface:
//
//   is_borrowed_v<W>         constexpr bool — true iff W is a
//                            Borrowed<T, S> instantiation.
//   IsBorrowed<W>            concept gate.
//   borrowed_value_t<W>      extracts T (the element type).
//   borrowed_source_t<W>     extracts S (the Source phantom tag).
//
// LookalikeBorrowed witness asserts the pattern-match is template-
// spec-based (NOT duck-typing on member shapes — a struct with a
// `std::span<int> span_` member must NOT match).
//
// Why two separate headers (IsBorrowed vs IsBorrowedRef).  Mirrors
// the existing IsConsumerHandle / IsProducerHandle split — different
// templates, different extractors, separate compile-time deps.
// Combining would force every consumer that only needs one to drag
// in both partial-spec instantiations.
//
// References: WRAP-Borrowed-Integration-1 (#1086).

#include <crucible/safety/Borrowed.h>

#include <cstdlib>
#include <type_traits>

namespace crucible::safety::extract {

namespace detail {

template <typename T>
struct is_borrowed_impl : std::false_type {
    using element_type = void;
    using source_type  = void;
};

template <typename T, typename Source>
struct is_borrowed_impl<::crucible::safety::Borrowed<T, Source>>
    : std::true_type
{
    using element_type = T;
    using source_type  = Source;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_borrowed_v =
    detail::is_borrowed_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsBorrowed = is_borrowed_v<T>;

template <typename T>
    requires is_borrowed_v<T>
using borrowed_value_t =
    typename detail::is_borrowed_impl<std::remove_cvref_t<T>>::element_type;

template <typename T>
    requires is_borrowed_v<T>
using borrowed_source_t =
    typename detail::is_borrowed_impl<std::remove_cvref_t<T>>::source_type;

// ── Self-test ───────────────────────────────────────────────────────

namespace detail::is_borrowed_self_test {

struct OwnerA { int dummy = 0; };
struct OwnerB { int dummy = 0; };

using B_A   = ::crucible::safety::Borrowed<int,         OwnerA>;
using B_B   = ::crucible::safety::Borrowed<const char,  OwnerB>;
using B_dbl = ::crucible::safety::Borrowed<double,      OwnerA>;

// Positive cases.
static_assert( is_borrowed_v<B_A>);
static_assert( is_borrowed_v<B_B>);
static_assert( is_borrowed_v<B_dbl>);

// cvref strip.
static_assert( is_borrowed_v<B_A&>);
static_assert( is_borrowed_v<B_A const&>);
static_assert( is_borrowed_v<B_A&&>);

// Negative cases — non-Borrowed rejected.
static_assert(!is_borrowed_v<int>);
static_assert(!is_borrowed_v<int*>);
static_assert(!is_borrowed_v<std::span<int>>,    // bare span, not wrapped
    "Bare std::span MUST NOT satisfy is_borrowed_v — it's the "
    "underlying carrier, not the wrapper.  Without this rejection, "
    "untagged spans would slip through Borrowed-expecting concept "
    "gates.");
static_assert(!is_borrowed_v<B_A*>);
static_assert(!is_borrowed_v<void>);

// Lookalike rejection — pattern-match must be template-spec, not
// duck-typing.  A struct that has the same `span_` member name and
// type must NOT satisfy is_borrowed_v.
struct LookalikeBorrowed { std::span<int> span_; };
static_assert(!is_borrowed_v<LookalikeBorrowed>,
    "is_borrowed_v MUST reject lookalikes.  If this fires, the "
    "partial spec has weakened to duck-typing and downstream concept "
    "overloads will misfire on user types whose member shape "
    "coincidentally matches Borrowed's internal layout.");

// Concept gate parity.
static_assert( IsBorrowed<B_A>);
static_assert(!IsBorrowed<int>);

// Extractor types.
static_assert(std::is_same_v<borrowed_value_t<B_A>,    int>);
static_assert(std::is_same_v<borrowed_value_t<B_B>,    const char>);
static_assert(std::is_same_v<borrowed_source_t<B_A>,   OwnerA>);
static_assert(std::is_same_v<borrowed_source_t<B_B>,   OwnerB>);
static_assert(std::is_same_v<borrowed_source_t<B_dbl>, OwnerA>);   // same OwnerA as B_A

// ── Runtime smoke test ──────────────────────────────────────────────

inline void runtime_smoke_test() {
    if (!is_borrowed_v<B_A>)         std::abort();
    if ( is_borrowed_v<int>)         std::abort();
    if (!IsBorrowed<B_B>)            std::abort();
    if ( IsBorrowed<std::span<int>>) std::abort();
    if (!std::is_same_v<borrowed_value_t<B_dbl>,  double>) std::abort();
    if (!std::is_same_v<borrowed_source_t<B_dbl>, OwnerA>) std::abort();
}

}  // namespace detail::is_borrowed_self_test

}  // namespace crucible::safety::extract
