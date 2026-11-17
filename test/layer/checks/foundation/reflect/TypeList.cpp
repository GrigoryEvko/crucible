// The compile-time checks of foundation/reflect/TypeList.h.

#include <foundation/reflect/TypeList.h>

#include <type_traits>

namespace foundation::reflect {

namespace detail::type_list_checks {

// The operations on a bare pack.
static_assert(OccursIn<int, long, int> && !OccursIn<int, long, char> && !OccursIn<int>);
static_assert(OccursOnce<int, long, int> && !OccursOnce<int, int, long, int> && !OccursOnce<int>);
static_assert(!OccursIn<int, int const> && !OccursIn<int, int&>);
static_assert(index_of_v<int, int, long> == 0 && index_of_v<long, int, char, long> == 2);

// An index needs one occurrence: an absent type and a type that occurs two
// times have none.
template <class T, class... Ts>
concept HasIndexOf = requires { index_of_v<T, Ts...>; };
static_assert(HasIndexOf<long, int, long> && !HasIndexOf<char, int, long> && !HasIndexOf<int, int, long, int>);

// The operations of a Pack.
using Three = Pack<int, long, char>;
static_assert(PackType<Three> && PackType<Pack<>>);
static_assert(!PackType<int> && !PackType<Three const> && !PackType<Three&>);
static_assert(std::is_empty_v<Three>);
static_assert(pack_size_v<Three> == 3 && pack_size_v<Pack<>> == 0);
static_assert(std::is_same_v<pack_at_t<0, Three>, int> && std::is_same_v<pack_at_t<2, Three>, char>);
static_assert(pack_contains_v<long, Three> && !pack_contains_v<short, Three>);
static_assert(pack_index_of_v<int, Three> == 0 && pack_index_of_v<char, Three> == 2);
static_assert(std::is_same_v<pack_concat_t<>, Pack<>>);
static_assert(std::is_same_v<pack_concat_t<Three>, Three>);
static_assert(std::is_same_v<pack_concat_t<Pack<int>, Pack<>, Pack<long, char>>, Three>);
static_assert(pack_unique_v<Three> && pack_unique_v<Pack<>> && !pack_unique_v<Pack<int, long, int>>);

// A type in a nested Pack only is no part of the outer Pack.
static_assert(!pack_contains_v<int, Pack<Pack<int>, long>>);
static_assert(pack_index_of_v<Pack<int>, Pack<Pack<int>, long>> == 0);

// A class that only looks like a Pack is no Pack, and a list of a different
// class template is no list of Pack.
template <class... Ts>
struct NotAPack final {};
static_assert(!PackType<NotAPack<int>> && !PackType<Pack<int>&>);
static_assert(ListOf<NotAPack<int>, NotAPack> && !ListOf<Pack<int>, NotAPack> && !ListOf<NotAPack<int>, Pack>);

// pack_apply_t gives the types to a different template.
static_assert(std::is_same_v<pack_apply_t<NotAPack, Three>, NotAPack<int, long, char>>);

// list_concat_t joins lists of one class template, in order, and no list
// gives the empty list of the template.
static_assert(std::is_same_v<list_concat_t<NotAPack>, NotAPack<>>);
static_assert(std::is_same_v<list_concat_t<NotAPack, NotAPack<int>>, NotAPack<int>>);
static_assert(
    std::is_same_v<list_concat_t<NotAPack, NotAPack<int>, NotAPack<>, NotAPack<long, int>>, NotAPack<int, long, int>>);

// The concat refuses a list of a different template and a type that is no
// list.
template <template <class...> class List, class... Lists>
concept JoinsLists = requires { typename list_concat_t<List, Lists...>; };
static_assert(JoinsLists<NotAPack, NotAPack<int>, NotAPack<long>>);
static_assert(!JoinsLists<NotAPack, NotAPack<int>, Pack<long>> && !JoinsLists<NotAPack, NotAPack<int>, int>);
static_assert(!JoinsLists<Pack, Pack<int> const>);

}  // namespace detail::type_list_checks

}  // namespace foundation::reflect
