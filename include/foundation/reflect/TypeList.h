#pragma once

// The TypeList family: a list of types with no value, and the operations on
// a list of types.
//
// Pack<Ts...> is a list of types.  Its operations are variable templates and
// aliases over one class, so each has an instantiation depth of one:
//
//   pack_size_v<P>             the count of types
//   pack_at_t<I, P>            the type at index I, for I below the count
//   pack_contains_v<T, P>      true when T is one of the types
//   pack_index_of_v<T, P>      the index of T, when T is in P one time
//   pack_concat_t<Ps...>       one Pack of the types of each P, in order
//   pack_apply_t<Template, P>  Template<Ts...>
//   pack_unique_v<P>           true when no type is in P two times
//
// Three operations take a bare pack of types, for a template that holds
// its types as a pack, such as the alternatives of a OneOf:
//
//   OccursIn<T, Ts...>         T is one of Ts
//   OccursOnce<T, Ts...>       T is one of Ts exactly one time
//   index_of_v<T, Ts...>       the index of T, when T is in Ts one time
//
// list_concat_t<List, Ls...> joins the lists of a class template other than
// Pack, such as the lists of a walk of a session type.  Each of Ls is a
// List<...>, and the result is one List<...> of their types, in order.
// pack_concat_t<Ps...> is list_concat_t<Pack, Ps...>.
//
// The count and the type at an index of a bare pack are the language forms
// sizeof...(Ts) and Ts...[I].  A template that refuses each instantiation
// states static_assert(false, reason).  The language evaluates such an
// assertion only in an instantiation, so the family has no false constant
// that waits for a template argument.
//
// No operation reads reflection, so the header includes no <meta>.  Each
// gate is a concept or a partial specialization of a class template of this
// file.  No unit can specialize a concept, and the rule specialization-home
// refuses a specialization of a class template of this file in each other
// file, so no unit can change an answer.

#include <cstddef>
#include <type_traits>

namespace foundation::reflect {

// T is one of Ts.  A concept, and not a variable template, because no unit
// can specialize a concept.  Complexity: linear in the count of Ts.
template <class T, class... Ts>
concept OccursIn = (std::is_same_v<T, Ts> || ...);

// T is one of Ts exactly one time.  Complexity: linear in the count of Ts.
template <class T, class... Ts>
concept OccursOnce = (std::size_t{0} + ... + std::size_t{std::is_same_v<T, Ts>}) == 1;

namespace detail {

// The index of the one T in Ts.  The caller holds OccursOnce<T, Ts...>, so
// the walk stops at a match.  Complexity: linear in the count of Ts, at
// compile time only.
template <class T, class... Ts>
[[nodiscard]] consteval std::size_t index_of_() noexcept {
    constexpr bool matches[] = {std::is_same_v<T, Ts>..., false};
    std::size_t index = 0;
    while (!matches[index]) {
        ++index;
    }
    return index;
}

}  // namespace detail

template <class T, class... Ts>
    requires OccursOnce<T, Ts...>
inline constexpr std::size_t index_of_v = detail::index_of_<T, Ts...>();

namespace detail {

// The step of the fold of list_concat_t: the types of the lists so far, in
// one List<...>.  The primary has no definition, so a type that is no
// List<...> has no step.  Only this class has the operator, so no list gets
// an operator.
template <template <class...> class List, class Joined>
struct ListJoin;

template <template <class...> class List, class... Ts>
struct ListJoin<List, List<Ts...>> {
    using type = List<Ts...>;

    template <class... Us>
    friend consteval ListJoin<List, List<Ts..., Us...>> operator|(ListJoin, ListJoin<List, List<Us...>>) noexcept {
        return {};
    }
};

}  // namespace detail

// A List<Ts...> for some Ts, with no cv-qualifier and no reference.
template <class Candidate, template <class...> class List>
concept ListOf = requires { typename detail::ListJoin<List, Candidate>::type; };

// One List<...> of the types of each of Lists, in order.  The fold makes one
// step for each list, with an instantiation depth of one.
template <template <class...> class List, class... Lists>
    requires(ListOf<Lists, List> && ...)
using list_concat_t =
    typename decltype((detail::ListJoin<List, List<>>{} | ... | detail::ListJoin<List, Lists>{}))::type;

// A list of types, for deduction only.  It has no member.
template <class... Ts>
struct Pack final {};

// A Pack<Ts...> for some Ts, with no cv-qualifier and no reference.
template <class P>
concept PackType = ListOf<P, Pack>;

namespace detail {

// The type at index I of Ts.  A class defers the index, because a Pack of
// no type cannot name a type of its pack in a declaration.
template <std::size_t I, class... Ts>
struct PackAt {
    using type = Ts...[I];
};

// The operations of one Pack.  The primary has no definition, so a type
// that is no Pack has no operation.  Each constraint of an operation is a
// constraint of a member, so a gate reads only its own instance.
template <class P>
struct PackParts;

template <class... Ts>
struct PackParts<Pack<Ts...>> {
    static constexpr std::size_t size = sizeof...(Ts);

    template <std::size_t I>
        requires(I < sizeof...(Ts))
    using at = typename PackAt<I, Ts...>::type;

    template <class T>
    static constexpr bool contains = OccursIn<T, Ts...>;

    template <class T>
        requires OccursOnce<T, Ts...>
    [[nodiscard]] static consteval std::size_t index_of() noexcept {
        return detail::index_of_<T, Ts...>();
    }

    static constexpr bool unique = (OccursOnce<Ts, Ts...> && ...);

    template <template <class...> class Template>
    using apply = Template<Ts...>;
};

}  // namespace detail

template <PackType P>
inline constexpr std::size_t pack_size_v = detail::PackParts<P>::size;

template <std::size_t I, PackType P>
using pack_at_t = typename detail::PackParts<P>::template at<I>;

template <class T, PackType P>
inline constexpr bool pack_contains_v = detail::PackParts<P>::template contains<T>;

template <class T, PackType P>
inline constexpr std::size_t pack_index_of_v = detail::PackParts<P>::template index_of<T>();

template <PackType... Ps>
using pack_concat_t = list_concat_t<Pack, Ps...>;

template <template <class...> class Template, PackType P>
using pack_apply_t = typename detail::PackParts<P>::template apply<Template>;

template <PackType P>
inline constexpr bool pack_unique_v = detail::PackParts<P>::unique;

}  // namespace foundation::reflect
