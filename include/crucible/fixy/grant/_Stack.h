#pragma once

// The strict default on this axis is the frame budget the build enforces
// with -Wframe-larger-than=4096. A binding needs a grant here only once it
// exceeds that budget.

#include <crucible/fixy/_Grant.h>
#include <crucible/fixy/Dim.h>

#include <cstddef>
#include <type_traits>

namespace crucible::fixy::grant::stack {

template <std::size_t MaxBytes>
struct alloc final : grant_base {};

// -Werror=vla rejects a variable-length array whether or not the binding
// carries this grant. The tag records a reviewed exception, it does not
// re-enable the construct.
struct vla_ok final : grant_base {};
struct alloca_ok final : grant_base {};

}  // namespace crucible::fixy::grant::stack

namespace crucible::fixy::grant {

template <std::size_t MaxBytes>
struct which_dim<stack::alloc<MaxBytes>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::StackUse> {};

template <>
struct which_dim<stack::vla_ok> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::StackUse> {};

template <>
struct which_dim<stack::alloca_ok> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::StackUse> {};

using accept_default_strict_for_StackUse = accept_default_strict_for<dim::DimensionAxis::StackUse>;

}  // namespace crucible::fixy::grant

namespace crucible::fixy::grant::detail::stack_grant_self_test {

using D = dim::DimensionAxis;

static_assert(IsGrantTag<stack::alloc<64>>);
static_assert(IsGrantTag<stack::alloc<4096>>);
static_assert(IsGrantTag<stack::vla_ok>);
static_assert(IsGrantTag<stack::alloca_ok>);

static_assert(sizeof(stack::alloc<64>) == 1);
static_assert(sizeof(stack::vla_ok) == 1);
static_assert(sizeof(stack::alloca_ok) == 1);

static_assert(which_dim_v<stack::alloc<64>> == D::StackUse);
static_assert(which_dim_v<stack::alloc<4096>> == D::StackUse);
static_assert(which_dim_v<stack::vla_ok> == D::StackUse);
static_assert(which_dim_v<stack::alloca_ok> == D::StackUse);

// fix-36: StackUse ships grant tags, so it is not a grantless axis.
static_assert(audit::grant_family_witnessed_v<stack::alloca_ok>,
              "grant/Stack.h ships a grant family for StackUse, so StackUse must not "
              "appear in grant::kAxesWithoutNonDefaultGrants.");
static_assert(which_dim_v<accept_default_strict_for_StackUse> == D::StackUse);

static_assert(!std::is_same_v<stack::alloc<64>, stack::alloc<128>>);
static_assert(std::is_same_v<stack::alloc<64>, stack::alloc<64>>);
static_assert(!std::is_same_v<stack::vla_ok, stack::alloca_ok>);

}  // namespace crucible::fixy::grant::detail::stack_grant_self_test
