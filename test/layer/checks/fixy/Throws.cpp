// The compile-time checks of fixy/Throws.h.

#include <fixy/Throws.h>

namespace fixy::detail::throws_self_test {

namespace ctrl = ::fixy::atom::ctrl;

struct unrelated_tag {};
struct sample_exception {};

// A class template of the test.  The walk reads its arguments as the members
// of a type tree, as it reads the arguments of each class template.
template <typename... Members>
struct Nest {};

// The atom itself, at the default family and at a named one.
static_assert(type_tree_contains_throws_v<ctrl::throws<>>);
static_assert(type_tree_contains_throws_v<ctrl::throws<sample_exception>>);

// Qualifiers do not hide the carrier.
static_assert(type_tree_contains_throws_v<ctrl::throws<> const>);
static_assert(type_tree_contains_throws_v<ctrl::throws<>&>);
static_assert(type_tree_contains_throws_v<ctrl::throws<> const&>);
static_assert(type_tree_contains_throws_v<ctrl::throws<> volatile>);

// Nested at every depth, and beside unrelated members.
static_assert(type_tree_contains_throws_v<Nest<ctrl::throws<>>>);
static_assert(type_tree_contains_throws_v<Nest<int, ctrl::throws<>>>);
static_assert(type_tree_contains_throws_v<Nest<int, ctrl::throws<> const&>>);
static_assert(type_tree_contains_throws_v<Nest<int, Nest<ctrl::throws<>, double>>>);
static_assert(type_tree_contains_throws_v<Nest<int, Nest<unrelated_tag, Nest<ctrl::throws<>>>>>);

// A named family nested, which is the case that a search for throws<>
// alone misses.
static_assert(type_tree_contains_throws_v<Nest<int, ctrl::throws<sample_exception>>>);
static_assert(type_tree_contains_throws_v<Nest<Nest<ctrl::throws<sample_exception>>, double>>);

// Nothing else answers true.
static_assert(!type_tree_contains_throws_v<int>);
static_assert(!type_tree_contains_throws_v<int const&>);
static_assert(!type_tree_contains_throws_v<void>);
static_assert(!type_tree_contains_throws_v<unrelated_tag>);
static_assert(!type_tree_contains_throws_v<Nest<int, double>>);
static_assert(!type_tree_contains_throws_v<Nest<int, Nest<unrelated_tag, double>>>);

// The exception family is a plain tag, not an atom, so naming it alone
// is not naming a throw.
static_assert(!type_tree_contains_throws_v<ctrl::any_exception>);
static_assert(!type_tree_contains_throws_v<Nest<ctrl::any_exception>>);

// Deriving from the atom to reach the match is not merely unmatched, it
// does not compile: the atom is final.  That is what closes the cheat,
// so it is what this cell asserts.  The reflection query would refuse a
// derived type anyway, because it reports the type it is asked about and
// never its bases.
static_assert(std::is_final_v<ctrl::throws<>>);
static_assert(std::is_final_v<ctrl::throws<sample_exception>>);

// The exact-type search matches an exact type, including for a needle
// that has nothing to do with control flow.
static_assert(type_tree_contains_v<unrelated_tag, unrelated_tag>);
static_assert(type_tree_contains_v<unrelated_tag, Nest<int, unrelated_tag>>);
static_assert(type_tree_contains_v<unrelated_tag, Nest<int, unrelated_tag const&>>);
static_assert(!type_tree_contains_v<unrelated_tag, Nest<int, double>>);
static_assert(type_tree_contains_v<ctrl::throws<>, Nest<int, ctrl::throws<>>>);

// And it is exact: the default family is not the named one.
static_assert(!type_tree_contains_v<ctrl::throws<>, ctrl::throws<sample_exception>>);
static_assert(type_tree_contains_throws_v<ctrl::throws<sample_exception>>);

// A carrier with a non-type parameter is descended through its type
// arguments.
template <int N, typename T>
struct nttp_carrier {};
static_assert(type_tree_contains_throws_v<nttp_carrier<1, ctrl::throws<>>>);
static_assert(type_tree_contains_throws_v<Nest<int, nttp_carrier<2, ctrl::throws<sample_exception>>>>);
static_assert(!type_tree_contains_throws_v<nttp_carrier<1, int>>);

// A plain class that holds a throwing wrapper in a member is found.  A
// template argument never names that member, so a walk over template
// arguments alone does not see it.
struct holds_throwing_member {
    Nest<int, ctrl::throws<>> held;
};
struct derives_throwing_base : Nest<ctrl::throws<>> {};
static_assert(type_tree_contains_throws_v<holds_throwing_member>);
static_assert(type_tree_contains_throws_v<derives_throwing_base>);
static_assert(type_tree_contains_throws_v<Nest<holds_throwing_member>>);

// A member alias is a declaration, not a component, so it is not read.
struct names_throws_in_an_alias {
    using hidden = ctrl::throws<>;
};
static_assert(!type_tree_contains_throws_v<names_throws_in_an_alias>);

}  // namespace fixy::detail::throws_self_test
