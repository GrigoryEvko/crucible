#pragma once

// A brand is the identity of one region, view or borrow, carried as a
// type.  A permission tag names a kind of region; a brand names one
// instance of it.  Two regions minted for one tag are two brands, so a
// borrow of the first cannot stand in for the second, and the compiler
// refuses the swap by deduction rather than by review.  Rust gets the
// same effect from an invariant lifetime.  Here it is a template
// parameter, and it costs nothing at runtime: every brand is an empty
// class and every branded wrapper keeps the layout it had without one.
//
// Three facts about a brand, each load-bearing.
//
// 1.  A brand is a type, so a callee names it.  A function generic over
//     the region it is handed writes `template <class B> void f(Region<T,
//     Tag, B>&)`, and a function that takes two things which must be
//     about the same region writes both with one B, or asks for
//     `SameBrand<A, B>`.  There is no runtime brand to compare.
//
// 2.  One call site yields one brand.  A mint inside a loop body mints
//     one brand for every object the loop makes, and that is sound: the
//     objects never overlap in time, so a borrow of one cannot outlive
//     it into the next.  Inside a template the rule is measured, not
//     assumed: a mint whose tag comes from a template parameter yields
//     one brand per instantiation of the enclosing template, and a mint
//     whose arguments are all concrete yields one brand shared by every
//     instantiation, because the compiler resolves that call once.  Two
//     runtime objects that must be told apart are minted at two call
//     sites, or through a template that names the tag.
//
// 3.  A brand refuses by identity and a deleted rvalue twin refuses by
//     value category.  They are two mechanisms for two failures.  The
//     twin stops a borrow being taken from a temporary, which no brand
//     can see because a temporary has a brand like any other object.
//     The brand stops a borrow of one object proving anything about
//     another, which no twin can see because both objects are lvalues.
//     Both stay.
//
// The fresh brand is the closure type of a lambda written in a mint's
// defaulted last template parameter.  A lambda expression in a default
// template argument yields a distinct closure type each time the
// default is used, which is what makes the brand fresh per call site.
// The parameter comes after every parameter pack the mint declares, so
// a caller cannot spell a brand it already holds and mint a second
// token of the same identity: the pack absorbs the argument first.
// Measured on GCC 16.2.1 in this dialect, in test/foundation/test_brand.cpp.
//
// DefaultBrand is the erased identity.  Every spelling that names no
// brand, `Permission<Tag>` and the like, is on it, and under it every
// token of one tag is one type.  A share, a read loan, a parked token and
// an owned region never drop their brand.  A permission token drops its
// brand only through permission_erase_brand, where the holder cannot name
// the brand.  A borrow (Borrowed, BorrowedRef, ScopedView) converts to its
// erased spelling implicitly.  Nothing converts an erased value back to a
// brand, so no caller can give an erased value to code that asks for a
// brand.  utils/scripts/check-brand-drain.py lists
// each spelling on DefaultBrand, and the count of those sites can only
// decrease.

#include <foundation/Platform.h>

#include <type_traits>

namespace foundation::brand {

// The erased identity.  A named type, deliberately: a lambda here would
// make every spelling of `Permission<Tag>` a distinct type.
struct DefaultBrand {};

// A brand is an empty class type.  The closure type of a lambda that
// captures nothing is one, and so is DefaultBrand.
template <class B>
concept IsBrand = std::is_class_v<B> && std::is_empty_v<B>;

// A brand that names one instance rather than the erased identity.
template <class B>
concept IsFreshBrand = IsBrand<B> && !std::is_same_v<B, DefaultBrand>;

// A type that carries a brand exposes it as brand_type.
template <class T>
concept HasBrand =
    requires { typename std::remove_cvref_t<T>::brand_type; } && IsBrand<typename std::remove_cvref_t<T>::brand_type>;

template <class T>
    requires HasBrand<T>
using brand_of_t = typename std::remove_cvref_t<T>::brand_type;

// Two branded values are about the same instance.
template <class A, class B>
concept SameBrand = HasBrand<A> && HasBrand<B> && std::is_same_v<brand_of_t<A>, brand_of_t<B>>;

// A branded value whose brand is one instance's, not the erased one.
template <class T>
concept IsBranded = HasBrand<T> && IsFreshBrand<brand_of_t<T>>;

// A branded value on the erased identity.
template <class T>
concept IsErased = HasBrand<T> && std::is_same_v<brand_of_t<T>, DefaultBrand>;

// The brand a view of Carrier takes: the carrier's own when it has one,
// so the view is pinned to that instance, and the fresh one a mint
// supplies otherwise.
template <class Carrier, class Fresh>
struct inherited_or_fresh_brand {
    using type = Fresh;
};

template <class Carrier, class Fresh>
    requires HasBrand<Carrier>
struct inherited_or_fresh_brand<Carrier, Fresh> {
    using type = brand_of_t<Carrier>;
};

template <class Carrier, class Fresh>
using inherited_or_fresh_brand_t = typename inherited_or_fresh_brand<Carrier, Fresh>::type;

}  // namespace foundation::brand

// The fresh brand, for a mint's LAST template parameter and nowhere
// else.  Written as a macro because the lambda must appear in the
// default argument itself: an alias would be evaluated once and every
// mint would share one brand.
#define CRUCIBLE_FRESH_BRAND decltype([] {})
