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
// 2.  A brand names one instance only while a scope bounds it (principle
//     P3 of misc/12_10_2026_linear.md).  One call site yields one brand, and
//     each run of the site yields that brand again.  Inside a template the
//     rule is measured, not assumed: a mint whose tag comes from a template
//     parameter yields one brand per instantiation of the enclosing
//     template, and a mint whose arguments are all concrete yields one brand
//     shared by every instantiation, because the compiler resolves that call
//     once.  The two kinds of mint follow from this fact.
//
//     A scope mint gives its brand to a body and to no result.  Its brand
//     is CRUCIBLE_SCOPE_BRAND, and the rule brand-escape refuses each result
//     of the mint and each object of static or thread storage duration that
//     holds the brand.  So no value of the brand outlives the call, two runs
//     of one site never hold two values of the brand at one time, and the
//     brand is fresh for each scope instance.  A fact that the brand proves
//     for one run, such as an index below the count of a view, then holds
//     in that run.  mint_branded_scope of foundation/core/Branded.h and the
//     frame doors with_mut of foundation/core/Frame.h are scope mints.
//
//     A mint that gives a branded value out, such as to(), the root
//     permission mint and mint_split, takes CRUCIBLE_FRESH_BRAND.  Two runs
//     of its site can give two live values of one brand, as two turns of a
//     loop that keep their tokens do.  Such a brand tells apart the values
//     of two sites, and it proves no fact that changes from run to run.  A
//     value that must be told apart from a value of a second run of its
//     site carries a run-time id: the Identity of foundation/core/Identity.h,
//     or the EndpointClaim of foundation/ChannelBinding.h for the one live
//     handle of a role.
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
// an owned region never drop their brand.  A permission token is on the
// erased identity only as the singleton root of its tag, the one live root
// of that tag.  No Ref converts to its erased spelling: erase_brand of
// foundation/core/Ref.h is the one door of a Ref, erase_brand of
// fixy/ScopedView.h is the one door of a view, and erase_brand of
// foundation/core/Strided.h is the one door of a unique strided view, which
// it consumes.  Nothing converts an erased value back to a brand, so no
// caller can give an erased value to code that asks for a brand.  The rule
// erased-brand (utils/assay/guard/engines/tsast/erased_brand.py) counts each
// spelling on DefaultBrand, and the count of those sites can only decrease.

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

// The mark of a member arena tag.  A member arena tag is the brand of the one
// arena that a class holds as a member, such as the scratch arena of a stage
// (foundation/core/BumpArena.h).  The tag is a class that the owner nests
// beside that member.  The rule brand-escape of the quarantine plugin refuses
// a tag that does not name exactly one member of the class that nests it, and
// a local or a static object of an arena of the tag.  So no second arena
// shares the brand, and a Ref of one arena does not fit a field that names a
// different arena of the class.  Two objects of the owner class still share
// the brand, which no type tells apart.
struct arena_member_brand {};

// The mark of a family arena tag.  A family arena tag is the brand of a
// family of arenas of one kind of data, such as the regions of the DAG.  No
// rule counts its arenas, so two arenas of the family share the brand: a Ref
// of one fits a field that names the other, and that identity needs a
// run-time id or a graph type that takes the brand of its own arena.
struct arena_family_brand {};

// The statement of a named arena tag that each arena of the named tag Other
// outlives each arena of the tag that states it.  An object of an arena keeps a
// borrow of a different arena only when the tag of its arena states the tag of
// that borrow (ArenaKeeps of foundation/core/BumpArena.h), so a pair of tags
// with no statement does not compile.  No mint proves the statement: the code
// that owns the two arenas keeps their order, and the comment of the tag tells
// how.  An epoch brand is not its member tag, so a statement of the tag admits
// nothing in an epoch.
template <class Other>
struct outlived_by {};

// The mark of an owner class with a member tag among its type arguments, such
// as BumpArena and EpochArena of foundation/core/BumpArena.h, whose first
// argument is the brand, and SlotMap of foundation/core/SlotMap.h, whose third
// argument is the brand.  The rule brand-escape reads it, with
// arena_member_brand on the argument at any position.
struct branded_arena {};

// The mark of an epoch brand: EpochOf<Tag> of foundation/core/BumpArena.h, the
// brand of the bytes of one epoch of an epoch arena.  The end of an epoch
// ends those bytes.  The rule brand-escape refuses an epoch brand in an object
// of static or thread storage duration, and in a member of a class that does
// not state the brand as its brand_type.
struct epoch_brand {};

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
