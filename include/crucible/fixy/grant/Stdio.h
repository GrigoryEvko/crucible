#pragma once

#include <crucible/fixy/Grant.h>
#include <crucible/fixy/Dim.h>

#include <type_traits>

namespace crucible::fixy::grant::stdio {

// <cstdio> defines lowercase stderr and stdout as object-like macros. The
// capitalized spellings are immune to that expansion.
namespace streams {
struct Stderr final {};
struct Stdout final {};
struct Debug final {};
}  // namespace streams

template <class Stream>
struct write final : grant_base {};

}  // namespace crucible::fixy::grant::stdio

namespace crucible::fixy::grant {

template <class Stream>
struct which_dim<stdio::write<Stream>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Stdio> {};

using accept_default_strict_for_Stdio = accept_default_strict_for<dim::DimensionAxis::Stdio>;

}  // namespace crucible::fixy::grant

namespace crucible::fixy::grant::detail::stdio_grant_self_test {

using D = dim::DimensionAxis;
namespace st = stdio::streams;

static_assert(IsGrantTag<stdio::write<st::Stderr>>);
static_assert(IsGrantTag<stdio::write<st::Stdout>>);
static_assert(IsGrantTag<stdio::write<st::Debug>>);

static_assert(!IsGrantTag<st::Stderr>);
static_assert(!IsGrantTag<st::Stdout>);
static_assert(!IsGrantTag<st::Debug>);

static_assert(sizeof(stdio::write<st::Stderr>) == 1);

static_assert(which_dim_v<stdio::write<st::Stderr>> == D::Stdio);
static_assert(which_dim_v<stdio::write<st::Stdout>> == D::Stdio);
static_assert(which_dim_v<stdio::write<st::Debug>> == D::Stdio);
static_assert(which_dim_v<accept_default_strict_for_Stdio> == D::Stdio);

static_assert(!std::is_same_v<stdio::write<st::Stderr>, stdio::write<st::Stdout>>);
static_assert(!std::is_same_v<stdio::write<st::Stdout>, stdio::write<st::Debug>>);

}  // namespace crucible::fixy::grant::detail::stdio_grant_self_test
