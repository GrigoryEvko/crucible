#pragma once

#include <crucible/safety/InferredPermissionTags.h>
#include <crucible/safety/InferredRow.h>
#include <crucible/safety/_IsOwnedRegion.h>

#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_EffectRow.h>

#include <cstddef>
#include <type_traits>

namespace crucible::fixy::wrap {

using ::crucible::safety::extract::inferred_row_t;
using ::crucible::safety::extract::inferred_row_count_v;
using ::crucible::safety::extract::function_has_effect_v;
using ::crucible::safety::extract::is_pure_function_v;
using ::crucible::safety::extract::IsPureFunction;

using ::crucible::safety::extract::inferred_permission_tags_raw_t;
using ::crucible::safety::extract::inferred_permission_tags_t;
using ::crucible::safety::extract::function_has_tag_v;
using ::crucible::safety::extract::inferred_permission_tags_count_v;
using ::crucible::safety::extract::is_tag_free_function_v;
using ::crucible::safety::extract::IsTagFreeFunction;

}  // namespace crucible::fixy::wrap

namespace crucible::fixy::wrap::self_test_inferred {

inline void f_pure(int, double) noexcept {}
inline void f_alloc(::crucible::effects::Alloc, std::size_t) noexcept {}
inline void f_bg(::crucible::effects::Bg, int) noexcept {}
inline void f_alloc_io(::crucible::effects::Alloc, ::crucible::effects::IO, int) noexcept {}
inline void f_alloc_dup(::crucible::effects::Alloc, ::crucible::effects::Alloc, int) noexcept {}

inline void f_no_tags(int, double, char*) noexcept {}
inline void f_nullary() noexcept {}

static_assert(std::is_same_v<::crucible::fixy::wrap::inferred_row_t<&f_pure>,
                             ::crucible::safety::extract::inferred_row_t<&f_pure>>);
static_assert(std::is_same_v<::crucible::fixy::wrap::inferred_row_t<&f_pure>, ::crucible::effects::EmptyRow>);
static_assert(std::is_same_v<::crucible::fixy::wrap::inferred_row_t<&f_alloc>,
                             ::crucible::effects::Row<::crucible::effects::Effect::Alloc>>);
static_assert(
    std::is_same_v<::crucible::fixy::wrap::inferred_row_t<&f_alloc_io>,
                   ::crucible::effects::Row<::crucible::effects::Effect::Alloc, ::crucible::effects::Effect::IO>>);

static_assert(::crucible::fixy::wrap::inferred_row_count_v<&f_pure>
              == ::crucible::safety::extract::inferred_row_count_v<&f_pure>);
static_assert(::crucible::fixy::wrap::inferred_row_count_v<&f_pure> == 0);
static_assert(::crucible::fixy::wrap::inferred_row_count_v<&f_alloc> == 1);
static_assert(::crucible::fixy::wrap::inferred_row_count_v<&f_alloc_io> == 2);
static_assert(::crucible::fixy::wrap::inferred_row_count_v<&f_alloc_dup> == 1);

static_assert(::crucible::fixy::wrap::function_has_effect_v<&f_alloc, ::crucible::effects::Effect::Alloc>
              == ::crucible::safety::extract::function_has_effect_v<&f_alloc, ::crucible::effects::Effect::Alloc>);
static_assert(::crucible::fixy::wrap::function_has_effect_v<&f_alloc, ::crucible::effects::Effect::Alloc>);
static_assert(!::crucible::fixy::wrap::function_has_effect_v<&f_alloc, ::crucible::effects::Effect::IO>);
static_assert(::crucible::fixy::wrap::function_has_effect_v<&f_alloc_io, ::crucible::effects::Effect::IO>);
static_assert(::crucible::fixy::wrap::function_has_effect_v<&f_bg, ::crucible::effects::Effect::Bg>);
static_assert(!::crucible::fixy::wrap::function_has_effect_v<&f_bg, ::crucible::effects::Effect::Alloc>);

static_assert(::crucible::fixy::wrap::is_pure_function_v<&f_pure>);
static_assert(!::crucible::fixy::wrap::is_pure_function_v<&f_alloc>);
static_assert(!::crucible::fixy::wrap::is_pure_function_v<&f_bg>);

static_assert(::crucible::fixy::wrap::IsPureFunction<&f_pure>);
static_assert(!::crucible::fixy::wrap::IsPureFunction<&f_alloc>);

static_assert(::crucible::fixy::wrap::IsPureFunction<&f_pure> == ::crucible::safety::extract::IsPureFunction<&f_pure>);

static_assert(std::is_same_v<::crucible::fixy::wrap::inferred_permission_tags_t<&f_no_tags>,
                             ::crucible::safety::extract::inferred_permission_tags_t<&f_no_tags>>);
static_assert(std::is_same_v<::crucible::fixy::wrap::inferred_permission_tags_t<&f_no_tags>,
                             ::crucible::safety::proto::EmptyPermSet>);
static_assert(std::is_same_v<::crucible::fixy::wrap::inferred_permission_tags_raw_t<&f_no_tags>,
                             ::crucible::safety::proto::EmptyPermSet>);

static_assert(::crucible::fixy::wrap::inferred_permission_tags_count_v<&f_no_tags>
              == ::crucible::safety::extract::inferred_permission_tags_count_v<&f_no_tags>);
static_assert(::crucible::fixy::wrap::inferred_permission_tags_count_v<&f_no_tags> == 0);
static_assert(::crucible::fixy::wrap::inferred_permission_tags_count_v<&f_nullary> == 0);
static_assert(::crucible::fixy::wrap::inferred_permission_tags_count_v<&f_alloc> == 0);

static_assert(::crucible::fixy::wrap::is_tag_free_function_v<&f_no_tags>
              == ::crucible::safety::extract::is_tag_free_function_v<&f_no_tags>);
static_assert(::crucible::fixy::wrap::is_tag_free_function_v<&f_no_tags>);
static_assert(::crucible::fixy::wrap::is_tag_free_function_v<&f_nullary>);
static_assert(::crucible::fixy::wrap::is_tag_free_function_v<&f_alloc>);

static_assert(::crucible::fixy::wrap::IsTagFreeFunction<&f_no_tags>);
static_assert(::crucible::fixy::wrap::IsTagFreeFunction<&f_no_tags>
              == ::crucible::safety::extract::IsTagFreeFunction<&f_no_tags>);

// An effect-row harvest and a permission-tag harvest read disjoint parts
// of a parameter list. A cap-tagged parameter must not register as a
// permission tag.
static_assert(::crucible::fixy::wrap::is_tag_free_function_v<&f_alloc>);
static_assert(::crucible::fixy::wrap::is_tag_free_function_v<&f_bg>);
static_assert(::crucible::fixy::wrap::is_tag_free_function_v<&f_alloc_io>);

constexpr int inferred_alias_cardinality = 11;
static_assert(inferred_alias_cardinality == 11, "The re-exported surface has changed. Update the cardinality "
                                                "and add a sentinel for the new symbol.");

}  // namespace crucible::fixy::wrap::self_test_inferred
