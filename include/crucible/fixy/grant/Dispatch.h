#pragma once

#include <crucible/fixy/Grant.h>
#include <crucible/fixy/Dim.h>

#include <cstddef>
#include <type_traits>

namespace crucible::fixy::grant::dispatch {

template <class FnPtrFamily>
struct indirect_call final : grant_base {};

// No Crucible type dispatches virtually. This tag exists for an FFI
// boundary that has to cross a foreign vtable.
template <class BaseClass>
struct virtual_call final : grant_base {};

// MaxDepth is a proven worst-case self-recursion depth, not an estimate.
template <std::size_t MaxDepth>
struct recurses final : grant_base {};

struct tail_call final : grant_base {};

}  // namespace crucible::fixy::grant::dispatch

namespace crucible::fixy::grant {

template <class FnPtrFamily>
struct which_dim<dispatch::indirect_call<FnPtrFamily>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::CallShape> {};

template <class BaseClass>
struct which_dim<dispatch::virtual_call<BaseClass>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::CallShape> {};

template <std::size_t MaxDepth>
struct which_dim<dispatch::recurses<MaxDepth>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::CallShape> {};

template <>
struct which_dim<dispatch::tail_call> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::CallShape> {};

using accept_default_strict_for_CallShape = accept_default_strict_for<dim::DimensionAxis::CallShape>;

}  // namespace crucible::fixy::grant

namespace crucible::fixy::grant::detail::dispatch_grant_self_test {

using D = dim::DimensionAxis;

struct sample_family final {};
struct sample_base final {};

static_assert(IsGrantTag<dispatch::indirect_call<sample_family>>);
static_assert(IsGrantTag<dispatch::virtual_call<sample_base>>);
static_assert(IsGrantTag<dispatch::recurses<32>>);
static_assert(IsGrantTag<dispatch::recurses<0>>);
static_assert(IsGrantTag<dispatch::tail_call>);

static_assert(sizeof(dispatch::indirect_call<sample_family>) == 1);
static_assert(sizeof(dispatch::virtual_call<sample_base>) == 1);
static_assert(sizeof(dispatch::recurses<32>) == 1);
static_assert(sizeof(dispatch::tail_call) == 1);

static_assert(which_dim_v<dispatch::indirect_call<sample_family>> == D::CallShape);
static_assert(which_dim_v<dispatch::virtual_call<sample_base>> == D::CallShape);
static_assert(which_dim_v<dispatch::recurses<32>> == D::CallShape);
static_assert(which_dim_v<dispatch::recurses<1>> == D::CallShape);
static_assert(which_dim_v<dispatch::tail_call> == D::CallShape);
static_assert(which_dim_v<accept_default_strict_for_CallShape> == D::CallShape);

static_assert(!std::is_same_v<dispatch::indirect_call<sample_family>, dispatch::virtual_call<sample_base>>);
static_assert(!std::is_same_v<dispatch::recurses<32>, dispatch::tail_call>);
static_assert(!std::is_same_v<dispatch::recurses<32>, dispatch::recurses<16>>);
static_assert(std::is_same_v<dispatch::recurses<32>, dispatch::recurses<32>>);

struct other_family final {};
static_assert(!std::is_same_v<dispatch::indirect_call<sample_family>, dispatch::indirect_call<other_family>>);

}  // namespace crucible::fixy::grant::detail::dispatch_grant_self_test
