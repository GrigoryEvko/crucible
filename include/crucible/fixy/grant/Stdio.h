#pragma once

// ── crucible::fixy::grant::stdio — stdio-write grant family (FIXY-V-246) ─
//
// Grants that engage `DimensionAxis::Stdio` (substrate ordinal 28, V-238).
// `write<Stream>` declares that a binding performs formatted stdio to a
// given stream.  The 50+ `std::fprintf(stderr, ...)` diagnostic sites map
// to `grant::stdio::write<streams::Stderr>`.  Hot-path code (TraceRing,
// Arena, KernelCache) MUST NOT carry this grant — the V-243 collision
// rule S001 (HotPath × Stdio ≥ BufferedWrite) rejects it.
//
//   Stream ∈ { streams::Stderr, streams::Stdout, streams::Debug }.
//
// The stream policy tags are capitalized deliberately: `<cstdio>` defines
// lowercase `stderr` / `stdout` as MACROS, so `streams::Stderr` /
// `streams::Stdout` dodge macro expansion.  Policy tags do NOT inherit
// grant_base (they are template arguments, not grants).  Zero runtime
// cost; greenfield; CR-09 which_dim in `namespace crucible::fixy::grant`.

#include <crucible/fixy/Grant.h>
#include <crucible/fixy/Dim.h>

#include <type_traits>

namespace crucible::fixy::grant::stdio {

// ─── Stream policy tags (template arguments — NOT grants) ─────────────
namespace streams {
struct Stderr final {};
struct Stdout final {};
struct Debug  final {};
}  // namespace streams

// ─── write<Stream> — formatted stdio to a stream ──────────────────────
template <class Stream>
struct write final : grant_base {};

}  // namespace crucible::fixy::grant::stdio

namespace crucible::fixy::grant {

template <class Stream>
struct which_dim<stdio::write<Stream>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Stdio> {};

// "I accept the strict default (no stdio) for this binding."
using accept_default_strict_for_Stdio =
    accept_default_strict_for<dim::DimensionAxis::Stdio>;

}  // namespace crucible::fixy::grant

namespace crucible::fixy::grant::detail::stdio_grant_self_test {

using D = dim::DimensionAxis;
namespace st = stdio::streams;

static_assert(IsGrantTag<stdio::write<st::Stderr>>);
static_assert(IsGrantTag<stdio::write<st::Stdout>>);
static_assert(IsGrantTag<stdio::write<st::Debug>>);

// Stream policy tags are NOT grants (a bare use in a pack is rejected).
static_assert(!IsGrantTag<st::Stderr>);
static_assert(!IsGrantTag<st::Stdout>);
static_assert(!IsGrantTag<st::Debug>);

static_assert(sizeof(stdio::write<st::Stderr>) == 1);

static_assert(which_dim_v<stdio::write<st::Stderr>> == D::Stdio);
static_assert(which_dim_v<stdio::write<st::Stdout>> == D::Stdio);
static_assert(which_dim_v<stdio::write<st::Debug>>  == D::Stdio);
static_assert(which_dim_v<accept_default_strict_for_Stdio> == D::Stdio);

// Each stream is a distinct grant type.
static_assert(!std::is_same_v<stdio::write<st::Stderr>, stdio::write<st::Stdout>>);
static_assert(!std::is_same_v<stdio::write<st::Stdout>, stdio::write<st::Debug>>);

}  // namespace crucible::fixy::grant::detail::stdio_grant_self_test
