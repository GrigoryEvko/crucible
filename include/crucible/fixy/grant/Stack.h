#pragma once

// ── crucible::fixy::grant::stack — stack-use grant family (FIXY-V-246) ──
//
// Stack-footprint grants that engage `DimensionAxis::StackUse` (substrate
// ordinal 26, V-238).  The axis strict default is "bounded stack ≤ 4 KB
// per function" (matches `-Wframe-larger-than=4096`); a binding that
// allocates a larger temporary, or uses a VLA / alloca, declares it here.
//
//   - alloc<MaxBytes>  — declares a stack temporary bounded to MaxBytes.
//                        MaxBytes is mandatory (no default): every
//                        explicit stack-budget grant states its ceiling.
//                        `grant::stack::alloc<64>` = ≤64 B temporaries.
//   - vla_ok           — marker for a documented VLA exception
//                        (banned by `-Werror=vla` regardless).
//   - alloca_ok        — marker for a documented alloca() exception.
//
// Zero runtime cost — every tag is an empty `final` struct inheriting
// `grant_base`.  Greenfield: no forward-pioneer to re-home.  Per CR-09
// the `which_dim` specializations live in `namespace
// crucible::fixy::grant` (allowlisted in the purity script).

#include <crucible/fixy/Grant.h>
#include <crucible/fixy/Dim.h>

#include <cstddef>
#include <type_traits>

namespace crucible::fixy::grant::stack {

// ─── alloc<MaxBytes> — bounded stack temporary (bound mandatory) ──────
template <std::size_t MaxBytes>
struct alloc final : grant_base {};

// ─── VLA / alloca exception markers ───────────────────────────────────
struct vla_ok    final : grant_base {};
struct alloca_ok final : grant_base {};

}  // namespace crucible::fixy::grant::stack

namespace crucible::fixy::grant {

template <std::size_t MaxBytes>
struct which_dim<stack::alloc<MaxBytes>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::StackUse> {};

template <>
struct which_dim<stack::vla_ok>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::StackUse> {};

template <>
struct which_dim<stack::alloca_ok>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::StackUse> {};

// "I accept the ≤4 KB strict default for this binding's stack footprint."
using accept_default_strict_for_StackUse =
    accept_default_strict_for<dim::DimensionAxis::StackUse>;

}  // namespace crucible::fixy::grant

namespace crucible::fixy::grant::detail::stack_grant_self_test {

using D = dim::DimensionAxis;

static_assert(IsGrantTag<stack::alloc<64>>);
static_assert(IsGrantTag<stack::alloc<4096>>);
static_assert(IsGrantTag<stack::vla_ok>);
static_assert(IsGrantTag<stack::alloca_ok>);

static_assert(sizeof(stack::alloc<64>) == 1);
static_assert(sizeof(stack::vla_ok)    == 1);
static_assert(sizeof(stack::alloca_ok) == 1);

static_assert(which_dim_v<stack::alloc<64>>    == D::StackUse);
static_assert(which_dim_v<stack::alloc<4096>>  == D::StackUse);
static_assert(which_dim_v<stack::vla_ok>       == D::StackUse);
static_assert(which_dim_v<stack::alloca_ok>    == D::StackUse);
static_assert(which_dim_v<accept_default_strict_for_StackUse> == D::StackUse);

static_assert(!std::is_same_v<stack::alloc<64>, stack::alloc<128>>);  // bound carries identity
static_assert(std::is_same_v<stack::alloc<64>, stack::alloc<64>>);
static_assert(!std::is_same_v<stack::vla_ok, stack::alloca_ok>);

}  // namespace crucible::fixy::grant::detail::stack_grant_self_test
