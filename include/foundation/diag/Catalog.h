#pragma once

// The closed catalog of the diagnostic tags.  This header includes each
// domain file of foundation/diag/tags/, so the walk that derives the catalog
// sees each tag, whatever else a translation unit includes.  It derives the
// tuple of the tags from the Category enum of foundation/diag/Tag.h, and it
// gives the readers that index the tuple and the prose of each tag.  A
// header that names a category or one tag includes foundation/diag/Tag.h or
// the domain file of the tag, and not this header.

#include <foundation/Platform.h>
#include <foundation/diag/Tag.h>
#include <foundation/diag/tags/Aborts.h>
#include <foundation/diag/tags/Rows.h>
#include <foundation/diag/tags/Tiers.h>
#include <foundation/diag/tags/Wrappers.h>

#include <cstddef>
#include <meta>
#include <type_traits>
#include <utility>
#include <vector>

namespace foundation::diag {

namespace detail {

// One tag reflection per enumerator, in declaration order, or a reflection
// of void for an enumerator that no tag spells.  A tag is a class declared
// directly in foundation::diag and derived from tag_base, so a stray type
// of the same name in this namespace cannot stand in for a tag. The mirror check in the
// check file of this header pins declaration order to the integer values.
// One walk over the namespace collects the tags, and each enumerator then
// compares identifiers, so the cost is linear in the members of the
// namespace plus the product of the two counts of names.  It is a
// template, so a name that reads it at namespace scope can depend on a
// template parameter, and only a translation unit that instantiates that
// name evaluates the walk.
template <class Unused = void>
[[nodiscard]] consteval std::vector<std::meta::info> catalog_tags() noexcept {
    std::vector<std::meta::info> classes;
    std::vector<std::string_view> identifiers;
    for (const auto m : std::meta::members_of(^^::foundation::diag, std::meta::access_context::unchecked())) {
        if (!std::meta::is_type(m) || std::meta::is_type_alias(m) || !std::meta::is_class_type(m)) continue;
        if (!std::meta::has_identifier(m)) continue;
        if (m == ^^tag_base || !std::meta::is_base_of_type(^^tag_base, m)) continue;
        classes.push_back(m);
        identifiers.push_back(std::meta::identifier_of(m));
    }
    std::vector<std::meta::info> tags;
    for (const auto en : std::meta::enumerators_of(^^Category)) {
        const std::string_view identifier = std::meta::identifier_of(en);
        std::meta::info found = ^^void;
        for (std::size_t index = 0; index < identifiers.size(); ++index) {
            if (identifiers[index] == identifier) {
                found = classes[index];
                break;
            }
        }
        tags.push_back(found);
    }
    return tags;
}

// The two checks that each enumerator names a tag and that each tag names
// an enumerator are in the check file of this header.  GCC evaluates a
// call with constant arguments in the body of a function that is not a
// template.  In this header, such a check would walk the catalog in each
// includer, also in an includer that reads no tag.

// The tuple of the tag types at the enumerators' positions, derived from
// the enum. tag_of_t<C> and category_of_v<Tag> index it.
//
// Each table below is a template, and each name that reads a table
// depends on a template parameter.  So only a translation unit that
// names a table builds it, and a translation unit that includes this
// header for one tag pays for no table.
template <class Unused>
struct catalog_of {
    using type = [:std::meta::substitute(^^std::tuple, catalog_tags<Unused>()):];
};

}  // namespace detail

template <class Unused = void>
using Catalog = typename detail::catalog_of<Unused>::type;

// An alias template cannot carry a requires-clause, so the constraint on the
// Category value lives on a struct template that the alias forwards to.
namespace detail {

template <Category C, class Unused = void>
struct tag_of_impl {
    static_assert(static_cast<std::size_t>(C) < catalog_size,
                  "tag_of_t<C>: Category value is out of catalog range. "
                  "Likely cause: a Category value cast from an out-of-range "
                  "integer via reinterpret_cast / static_cast without a "
                  "preceding range check.");
    using type = std::tuple_element_t<static_cast<std::size_t>(C), Catalog<Unused>>;
};

}  // namespace detail

template <Category C>
using tag_of_t = typename detail::tag_of_impl<C>::type;

namespace detail {

template <typename Tag, typename Unused = void, std::size_t... Is>
[[nodiscard]] consteval std::size_t category_index_fold(std::index_sequence<Is...>) noexcept {
    std::size_t result = sizeof...(Is);  // sentinel: not found
    // A later match overwrites an earlier one. No tag type appears twice in
    // the catalog, so at most one term of the fold ever assigns.
    ((std::is_same_v<Tag, std::tuple_element_t<Is, Catalog<Unused>>> ? (void)(result = Is) : (void)0), ...);
    return result;
}

template <typename Tag>
[[nodiscard]] consteval std::size_t category_index_of() noexcept {
    return category_index_fold<Tag>(std::make_index_sequence<catalog_size>{});
}

template <typename Tag>
struct category_of_impl {
    static_assert(is_diagnostic_class_v<Tag>, "category_of_v<Tag>: Tag must be derived from "
                                              "foundation::diag::tag_base.  Read Tag::name, Tag::description "
                                              "and Tag::remediation directly to access the fields of a tag "
                                              "without the Category indirection.");
    static constexpr std::size_t index = category_index_of<Tag>();
    static_assert(index < catalog_size, "category_of_v<Tag>: Tag is not registered in the foundation "
                                        "Catalog.  The Category enum is CLOSED to the tags of the "
                                        "Catalog tuple; user-defined tags inherit from "
                                        "tag_base and participate in the type-level diagnostic surface "
                                        "(diagnostic_name_v, Diagnostic<UserTag, Ctx...>) without "
                                        "occupying a Category slot.  If you genuinely need this tag "
                                        "in the Category enum, add it to foundation/diag/Catalog.h's Catalog "
                                        "and Category at the same integer index (APPEND-ONLY).");
    static constexpr Category value = static_cast<Category>(index);
};

}  // namespace detail

template <typename Tag>
inline constexpr Category category_of_v = detail::category_of_impl<Tag>::value;

namespace detail {

// One text field of every tag, at the tag's Category index, derived from
// the tuple, and the sentinel after the last field.  Select tells which
// field, and the three tables share one walk.  Each table is a variable
// template, and a translation unit builds it only when a call reads it.  The
// table is a C array, and its index calls no function of the library in a
// build.
template <auto Select, class Unused, std::size_t... Is>
inline constexpr std::string_view catalog_fields_v[sizeof...(Is) + 1] = {
    Select.template operator()<std::tuple_element_t<Is, Catalog<Unused>>>()...,
    "<unknown Category>"
};

// Reads the field of c in the table of Select.  An index past the catalog
// reads the sentinel.  A value cast in from an out-of-range integer then
// gives the sentinel, and the read stays in the table.
template <auto Select, class Unused, std::size_t... Is>
[[nodiscard]] constexpr std::string_view catalog_field(Category c, std::index_sequence<Is...>) noexcept {
    const auto index = static_cast<std::size_t>(std::to_underlying(c));
    return catalog_fields_v<Select, Unused, Is...>[index < sizeof...(Is) ? index : sizeof...(Is)];
}

}  // namespace detail

// The accessors answer with the tag's own field for every enumerator and
// with the sentinel for a value cast in from an out-of-range integer.
// Each is a template with one defaulted parameter, so the table that it
// reads is built only where a call instantiates it.  A call names no
// template argument: name_of(c).
template <class Unused = void>
[[nodiscard]] constexpr std::string_view name_of(Category c) noexcept {
    return detail::catalog_field<detail::select_name, Unused>(c, std::make_index_sequence<catalog_size>{});
}

template <class Unused = void>
[[nodiscard]] constexpr std::string_view description_of(Category c) noexcept {
    return detail::catalog_field<detail::select_description, Unused>(c, std::make_index_sequence<catalog_size>{});
}

template <class Unused = void>
[[nodiscard]] constexpr std::string_view remediation_of(Category c) noexcept {
    return detail::catalog_field<detail::select_remediation, Unused>(c, std::make_index_sequence<catalog_size>{});
}

}  // namespace foundation::diag
