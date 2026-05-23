#pragma once

// ── crucible::fixy::grant::dispatch — call-shape grant family (FIXY-V-245) ─
//
// Four grant tags that engage `DimensionAxis::CallShape` (substrate
// ordinal 25, added by V-238).  Each grant declares HOW a function
// binding transfers control to a callee — through a function pointer,
// a virtual table, a bounded self-recursion, or a guaranteed tail call.
// Every tag routes to `DimensionAxis::CallShape` via `which_dim` so
// Reject.h's engagement walk treats this axis uniformly, and so the
// CollisionCatalog dispatch rules (D001 indirect-call-not-noexcept,
// D002 recurses-unbounded, V-243) can read the CallShape tier off the
// binding.
//
// ── The LOAD-BEARING grant: recurses<MaxDepth> ────────────────────────
//
// `recurses<MaxDepth>` carries the proven worst-case recursion depth as
// a mandatory NTTP — there is NO default, so a bare `recurses` (no
// bound) is ill-formed at the grant site, and the D002 composition rule
// rejects an unbounded recursion at the binding site.  Example:
// `MerkleDag::merkle_hash` walks a content-addressed DAG whose children
// are acyclic by construction; surfacing that implicit bound as
// `grant::dispatch::recurses<32>` makes the depth ceiling auditable
// instead of merely true-by-luck.
//
// ── indirect_call<FnPtrFamily> — closes Scenario A ────────────────────
//
// `indirect_call<FnPtrFamily>` tags an indirect (function-pointer) call,
// where FnPtrFamily is a stable per-callback-family tag.  The V-243 D001
// rule pairs it with the noexcept requirement (V-086 already added
// noexcept to `BackgroundThread::RegionReadyCallback::Fn`): a callback
// family whose RunFn type is not noexcept is rejected.
//
// ── Cost / greenfield ─────────────────────────────────────────────────
//
// Zero runtime cost — every tag is an empty `final` struct inheriting
// `grant_base` (sizeof == 1 standalone, EBO-collapses to 0 bytes).
// Unlike the ctrl family (which re-homed the V-087 throws pioneer), the
// dispatch family is greenfield: no prior tag to alias.
//
// Per the namespace-purity discipline (CR-09), all `which_dim`
// specializations live syntactically inside `namespace
// crucible::fixy::grant`.  This header reopens that namespace;
// `scripts/check-fixy-grant-namespace-purity.sh` allowlists it.  The
// `grant::dispatch` sub-namespace open is NOT the locked namespace.

#include <crucible/fixy/Grant.h>            // grant_base, which_dim, accept_default_strict_for
#include <crucible/fixy/Dim.h>              // dim::DimensionAxis::CallShape

#include <cstddef>
#include <type_traits>

// ═════════════════════════════════════════════════════════════════════
// ── The four grant tags (grant::dispatch) ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace crucible::fixy::grant::dispatch {

// ─── (a) indirect_call<FnPtrFamily> — function-pointer dispatch ───────
//
// FnPtrFamily is a stable tag identifying the callback family (so two
// distinct callback schemas are distinct grant types).  Pairs with the
// D001 noexcept requirement.
template <class FnPtrFamily>
struct indirect_call final : grant_base {};

// ─── (b) virtual_call<BaseClass> — virtual dispatch (FFI compat) ──────
//
// Banned today via `-fno-rtti`; the tag exists so an FFI island that
// must cross a vtable boundary can declare it.
template <class BaseClass>
struct virtual_call final : grant_base {};

// ─── (c) recurses<MaxDepth> — LOAD-BEARING; bound is mandatory ────────
//
// No default: a bare `recurses` is ill-formed.  MaxDepth is the proven
// worst-case self-recursion depth.
template <std::size_t MaxDepth>
struct recurses final : grant_base {};

// ─── (d) tail_call — guaranteed tail call ([[gnu::musttail]]) ─────────
struct tail_call final : grant_base {};

}  // namespace crucible::fixy::grant::dispatch

// ═════════════════════════════════════════════════════════════════════
// ── which_dim routing + engagement alias (grant — CR-09 locked) ───────
// ═════════════════════════════════════════════════════════════════════

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
struct which_dim<dispatch::tail_call>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::CallShape> {};

// ─── Mandatory engagement tag for the CallShape axis ──────────────────
//
// "I have read the CallShape discipline and accept the strict default
// (Direct) for this binding."  which_dim handled by the generic
// `accept_default_strict_for<D>` specialization in Grant.h.
using accept_default_strict_for_CallShape =
    accept_default_strict_for<dim::DimensionAxis::CallShape>;

}  // namespace crucible::fixy::grant

// ═════════════════════════════════════════════════════════════════════
// ── Surface integrity sentinels ──────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace crucible::fixy::grant::detail::dispatch_grant_self_test {

using D = dim::DimensionAxis;

struct sample_family final {};
struct sample_base   final {};

// ─── (1) IsGrantTag — every grant tag participates ────────────────────
static_assert(IsGrantTag<dispatch::indirect_call<sample_family>>);
static_assert(IsGrantTag<dispatch::virtual_call<sample_base>>);
static_assert(IsGrantTag<dispatch::recurses<32>>);
static_assert(IsGrantTag<dispatch::recurses<0>>);
static_assert(IsGrantTag<dispatch::tail_call>);

// ─── (2) sizeof — EBO-collapsible standalone marker (1 byte) ──────────
static_assert(sizeof(dispatch::indirect_call<sample_family>) == 1);
static_assert(sizeof(dispatch::virtual_call<sample_base>)    == 1);
static_assert(sizeof(dispatch::recurses<32>)                 == 1);
static_assert(sizeof(dispatch::tail_call)                    == 1);

// ─── (3) which_dim routing — every tag → CallShape ────────────────────
static_assert(which_dim_v<dispatch::indirect_call<sample_family>> == D::CallShape);
static_assert(which_dim_v<dispatch::virtual_call<sample_base>>    == D::CallShape);
static_assert(which_dim_v<dispatch::recurses<32>>                 == D::CallShape);
static_assert(which_dim_v<dispatch::recurses<1>>                  == D::CallShape);
static_assert(which_dim_v<dispatch::tail_call>                    == D::CallShape);
static_assert(which_dim_v<accept_default_strict_for_CallShape>    == D::CallShape);

// ─── (4) Distinctness — different grant kinds are different types ─────
static_assert(!std::is_same_v<dispatch::indirect_call<sample_family>,
                              dispatch::virtual_call<sample_base>>);
static_assert(!std::is_same_v<dispatch::recurses<32>, dispatch::tail_call>);
static_assert(!std::is_same_v<dispatch::recurses<32>, dispatch::recurses<16>>);  // bound carries identity
static_assert(std::is_same_v<dispatch::recurses<32>, dispatch::recurses<32>>);

// ─── (5) Family / base tags carry identity ────────────────────────────
struct other_family final {};
static_assert(!std::is_same_v<dispatch::indirect_call<sample_family>,
                              dispatch::indirect_call<other_family>>);

}  // namespace crucible::fixy::grant::detail::dispatch_grant_self_test
