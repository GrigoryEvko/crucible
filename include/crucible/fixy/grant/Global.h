#pragma once

// ── crucible::fixy::grant::global — global-state grant family (FIXY-V-246) ─
//
// Grants that engage `DimensionAxis::GlobalState` (substrate ordinal 27,
// V-238).  Each names a distinct piece of mutable global / thread-local
// state with a unique phantom tag, so the static-init-order fiasco (S004
// cycle detection) and the untagged-TLS hazard (G001) are auditable.
//
//   - singleton<GlobalTag>     — a Meyers singleton.  GlobalTag is a
//                                unique phantom tag (closes Scenario D:
//                                CKernel.h, warden/Registry.h,
//                                perf/detail/BpfLoader.h).  S004 walks the
//                                registered tags for an init cycle.
//   - thread_local_<TLSTag>    — a thread_local datum.  TLSTag mandatory
//                                (no default — G001 rejects untagged TLS).
//                                Closes Scenario B (vessel schema_cache).
//   - namespace_static<StaticTag> — a namespace-scope static datum.
//   - atexit_handler           — marker for a registered atexit handler.
//
// Tags are mandatory (no defaults): each global gets a unique identity so
// two distinct globals are distinct grant types.  Zero runtime cost.
// Greenfield; CR-09 which_dim lives in `namespace crucible::fixy::grant`.

#include <crucible/fixy/Grant.h>
#include <crucible/fixy/Dim.h>

#include <type_traits>

namespace crucible::fixy::grant::global {

// ─── singleton<GlobalTag> — Meyers singleton (S004 cycle detection) ───
template <class GlobalTag>
struct singleton final : grant_base {};

// ─── thread_local_<TLSTag> — thread_local datum (G001; tag mandatory) ─
template <class TLSTag>
struct thread_local_ final : grant_base {};

// ─── namespace_static<StaticTag> — namespace-scope static datum ───────
template <class StaticTag>
struct namespace_static final : grant_base {};

// ─── atexit_handler — registered atexit handler marker ────────────────
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
struct which_dim<global::atexit_handler>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::GlobalState> {};

// "I accept the strict default (no mutable global state) for this binding."
using accept_default_strict_for_GlobalState =
    accept_default_strict_for<dim::DimensionAxis::GlobalState>;

}  // namespace crucible::fixy::grant

namespace crucible::fixy::grant::detail::global_grant_self_test {

using D = dim::DimensionAxis;

struct sample_tag       final {};
struct other_tag        final {};

static_assert(IsGrantTag<global::singleton<sample_tag>>);
static_assert(IsGrantTag<global::thread_local_<sample_tag>>);
static_assert(IsGrantTag<global::namespace_static<sample_tag>>);
static_assert(IsGrantTag<global::atexit_handler>);

static_assert(sizeof(global::singleton<sample_tag>)       == 1);
static_assert(sizeof(global::thread_local_<sample_tag>)   == 1);
static_assert(sizeof(global::namespace_static<sample_tag>) == 1);
static_assert(sizeof(global::atexit_handler)              == 1);

static_assert(which_dim_v<global::singleton<sample_tag>>       == D::GlobalState);
static_assert(which_dim_v<global::thread_local_<sample_tag>>   == D::GlobalState);
static_assert(which_dim_v<global::namespace_static<sample_tag>> == D::GlobalState);
static_assert(which_dim_v<global::atexit_handler>              == D::GlobalState);
static_assert(which_dim_v<accept_default_strict_for_GlobalState> == D::GlobalState);

// Each global's tag carries identity; the four grant kinds are distinct.
static_assert(!std::is_same_v<global::singleton<sample_tag>, global::singleton<other_tag>>);
static_assert(!std::is_same_v<global::singleton<sample_tag>, global::thread_local_<sample_tag>>);
static_assert(!std::is_same_v<global::thread_local_<sample_tag>,
                              global::namespace_static<sample_tag>>);

}  // namespace crucible::fixy::grant::detail::global_grant_self_test
