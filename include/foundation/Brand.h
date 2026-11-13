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
// Measured on GCC 16.2.1 in this dialect, in test/foundation/test_brand.cpp.
//
// Two rules keep a caller from a second token of a brand that it holds.
// The parameter comes after every parameter pack of the mint, so the pack
// takes each explicit argument, and no caller spells the brand.  The pack
// does not stop deduction.  The address of a mint with a target function
// type deduces each template parameter from that type, and so does the
// declared type of an explicit specialization.  A brand that the result
// type names directly then comes from a type that names a held brand, and
// the fresh default does not apply.  So the result type of a mint holds
// its brand in undeducible_brand_t, which deduction cannot set, and each
// use of the mint takes the fresh default.  The negative fixture
// neg_brand_root_mint_address binds the root mint to a function reference
// with a held brand, which deduces as an address does, and the check
// AddressTakesBrand of test_brand.cpp holds the address itself.
//
// DefaultBrand is the erased identity.  Every spelling that names no
// brand, `Permission<Tag>` and the like, is on it, and under it every
// token of one tag is one type.  A share, a read loan, a parked token and
// an owned region never drop their brand.  A permission token drops its
// brand only through permission_erase_brand, where the holder cannot name
// the brand.  A borrow (Borrowed, BorrowedRef, ScopedView) converts to its
// erased spelling implicitly.  Nothing converts an erased value back to a
// brand, so no caller can give an erased value to code that asks for a
// brand.  The rule erased-brand (utils/assay/guard/engines/tsast/
// erased_brand.py) counts each spelling on DefaultBrand, and the count of
// those sites can only decrease.

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

// The brand of the result of a mint, in a context where template argument
// deduction cannot set it (the fresh brand, below).  The member type of a
// class template is such a context, so a target type and the declared type
// of a specialization set no brand.  The rule specialization-home refuses
// a specialization of the class outside this header.
template <class Brand>
struct undeducible_brand {
    using type = Brand;
};

template <class Brand>
using undeducible_brand_t = typename undeducible_brand<Brand>::type;

// The mark of a scope brand.  A scope brand names one call of a scope mint,
// such as mint_branded_scope of foundation/core/Branded.h, and no value of it
// outlives that call.  One call site gives each of its calls one brand, so a
// value that one call keeps would stand in for a value of the next call.
// CRUCIBLE_SCOPE_BRAND puts this annotation on the call operator of its
// lambda, and reflection and the rule brand-escape of the quarantine plugin
// read it.  The plugin makes each object of static or thread storage duration
// that holds the brand a compile error, and each result of the mint that holds
// it.
struct scope_brand {};

}  // namespace foundation::brand

// The fresh brand, for a mint's LAST template parameter and nowhere
// else.  Written as a macro because the lambda must appear in the
// default argument itself: an alias would be evaluated once and every
// mint would share one brand.  The result type of the mint names the
// parameter through undeducible_brand_t, so no deduction replaces it.
#define CRUCIBLE_FRESH_BRAND decltype([] {})

// The fresh brand of a scope mint, with the mark foundation::brand::scope_brand
// on the call operator of its lambda.  It obeys each rule of a fresh brand.
#define CRUCIBLE_SCOPE_BRAND decltype([][[= ::foundation::brand::scope_brand{}]] {})
