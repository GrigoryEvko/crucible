#pragma once

// Harvests, from a function's parameter list, the set of region tags
// the function operates on.  A parameter that carries no tag
// contributes nothing.
//
// Two forms come out.  The raw one keeps declaration order, for a
// caller that wants the tag list to mirror the parameter list.  The
// canonical one is sorted, so two signatures whose tags differ only in
// order produce the same set.  Cache keys are built on the canonical
// form and would otherwise depend on parameter order.

#include <crucible/safety/_SignatureTraits.h>
#include <crucible/safety/_IsOwnedRegion.h>
#include <crucible/safety/IsPermission.h>

#include <crucible/permissions/_PermSet.h>

#include <type_traits>

namespace crucible::safety::extract {

namespace detail {

// The branch is an `if constexpr` chain rather than a set of
// concept-constrained specializations, which the compiler would have
// to disambiguate even though the predicates are mutually exclusive.
template <typename P, typename Acc>
[[nodiscard]] consteval auto accumulate_param_tag() noexcept {
    if constexpr (extract::is_owned_region_v<P>) {
        return std::type_identity<::crucible::safety::proto::perm_set_insert_t<Acc, extract::owned_region_tag_t<P>>>{};
    } else if constexpr (extract::is_permission_v<P>) {
        return std::type_identity<::crucible::safety::proto::perm_set_insert_t<Acc, extract::permission_tag_t<P>>>{};
    } else if constexpr (extract::is_shared_permission_v<P>) {
        return std::type_identity<
            ::crucible::safety::proto::perm_set_insert_t<Acc, extract::shared_permission_tag_t<P>>>{};
    } else {
        return std::type_identity<Acc>{};
    }
}

template <typename P, typename Acc>
using accumulate_param_tag_t = typename decltype(accumulate_param_tag<P, Acc>())::type;

// The fold carries an explicit AtEnd flag because specializing on the
// arity itself would require the arity to be a fixed value, and it
// differs from one function to the next.

template <auto FnPtr, std::size_t I, typename Acc, bool AtEnd>
struct infer_perm_tags_step;

template <auto FnPtr, std::size_t I, typename Acc>
struct infer_perm_tags_step<FnPtr, I, Acc, /*AtEnd=*/true> {
    using type = Acc;
};

template <auto FnPtr, std::size_t I, typename Acc>
struct infer_perm_tags_step<FnPtr, I, Acc, /*AtEnd=*/false> {
    using P_I = extract::param_type_t<FnPtr, I>;
    using NextAcc = accumulate_param_tag_t<P_I, Acc>;
    static constexpr std::size_t Next = I + 1;
    using type = typename infer_perm_tags_step<FnPtr, Next, NextAcc, (Next >= extract::arity_v<FnPtr>)>::type;
};

template <auto FnPtr>
using infer_perm_tags_raw = typename infer_perm_tags_step<FnPtr, 0, ::crucible::safety::proto::EmptyPermSet,
                                                          (0 >= extract::arity_v<FnPtr>)>::type;

}  // namespace detail

template <auto FnPtr>
using inferred_permission_tags_raw_t = detail::infer_perm_tags_raw<FnPtr>;

template <auto FnPtr>
using inferred_permission_tags_t =
    ::crucible::safety::proto::perm_set_canonicalize_t<inferred_permission_tags_raw_t<FnPtr>>;

template <auto FnPtr, typename Tag>
inline constexpr bool function_has_tag_v =
    ::crucible::safety::proto::perm_set_contains_v<inferred_permission_tags_t<FnPtr>, Tag>;

// Each distinct tag is one parallelizable axis.
template <auto FnPtr>
inline constexpr std::size_t inferred_permission_tags_count_v = inferred_permission_tags_t<FnPtr>::size;

template <auto FnPtr>
inline constexpr bool is_tag_free_function_v = inferred_permission_tags_count_v<FnPtr> == 0;

template <auto FnPtr>
concept IsTagFreeFunction = is_tag_free_function_v<FnPtr>;

// Only the tag-free side is exercised here.  Covering the positive
// cases would instantiate the production wrappers in every consumer of
// this header, so they are tested elsewhere.

namespace detail::infer_perm_tags_self_test {

inline void f_no_tags(int, double, char*) noexcept {}

static_assert(::crucible::safety::proto::perm_set_equal_v<inferred_permission_tags_t<&f_no_tags>,
                                                          ::crucible::safety::proto::EmptyPermSet>);

static_assert(is_tag_free_function_v<&f_no_tags>);
static_assert(IsTagFreeFunction<&f_no_tags>);
static_assert(inferred_permission_tags_count_v<&f_no_tags> == 0);

inline void f_nullary() noexcept {}
static_assert(is_tag_free_function_v<&f_nullary>);
static_assert(inferred_permission_tags_count_v<&f_nullary> == 0);

static_assert(::crucible::safety::proto::perm_set_equal_v<inferred_permission_tags_raw_t<&f_no_tags>,
                                                          ::crucible::safety::proto::EmptyPermSet>);

}  // namespace detail::infer_perm_tags_self_test

inline bool inferred_permission_tags_smoke_test() noexcept {
    using namespace detail::infer_perm_tags_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_tag_free_function_v<&f_no_tags>;
        ok = ok && is_tag_free_function_v<&f_nullary>;
        ok = ok && IsTagFreeFunction<&f_no_tags>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
