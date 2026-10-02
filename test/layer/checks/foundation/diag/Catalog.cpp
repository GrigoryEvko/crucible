// The compile-time checks of foundation/diag/Catalog.h.

#include <foundation/diag/Catalog.h>

namespace foundation::diag {

namespace detail {

[[nodiscard]] consteval bool every_category_names_a_tag() noexcept {
    for (const auto tag : catalog_tags()) {
        if (tag == ^^void) return false;
    }
    return true;
}

// True when some Category enumerator spells `identifier`.
[[nodiscard]] consteval bool category_names_(std::string_view identifier) noexcept {
    for (const auto en : std::meta::enumerators_of(^^Category)) {
        if (std::meta::identifier_of(en) == identifier) return true;
    }
    return false;
}

// The walk of catalog_tags runs from the enum to the tags.  This one runs
// the other way, over the tag classes declared directly in
// foundation::diag.  A tag written above the enum and then forgotten in
// it reaches no Category, so tag_of_t and category_of_v never name it and
// the three accessors never answer with its text.
//
// This file includes the header and no other, so the walk reads the
// namespace after every shipped tag and after the enum.  A user extension
// declared in a later header is a different thing: the Category enum is
// closed, and such a tag is meant to have no enumerator.  A namespace has
// no access control, so the current context reads each of its members.
[[nodiscard]] consteval bool every_tag_names_a_category() noexcept {
    for (const auto m : std::meta::members_of(^^::foundation::diag, std::meta::access_context::current())) {
        if (!std::meta::is_type(m) || std::meta::is_type_alias(m) || !std::meta::is_class_type(m)) continue;
        if (m == ^^tag_base || !std::meta::is_base_of_type(^^tag_base, m)) continue;
        if (!std::meta::has_identifier(m)) continue;
        if (!category_names_(std::meta::identifier_of(m))) return false;
    }
    return true;
}

static_assert(every_category_names_a_tag(), "A Category enumerator names no tag: no class derived from "
                                            "tag_base with that identifier is declared in foundation::diag. "
                                            "Declare the tag struct above the enum, spelled exactly as the "
                                            "enumerator, or remove the enumerator.");

static_assert(every_tag_names_a_category(), "A tag class declared in foundation::diag has no Category "
                                            "enumerator. The tag is then unreachable through tag_of_t, "
                                            "category_of_v, name_of, description_of and remediation_of. "
                                            "Append an enumerator spelled exactly as the class, at the next "
                                            "free value, or move the class out of foundation::diag if it is "
                                            "meant to stay outside the catalog.");

}  // namespace detail

}  // namespace foundation::diag

namespace foundation::diag::detail::diag_self_test {

static_assert(is_diagnostic_class_v<EffectRowMismatch>);
static_assert(is_diagnostic_class_v<DetSafeLeak>);
static_assert(is_diagnostic_class_v<NumericalTierMismatch>);

static_assert(!is_diagnostic_class_v<tag_base>);

static_assert(!is_diagnostic_class_v<int>);
static_assert(!is_diagnostic_class_v<void>);

struct random_struct_for_test {};
static_assert(!is_diagnostic_class_v<random_struct_for_test>);

struct user_defined_tag : tag_base {
    static constexpr std::string_view name = "UserDefinedTag";
    static constexpr std::string_view description = "user-extension test";
    static constexpr std::string_view remediation = "this is a self-test";
};
static_assert(is_diagnostic_class_v<user_defined_tag>);
static_assert(diagnostic_name_v<user_defined_tag> == "UserDefinedTag");

// The visitor further down walks indices 0 to catalog_size - 1 and so counts
// the tuple. The tuple is derived from the enum, so the two counts agree by
// construction; the count is kept as the witness that the derivation saw
// every enumerator.
inline constexpr auto category_enumerators = std::define_static_array(std::meta::enumerators_of(^^Category));

inline constexpr std::size_t category_count = category_enumerators.size();

static_assert(category_count == catalog_size, "Category enum cardinality and Catalog tuple "
                                              "size diverged.  Every Category enumerator must have a matching "
                                              "tag type at the same integer index in the Catalog tuple "
                                              "(append-only discipline).  Likely cause: a new Category value "
                                              "was added without appending the corresponding tag struct + tag "
                                              "specialization, OR a tag was appended to Catalog without "
                                              "shipping the Category enumerator.  catalog_size is a literal, "
                                              "so that no includer of the header walks the enum: write the "
                                              "new count in its initializer.");

[[nodiscard]] consteval std::size_t enumerator_position(std::meta::info enumerator) noexcept {
    for (std::size_t i = 0; i < category_enumerators.size(); ++i) {
        if (category_enumerators[i] == enumerator) return i;
    }
    return category_enumerators.size();  // sentinel: not found
}

// The tuple is derived from the enum in declaration order, so what is
// checked here is the pin: the enumerator at position I must have the value
// I, the tuple must hold its tag at I, that tag's name must spell the
// enumerator, and the three accessors must answer for it with the tag's own
// fields rather than the sentinel.
[[nodiscard]] consteval bool category_mirrors_catalog() noexcept {
    constexpr std::string_view sentinel{"<unknown Category>"};
    bool mirrors = true;
    // -Wshadow fires spuriously on the expansion-statement induction variable.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : category_enumerators) {
        constexpr std::size_t index = enumerator_position(en);
        constexpr Category c = [:en:];
        using tag = std::tuple_element_t<index, Catalog<>>;
        mirrors = mirrors && static_cast<std::size_t>(std::to_underlying(c)) == index
               && std::is_same_v<tag, tag_of_t<c>> && std::meta::identifier_of(en) == tag::name
               && name_of(c) == tag::name && description_of(c) == tag::description
               && remediation_of(c) == tag::remediation && name_of(c) != sentinel && description_of(c) != sentinel
               && remediation_of(c) != sentinel;
    }
#pragma GCC diagnostic pop
    return mirrors;
}

static_assert(category_mirrors_catalog(), "The Category enum and the Catalog tuple drifted apart. For the "
                                          "enumerator at position I: its value must be I, the tuple must hold "
                                          "its tag at I, the tag's name must spell the enumerator, and name_of "
                                          "/ description_of / remediation_of must return that tag's fields. "
                                          "Likely cause: an enumerator inserted at a non-terminal position "
                                          "(violates the APPEND-ONLY discipline), an enumerator given a "
                                          "non-matching value, or a tag whose name field does not spell its "
                                          "class.");

template <std::size_t... Is>
[[nodiscard]] consteval bool category_of_reverse_map_impl(std::index_sequence<Is...>) noexcept {
    return ((category_of_v<std::tuple_element_t<Is, Catalog<>>> == static_cast<Category>(Is)) && ...);
}

[[nodiscard]] consteval bool category_of_reverse_map() noexcept {
    return category_of_reverse_map_impl(std::make_index_sequence<catalog_size>{});
}

static_assert(category_of_reverse_map(), "category_of_v<Tag> does not round-trip with tag_of_t<C>. "
                                         "Likely cause: catalog_category_bijection drift (see preceding "
                                         "assertion) or category_index_fold's match dispatch was modified "
                                         "to break uniqueness.");

template <std::size_t... Is>
[[nodiscard]] consteval bool catalog_names_distinct_impl(std::index_sequence<Is...>) noexcept {
    constexpr auto names = std::array<std::string_view, sizeof...(Is)>{std::tuple_element_t<Is, Catalog<>>::name...};
    for (std::size_t i = 0; i < names.size(); ++i) {
        for (std::size_t j = i + 1; j < names.size(); ++j) {
            if (names[i] == names[j]) return false;
        }
    }
    return true;
}

[[nodiscard]] consteval bool catalog_names_distinct() noexcept {
    return catalog_names_distinct_impl(std::make_index_sequence<catalog_size>{});
}

static_assert(catalog_names_distinct(), "Two or more tags in Catalog share the same `name` field. "
                                        "Diagnostic names must be unique so build-log greps return one "
                                        "tag per match.");

template <std::size_t... Is>
[[nodiscard]] consteval bool catalog_fields_nonempty_impl(std::index_sequence<Is...>) noexcept {
    return ((!std::tuple_element_t<Is, Catalog<>>::description.empty()
             && !std::tuple_element_t<Is, Catalog<>>::remediation.empty()
             && !std::tuple_element_t<Is, Catalog<>>::name.empty())
            && ...);
}

[[nodiscard]] consteval bool catalog_fields_nonempty() noexcept {
    return catalog_fields_nonempty_impl(std::make_index_sequence<catalog_size>{});
}

static_assert(catalog_fields_nonempty(), "One or more tags in Catalog has empty name/description/"
                                         "remediation.  Every tag must carry user-readable prose for all "
                                         "three fields — diagnostic output without remediation guidance "
                                         "is half a diagnostic.");

using d1_t = Diagnostic<EffectRowMismatch, int, float>;
using d2_t = Diagnostic<HotPathViolation>;

static_assert(is_diagnostic_v<d1_t>);
static_assert(is_diagnostic_v<d2_t>);
static_assert(!is_diagnostic_v<EffectRowMismatch>);
static_assert(!is_diagnostic_v<int>);

static_assert(std::is_same_v<typename d1_t::diagnostic_class, EffectRowMismatch>);
static_assert(std::is_same_v<typename d1_t::context, std::tuple<int, float>>);
static_assert(std::is_same_v<typename d2_t::context, std::tuple<>>);

static_assert(d1_t::name == "EffectRowMismatch");
static_assert(d2_t::name == "HotPathViolation");

CRUCIBLE_DIAG_ASSERT(true, EffectRowMismatch, "Self-test happy path: condition is true, macro compiles silently.");

CRUCIBLE_DIAG_ASSERT((std::is_same_v<int, int>), HotPathViolation,
                     "Comma in condition protected by parentheses; preprocessor "
                     "passes the entire is_same_v expression to static_assert.");

static_assert(categories_v.size() == catalog_size, "categories_v cardinality drifted from catalog_size — both must "
                                                   "track the same source of truth.");
static_assert(categories_v[0] == Category::EffectRowMismatch);
static_assert(categories_v[catalog_size - 1] == category_of_v<std::tuple_element_t<catalog_size - 1, Catalog<>>>);

template <std::size_t... Is>
[[nodiscard]] consteval bool categories_array_matches_enum_impl(std::index_sequence<Is...>) noexcept {
    return ((categories_v[Is] == static_cast<Category>(Is)) && ...);
}

[[nodiscard]] consteval bool categories_array_matches_enum() noexcept {
    return categories_array_matches_enum_impl(std::make_index_sequence<catalog_size>{});
}

static_assert(categories_array_matches_enum(), "categories_v ordering drifted from Category enum integer values. "
                                               "categories_v[I] must equal static_cast<Category>(I) for every "
                                               "in-range I — this is the runtime mirror of catalog_category_"
                                               "bijection.");

[[nodiscard]] consteval std::size_t enumerate_categories_count() noexcept {
    std::size_t count = 0;
    enumerate_categories([&count]<Category /*C*/>() noexcept { ++count; });
    return count;
}

static_assert(enumerate_categories_count() == catalog_size,
              "enumerate_categories<F> did not invoke F for every Category value. "
              "Likely cause: index_sequence dispatch broken or fold expression "
              "regression.");

static_assert(std::is_same_v<decltype(mint_diagnostic<EffectRowMismatch>(int{}, float{})),
                             Diagnostic<EffectRowMismatch, int, float>>);

static_assert(std::is_same_v<decltype(mint_diagnostic<HotPathViolation>()), Diagnostic<HotPathViolation>>);

}  // namespace foundation::diag::detail::diag_self_test
