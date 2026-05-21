#pragma once

// ═══════════════════════════════════════════════════════════════════
// fixy::wrap::Inferred — V-044 surface
//
// Re-exports the two parameter-list-introspecting substrates that
// live in `crucible::safety::extract::`:
//
//   * crucible/safety/InferredRow.h          — Met(X) effect-row
//                                              harvest from cap-tag
//                                              parameters
//   * crucible/safety/InferredPermissionTags.h
//                                            — CSL permission-tag
//                                              harvest from
//                                              OwnedRegion /
//                                              Permission /
//                                              SharedPermission
//                                              parameters
//
// The pair co-evolves at the FOUND-I02 row-hash boundary: the
// dispatcher uses both inferred-row AND inferred-permission-tags
// for federation cache-key stability.  Combined umbrella mirrors
// V-041 (SimdWorkloadLocality.h) and V-043 (PipelineStageEndpoint.h)
// precedent — co-evolving substrates share one umbrella.
//
// Substrate doc-blocks: see the per-substrate headers.  Each ships
// header-internal static_asserts that THIS file triggers under the
// project's warnings-as-errors flags
// (feedback_header_only_static_assert_blind_spot.md).
//
// ─── Public surface (11 symbols) ────────────────────────────────────
//
//   InferredRow substrate (5):
//     inferred_row_t<FnPtr>                        (type alias)
//     inferred_row_count_v<FnPtr>                  (size_t)
//     function_has_effect_v<FnPtr, Effect>         (bool)
//     is_pure_function_v<FnPtr>                    (bool)
//     IsPureFunction<FnPtr>                        (concept)
//
//   InferredPermissionTags substrate (6):
//     inferred_permission_tags_raw_t<FnPtr>        (type alias)
//     inferred_permission_tags_t<FnPtr>            (type alias,
//                                                   canonicalized)
//     function_has_tag_v<FnPtr, Tag>               (bool)
//     inferred_permission_tags_count_v<FnPtr>      (size_t)
//     is_tag_free_function_v<FnPtr>                (bool)
//     IsTagFreeFunction<FnPtr>                     (concept)

#include <crucible/safety/InferredPermissionTags.h>
#include <crucible/safety/InferredRow.h>
#include <crucible/safety/IsOwnedRegion.h>

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

#include <cstddef>
#include <type_traits>

namespace crucible::fixy::wrap {

// ═══════════════════════════════════════════════════════════════════
// ── 1. InferredRow — Met(X) effect-row harvest (5) ───────────────
// ═══════════════════════════════════════════════════════════════════

using ::crucible::safety::extract::inferred_row_t;
using ::crucible::safety::extract::inferred_row_count_v;
using ::crucible::safety::extract::function_has_effect_v;
using ::crucible::safety::extract::is_pure_function_v;
using ::crucible::safety::extract::IsPureFunction;

// ═══════════════════════════════════════════════════════════════════
// ── 2. InferredPermissionTags — CSL tag harvest (6) ──────────────
// ═══════════════════════════════════════════════════════════════════

using ::crucible::safety::extract::inferred_permission_tags_raw_t;
using ::crucible::safety::extract::inferred_permission_tags_t;
using ::crucible::safety::extract::function_has_tag_v;
using ::crucible::safety::extract::inferred_permission_tags_count_v;
using ::crucible::safety::extract::is_tag_free_function_v;
using ::crucible::safety::extract::IsTagFreeFunction;

}  // namespace crucible::fixy::wrap

// ═══════════════════════════════════════════════════════════════════
// ── Dual-export sentinel — FIXY-V-044 ──────────────────────────────
// ═══════════════════════════════════════════════════════════════════
//
// Header-internal identity sentinels.  Same discipline as
// fixy/wrap/SimdWorkloadLocality.h (V-041), Checked.h (V-042),
// PipelineStageEndpoint.h (V-043).

namespace crucible::fixy::wrap::self_test_inferred {

// ── Synthetic probe functions ────────────────────────────────────

inline void f_pure(int, double) noexcept {}
inline void f_alloc(::crucible::effects::Alloc, std::size_t) noexcept {}
inline void f_bg(::crucible::effects::Bg, int) noexcept {}
inline void f_alloc_io(::crucible::effects::Alloc,
                       ::crucible::effects::IO,
                       int) noexcept {}
inline void f_alloc_dup(::crucible::effects::Alloc,
                        ::crucible::effects::Alloc,
                        int) noexcept {}

inline void f_no_tags(int, double, char*) noexcept {}
inline void f_nullary() noexcept {}

// ── 1. InferredRow — type-alias identity ─────────────────────────

static_assert(std::is_same_v<
    ::crucible::fixy::wrap::inferred_row_t<&f_pure>,
    ::crucible::safety::extract::inferred_row_t<&f_pure>>);
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::inferred_row_t<&f_pure>,
    ::crucible::effects::EmptyRow>);
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::inferred_row_t<&f_alloc>,
    ::crucible::effects::Row<::crucible::effects::Effect::Alloc>>);
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::inferred_row_t<&f_alloc_io>,
    ::crucible::effects::Row<::crucible::effects::Effect::Alloc,
                             ::crucible::effects::Effect::IO>>);

// ── 2. InferredRow — inferred_row_count_v ────────────────────────

static_assert(
    ::crucible::fixy::wrap::inferred_row_count_v<&f_pure> ==
    ::crucible::safety::extract::inferred_row_count_v<&f_pure>);
static_assert(::crucible::fixy::wrap::inferred_row_count_v<&f_pure>     == 0);
static_assert(::crucible::fixy::wrap::inferred_row_count_v<&f_alloc>    == 1);
static_assert(::crucible::fixy::wrap::inferred_row_count_v<&f_alloc_io> == 2);
// Duplicate cap collapsed via insert-unique → still 1.
static_assert(::crucible::fixy::wrap::inferred_row_count_v<&f_alloc_dup> == 1);

// ── 3. InferredRow — function_has_effect_v ───────────────────────

static_assert(
    ::crucible::fixy::wrap::function_has_effect_v<
        &f_alloc, ::crucible::effects::Effect::Alloc> ==
    ::crucible::safety::extract::function_has_effect_v<
        &f_alloc, ::crucible::effects::Effect::Alloc>);
static_assert( ::crucible::fixy::wrap::function_has_effect_v<
                  &f_alloc,    ::crucible::effects::Effect::Alloc>);
static_assert(!::crucible::fixy::wrap::function_has_effect_v<
                  &f_alloc,    ::crucible::effects::Effect::IO>);
static_assert( ::crucible::fixy::wrap::function_has_effect_v<
                  &f_alloc_io, ::crucible::effects::Effect::IO>);
static_assert( ::crucible::fixy::wrap::function_has_effect_v<
                  &f_bg,       ::crucible::effects::Effect::Bg>);
static_assert(!::crucible::fixy::wrap::function_has_effect_v<
                  &f_bg,       ::crucible::effects::Effect::Alloc>);

// ── 4. InferredRow — is_pure_function_v + IsPureFunction concept ─

static_assert( ::crucible::fixy::wrap::is_pure_function_v<&f_pure>);
static_assert(!::crucible::fixy::wrap::is_pure_function_v<&f_alloc>);
static_assert(!::crucible::fixy::wrap::is_pure_function_v<&f_bg>);

static_assert( ::crucible::fixy::wrap::IsPureFunction<&f_pure>);
static_assert(!::crucible::fixy::wrap::IsPureFunction<&f_alloc>);

// Cross-path agreement on admission.
static_assert(
    ::crucible::fixy::wrap::IsPureFunction<&f_pure> ==
    ::crucible::safety::extract::IsPureFunction<&f_pure>);

// ── 5. InferredPermissionTags — type-alias identity ──────────────

static_assert(std::is_same_v<
    ::crucible::fixy::wrap::inferred_permission_tags_t<&f_no_tags>,
    ::crucible::safety::extract::inferred_permission_tags_t<&f_no_tags>>);
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::inferred_permission_tags_t<&f_no_tags>,
    ::crucible::safety::proto::EmptyPermSet>);
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::inferred_permission_tags_raw_t<&f_no_tags>,
    ::crucible::safety::proto::EmptyPermSet>);

// ── 6. InferredPermissionTags — inferred_permission_tags_count_v ─

static_assert(
    ::crucible::fixy::wrap::inferred_permission_tags_count_v<&f_no_tags> ==
    ::crucible::safety::extract::inferred_permission_tags_count_v<&f_no_tags>);
static_assert(::crucible::fixy::wrap::inferred_permission_tags_count_v<&f_no_tags> == 0);
static_assert(::crucible::fixy::wrap::inferred_permission_tags_count_v<&f_nullary> == 0);
// f_alloc carries cap-tags but NO permission tags — count stays 0
// (the two harvests are orthogonal axes).
static_assert(::crucible::fixy::wrap::inferred_permission_tags_count_v<&f_alloc> == 0);

// ── 7. InferredPermissionTags — is_tag_free_function_v +
//     IsTagFreeFunction concept ─────────────────────────────────

static_assert(
    ::crucible::fixy::wrap::is_tag_free_function_v<&f_no_tags> ==
    ::crucible::safety::extract::is_tag_free_function_v<&f_no_tags>);
static_assert(::crucible::fixy::wrap::is_tag_free_function_v<&f_no_tags>);
static_assert(::crucible::fixy::wrap::is_tag_free_function_v<&f_nullary>);
// Cap-tagged-but-permission-free function is still tag-free.
static_assert(::crucible::fixy::wrap::is_tag_free_function_v<&f_alloc>);

static_assert(::crucible::fixy::wrap::IsTagFreeFunction<&f_no_tags>);
static_assert(
    ::crucible::fixy::wrap::IsTagFreeFunction<&f_no_tags> ==
    ::crucible::safety::extract::IsTagFreeFunction<&f_no_tags>);

// ── 8. Axes orthogonality — Inferred row and permission tags
//     never reach into each other's signals ────────────────────
//
// f_alloc has effect row {Alloc}, permission tags {}.
// f_no_tags has effect row {}, permission tags {}.
// f_bg has effect row {Bg}, permission tags {}.
// None of these should produce a non-empty permission-tag set.

static_assert(::crucible::fixy::wrap::is_tag_free_function_v<&f_alloc>);
static_assert(::crucible::fixy::wrap::is_tag_free_function_v<&f_bg>);
static_assert(::crucible::fixy::wrap::is_tag_free_function_v<&f_alloc_io>);

// ── Cardinality witness ──────────────────────────────────────────
//
// 11 surfaced using-declarations across 2 substrates:
//
//   InferredRow              (5) — inferred_row_t + count_v +
//                                  function_has_effect_v +
//                                  is_pure_function_v +
//                                  IsPureFunction concept
//   InferredPermissionTags   (6) — inferred_permission_tags_raw_t +
//                                  inferred_permission_tags_t +
//                                  function_has_tag_v +
//                                  inferred_permission_tags_count_v +
//                                  is_tag_free_function_v +
//                                  IsTagFreeFunction concept
//
// Future additions to either substrate MUST extend this block + bump
// the constant + add a sentinel above.

constexpr int inferred_alias_cardinality = 11;
static_assert(inferred_alias_cardinality == 11,
    "fixy::wrap::Inferred cardinality changed — update Inferred.h "
    "sentinel block to track the two parameter-introspection "
    "substrates' public surface.");

}  // namespace crucible::fixy::wrap::self_test_inferred
