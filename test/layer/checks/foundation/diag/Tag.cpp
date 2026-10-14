// The compile-time checks of foundation/diag/Tag.h.

#include <foundation/diag/Tag.h>

namespace foundation::diag::detail::tag_self_test {

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

CRUCIBLE_DIAG_ASSERT(true, EffectRowMismatch, "Self-test happy path: condition is true, macro compiles silently.");

CRUCIBLE_DIAG_ASSERT((std::is_same_v<int, int>), HotPathViolation,
                     "Comma in condition protected by parentheses; preprocessor "
                     "passes the entire is_same_v expression to static_assert.");

static_assert(categories_v.size() == catalog_size, "categories_v cardinality drifted from catalog_size — both must "
                                                   "track the same source of truth.");
static_assert(categories_v[0] == Category::EffectRowMismatch);

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

}  // namespace foundation::diag::detail::tag_self_test
