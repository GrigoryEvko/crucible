#pragma once

// Each distinct global needs its own tag type. Two globals that share one
// tag collapse to a single grant and become indistinguishable to any
// consumer that walks the grants of a binding.

#include <crucible/fixy/Grant.h>
#include <crucible/fixy/Dim.h>

#include <type_traits>

namespace crucible::fixy::grant::global {

template <class GlobalTag>
struct singleton final : grant_base {};

template <class TLSTag>
struct thread_local_ final : grant_base {};

template <class StaticTag>
struct namespace_static final : grant_base {};

struct atexit_handler final : grant_base {};

}  // namespace crucible::fixy::grant::global

namespace crucible::fixy::grant {

template <class GlobalTag>
struct which_dim<global::singleton<GlobalTag>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::GlobalState> {};

template <class TLSTag>
struct which_dim<global::thread_local_<TLSTag>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::GlobalState> {};

template <class StaticTag>
struct which_dim<global::namespace_static<StaticTag>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::GlobalState> {};

template <>
struct which_dim<global::atexit_handler> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::GlobalState> {
};

using accept_default_strict_for_GlobalState = accept_default_strict_for<dim::DimensionAxis::GlobalState>;

}  // namespace crucible::fixy::grant

namespace crucible::fixy::grant::detail::global_grant_self_test {

using D = dim::DimensionAxis;

struct sample_tag final {};
struct other_tag final {};

static_assert(IsGrantTag<global::singleton<sample_tag>>);
static_assert(IsGrantTag<global::thread_local_<sample_tag>>);
static_assert(IsGrantTag<global::namespace_static<sample_tag>>);
static_assert(IsGrantTag<global::atexit_handler>);

static_assert(sizeof(global::singleton<sample_tag>) == 1);
static_assert(sizeof(global::thread_local_<sample_tag>) == 1);
static_assert(sizeof(global::namespace_static<sample_tag>) == 1);
static_assert(sizeof(global::atexit_handler) == 1);

static_assert(which_dim_v<global::singleton<sample_tag>> == D::GlobalState);
static_assert(which_dim_v<global::thread_local_<sample_tag>> == D::GlobalState);
static_assert(which_dim_v<global::namespace_static<sample_tag>> == D::GlobalState);
static_assert(which_dim_v<global::atexit_handler> == D::GlobalState);

// fix-36: GlobalState ships grant tags, so it is not a grantless axis.
static_assert(audit::grant_family_witnessed_v<global::atexit_handler>,
              "grant/Global.h ships a grant family for GlobalState, so GlobalState "
              "must not appear in grant::kAxesWithoutNonDefaultGrants.");
static_assert(which_dim_v<accept_default_strict_for_GlobalState> == D::GlobalState);

static_assert(!std::is_same_v<global::singleton<sample_tag>, global::singleton<other_tag>>);
static_assert(!std::is_same_v<global::singleton<sample_tag>, global::thread_local_<sample_tag>>);
static_assert(!std::is_same_v<global::thread_local_<sample_tag>, global::namespace_static<sample_tag>>);

}  // namespace crucible::fixy::grant::detail::global_grant_self_test
