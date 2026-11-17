// The compile-time checks of fixy/Reductions.h.

#include <fixy/Reductions.h>

#include <type_traits>

namespace fixy::detail::reductions_checks {

// Each fixy type is the foundation entity, not a second type.
static_assert(std::is_same_v<::fixy::TreeStep, ::foundation::core::TreeStep>);
static_assert(std::is_same_v<::fixy::PlanNode, ::foundation::core::PlanNode>);
static_assert(std::is_same_v<::fixy::PlanError, ::foundation::core::PlanError>);
static_assert(std::is_same_v<::fixy::RunsError, ::foundation::core::RunsError>);
static_assert(std::is_same_v<::fixy::SegmentError, ::foundation::core::SegmentError>);
static_assert(std::is_same_v<::fixy::ScanKind, ::foundation::core::ScanKind>);
static_assert(std::is_same_v<::fixy::Comparator, ::foundation::core::Comparator>);
static_assert(std::is_same_v<::fixy::Ascending, ::foundation::core::Ascending>);
static_assert(std::is_same_v<::fixy::KeyDirection, ::foundation::core::KeyDirection>);
static_assert(std::is_same_v<decltype(::fixy::order_by_key(&::fixy::TreeStep::left)),
                             ::foundation::core::KeyOrder<decltype(&::fixy::TreeStep::left),
                                                          ::foundation::core::KeyDirection::ascending>>);

// Two addresses are of one entity when the two constants of them are one type.
template <auto First, auto Second>
inline constexpr bool is_one_entity =
    std::is_same_v<std::integral_constant<decltype(First), First>, std::integral_constant<decltype(Second), Second>>;

// Each fixy function and variable is the foundation entity.
static_assert(is_one_entity<&::fixy::count_tree_depth, &::foundation::core::count_tree_depth>);
static_assert(is_one_entity<&::fixy::count_inclusive_scan_depth, &::foundation::core::count_inclusive_scan_depth>);
static_assert(is_one_entity<&::fixy::count_exclusive_scan_depth, &::foundation::core::count_exclusive_scan_depth>);
static_assert(is_one_entity<&::fixy::count_tree_grains<4>, &::foundation::core::count_tree_grains<4>>);
static_assert(is_one_entity<&::fixy::sort_network_size<4>, &::foundation::core::sort_network_size<4>>);
static_assert(is_one_entity<&::fixy::stable_sort_radix_count, &::foundation::core::stable_sort_radix_count>);
static_assert(is_one_entity<&::fixy::max_plan_leaves, &::foundation::core::max_plan_leaves>);
static_assert(is_one_entity<&::fixy::empty_plan_node, &::foundation::core::empty_plan_node>);
static_assert(!is_one_entity<&::fixy::count_inclusive_scan_depth, &::fixy::count_exclusive_scan_depth>);

// The fixy names give the values of the library.
static_assert(::fixy::count_tree_depth(5) == 3 && ::fixy::count_tree_grains<4>(9) == 3);
static_assert(::fixy::encode_total_order_key(-0.0f) < ::fixy::encode_total_order_key(0.0f));
static_assert(::fixy::plan_tree<3>().size() == 2);
static_assert(::fixy::plan_sort_network<4>().size() == ::fixy::sort_network_size<4>);

// The fixy concepts are the foundation concepts.
static_assert(::fixy::TreeSumElement<float> && !::fixy::TreeSumElement<int>);
static_assert(::fixy::TreeGrain<64> && !::fixy::TreeGrain<48>);
static_assert(::fixy::NetworkCount<2> && !::fixy::NetworkCount<1>);
static_assert(::fixy::AscendingElement<int> && !::fixy::AscendingElement<bool>);

}  // namespace fixy::detail::reductions_checks
