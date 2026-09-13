#pragma once

#include <crucible/permissions/Permission.h>

#include <type_traits>

namespace crucible::safety::extract {

template <typename T>
inline constexpr bool is_permission_v = ::crucible::safety::detail::is_permission_impl<std::remove_cvref_t<T>>::value;

template <typename T>
inline constexpr bool is_shared_permission_v =
    ::crucible::safety::detail::is_shared_permission_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsPermission = is_permission_v<T>;

template <typename T>
concept IsSharedPermission = is_shared_permission_v<T>;

template <typename T, typename Tag>
concept IsPermissionFor =
    is_permission_v<T>
    && std::is_same_v<typename ::crucible::safety::detail::is_permission_impl<std::remove_cvref_t<T>>::tag_type, Tag>;

template <typename T, typename Tag>
concept IsSharedPermissionFor =
    is_shared_permission_v<T>
    && std::is_same_v<typename ::crucible::safety::detail::is_shared_permission_impl<std::remove_cvref_t<T>>::tag_type,
                      Tag>;

// The extractors are constrained on the matching predicate rather than left to
// the primary template, which would hand back void for an unrelated argument
// and propagate it into whatever tries to round-trip the tag.

template <typename T>
    requires is_permission_v<T>
using permission_tag_t = typename ::crucible::safety::detail::is_permission_impl<std::remove_cvref_t<T>>::tag_type;

template <typename T>
    requires is_shared_permission_v<T>
using shared_permission_tag_t =
    typename ::crucible::safety::detail::is_shared_permission_impl<std::remove_cvref_t<T>>::tag_type;

namespace detail::is_permission_self_test {

struct test_tag_a {};
struct test_tag_b {};

using P_a = ::crucible::safety::Permission<test_tag_a>;
using P_b = ::crucible::safety::Permission<test_tag_b>;
using SP_a = ::crucible::safety::SharedPermission<test_tag_a>;
using SP_b = ::crucible::safety::SharedPermission<test_tag_b>;
using SG_a = ::crucible::safety::SharedPermissionGuard<test_tag_a>;

static_assert(is_permission_v<P_a>);
static_assert(is_permission_v<P_b>);

static_assert(is_permission_v<P_a&>);
static_assert(is_permission_v<P_a&&>);
static_assert(is_permission_v<P_a const>);
static_assert(is_permission_v<P_a const&>);
static_assert(is_permission_v<P_a const&&>);
static_assert(is_permission_v<P_a volatile>);
static_assert(is_permission_v<P_a const volatile>);

static_assert(!is_permission_v<int>);
static_assert(!is_permission_v<int*>);
static_assert(!is_permission_v<int&>);
static_assert(!is_permission_v<void>);
static_assert(!is_permission_v<test_tag_a>);

// The shared permission and its guard are separate surfaces: linear against
// fractional against a scope guard. Admitting them here would let a fractional
// handle reach a consumer that requires exclusive ownership.
static_assert(!is_permission_v<SP_a>);
static_assert(!is_permission_v<SG_a>);

static_assert(!is_permission_v<P_a*>);
static_assert(!is_permission_v<P_a* const>);
static_assert(!is_permission_v<P_a const*>);

static_assert(is_shared_permission_v<SP_a>);
static_assert(is_shared_permission_v<SP_b>);
static_assert(is_shared_permission_v<SP_a&>);
static_assert(is_shared_permission_v<SP_a&&>);
static_assert(is_shared_permission_v<SP_a const>);
static_assert(is_shared_permission_v<SP_a const&>);
static_assert(is_shared_permission_v<SP_a volatile>);

static_assert(!is_shared_permission_v<int>);
static_assert(!is_shared_permission_v<P_a>);
static_assert(!is_shared_permission_v<SG_a>);
static_assert(!is_shared_permission_v<test_tag_a>);
static_assert(!is_shared_permission_v<SP_a*>);

static_assert(IsPermission<P_a>);
static_assert(IsPermission<P_a&&>);
static_assert(IsPermission<P_a const&>);
static_assert(!IsPermission<int>);
static_assert(!IsPermission<SP_a>);

static_assert(IsSharedPermission<SP_a>);
static_assert(IsSharedPermission<SP_a&&>);
static_assert(!IsSharedPermission<int>);
static_assert(!IsSharedPermission<P_a>);

static_assert(std::is_same_v<permission_tag_t<P_a>, test_tag_a>);
static_assert(std::is_same_v<permission_tag_t<P_b>, test_tag_b>);

static_assert(std::is_same_v<shared_permission_tag_t<SP_a>, test_tag_a>);
static_assert(std::is_same_v<shared_permission_tag_t<SP_b>, test_tag_b>);

static_assert(std::is_same_v<permission_tag_t<P_a&>, test_tag_a>);
static_assert(std::is_same_v<permission_tag_t<P_a const&>, test_tag_a>);
static_assert(std::is_same_v<permission_tag_t<P_a&&>, test_tag_a>);
static_assert(std::is_same_v<permission_tag_t<P_a const&&>, test_tag_a>);

static_assert(std::is_same_v<shared_permission_tag_t<SP_a&>, test_tag_a>);
static_assert(std::is_same_v<shared_permission_tag_t<SP_a const&>, test_tag_a>);
static_assert(std::is_same_v<shared_permission_tag_t<SP_a&&>, test_tag_a>);

static_assert(!std::is_same_v<permission_tag_t<P_a>, permission_tag_t<P_b>>);
static_assert(!std::is_same_v<shared_permission_tag_t<SP_a>, shared_permission_tag_t<SP_b>>);

// The two wrappers project the same region tag. Separating them is the job of
// the two predicates above, not of the tag.
static_assert(std::is_same_v<permission_tag_t<P_a>, shared_permission_tag_t<SP_a>>);

static_assert(IsPermissionFor<P_a, test_tag_a>);
static_assert(IsPermissionFor<P_a const&, test_tag_a>);
static_assert(IsPermissionFor<P_a&&, test_tag_a>);
static_assert(!IsPermissionFor<P_a, test_tag_b>);
static_assert(!IsPermissionFor<P_b, test_tag_a>);
static_assert(!IsPermissionFor<int, test_tag_a>);
static_assert(!IsPermissionFor<SP_a, test_tag_a>);

static_assert(IsSharedPermissionFor<SP_a, test_tag_a>);
static_assert(IsSharedPermissionFor<SP_a const&, test_tag_a>);
static_assert(!IsSharedPermissionFor<SP_a, test_tag_b>);
static_assert(!IsSharedPermissionFor<P_a, test_tag_a>);

}  // namespace detail::is_permission_self_test

inline bool is_permission_smoke_test() noexcept {
    using namespace detail::is_permission_self_test;

    // The volatile bound defeats constant folding, so the trait reads survive
    // dead-code elimination.
    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_permission_v<P_a>;
        ok = ok && is_permission_v<P_a&&>;
        ok = ok && !is_permission_v<int>;
        ok = ok && !is_permission_v<SP_a>;

        ok = ok && is_shared_permission_v<SP_a>;
        ok = ok && !is_shared_permission_v<P_a>;
        ok = ok && !is_shared_permission_v<int>;

        ok = ok && IsPermission<P_a>;
        ok = ok && !IsPermission<SP_a>;
        ok = ok && IsSharedPermission<SP_a>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
