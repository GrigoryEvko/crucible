#pragma once

#include <crucible/Platform.h>
#include <crucible/concurrent/ParallelismRule.h>

#include <type_traits>

namespace crucible::safety {

struct LocalityIgnore_t {};
struct LocalityLocal_t {};
struct LocalitySpread_t {};

template <typename Tag>
concept HasLocalityHint = requires {
    typename Tag::locality_hint;
    requires std::is_same_v<typename Tag::locality_hint, LocalityIgnore_t>
                 || std::is_same_v<typename Tag::locality_hint, LocalityLocal_t>
                 || std::is_same_v<typename Tag::locality_hint, LocalitySpread_t>;
};

// A Tag with no hint yields NumaIgnore, which leaves the parallelism rule's
// own choice of policy in place.

namespace detail {

template <typename Tag>
[[nodiscard]] consteval ::crucible::concurrent::NumaPolicy locality_hint_of_impl() noexcept {
    if constexpr (HasLocalityHint<Tag>) {
        using H = typename Tag::locality_hint;
        if constexpr (std::is_same_v<H, LocalityLocal_t>) {
            return ::crucible::concurrent::NumaPolicy::NumaLocal;
        } else if constexpr (std::is_same_v<H, LocalitySpread_t>) {
            return ::crucible::concurrent::NumaPolicy::NumaSpread;
        } else {
            // HasLocalityHint admits exactly three types, so this branch is
            // LocalityIgnore_t rather than an unconstrained fallback.
            return ::crucible::concurrent::NumaPolicy::NumaIgnore;
        }
    } else {
        return ::crucible::concurrent::NumaPolicy::NumaIgnore;
    }
}

}  // namespace detail

template <typename Tag>
inline constexpr ::crucible::concurrent::NumaPolicy locality_hint_of_v = detail::locality_hint_of_impl<Tag>();

template <typename Tag>
[[nodiscard]] inline ::crucible::concurrent::ParallelismDecision
recommend_parallelism_with_locality(::crucible::concurrent::WorkBudget budget) noexcept {
    auto dec = ::crucible::concurrent::recommend_parallelism(budget);
    if constexpr (HasLocalityHint<Tag>) {
        // A sequential decision has no workers to place, so the hint applies
        // only to the parallel case.
        if (dec.kind == ::crucible::concurrent::ParallelismDecision::Kind::Parallel) {
            dec.numa = locality_hint_of_v<Tag>;
        }
    }
    return dec;
}

namespace detail::locality_hint_self_test {

struct unpinned_tag {};
static_assert(!HasLocalityHint<unpinned_tag>);
static_assert(locality_hint_of_v<unpinned_tag> == ::crucible::concurrent::NumaPolicy::NumaIgnore);

struct local_tag {
    using locality_hint = LocalityLocal_t;
};
static_assert(HasLocalityHint<local_tag>);
static_assert(locality_hint_of_v<local_tag> == ::crucible::concurrent::NumaPolicy::NumaLocal);

struct spread_tag {
    using locality_hint = LocalitySpread_t;
};
static_assert(HasLocalityHint<spread_tag>);
static_assert(locality_hint_of_v<spread_tag> == ::crucible::concurrent::NumaPolicy::NumaSpread);

struct ignore_tag {
    using locality_hint = LocalityIgnore_t;
};
static_assert(HasLocalityHint<ignore_tag>);
static_assert(locality_hint_of_v<ignore_tag> == ::crucible::concurrent::NumaPolicy::NumaIgnore);

struct typo_tag {
    using locality_hint = int;
};
static_assert(!HasLocalityHint<typo_tag>);
static_assert(locality_hint_of_v<typo_tag> == ::crucible::concurrent::NumaPolicy::NumaIgnore);

static_assert(std::is_empty_v<LocalityIgnore_t>);
static_assert(std::is_empty_v<LocalityLocal_t>);
static_assert(std::is_empty_v<LocalitySpread_t>);

}  // namespace detail::locality_hint_self_test

}  // namespace crucible::safety
