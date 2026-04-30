#pragma once

// FOUND-D29 — wrapper-detection predicate for `AllocClass<Tag, T>`.

#include <crucible/safety/AllocClass.h>

#include <type_traits>

namespace crucible::safety::extract {

using ::crucible::safety::AllocClassTag_v;

namespace detail {

template <typename T>
struct is_alloc_class_impl : std::false_type {
    using value_type = void;
};

template <AllocClassTag_v Tag, typename U>
struct is_alloc_class_impl<::crucible::safety::AllocClass<Tag, U>>
    : std::true_type
{
    using value_type = U;
    static constexpr AllocClassTag_v alloc_tag = Tag;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_alloc_class_v =
    detail::is_alloc_class_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsAllocClass = is_alloc_class_v<T>;

template <typename T>
    requires is_alloc_class_v<T>
using alloc_class_value_t =
    typename detail::is_alloc_class_impl<std::remove_cvref_t<T>>::value_type;

template <typename T>
    requires is_alloc_class_v<T>
inline constexpr AllocClassTag_v alloc_class_tag_v =
    detail::is_alloc_class_impl<std::remove_cvref_t<T>>::alloc_tag;

namespace detail::is_alloc_class_self_test {

using AC_int_stack = ::crucible::safety::AllocClass<AllocClassTag_v::Stack, int>;
using AC_int_pool  = ::crucible::safety::AllocClass<AllocClassTag_v::Pool,  int>;
using AC_int_arena = ::crucible::safety::AllocClass<AllocClassTag_v::Arena, int>;
using AC_int_heap  = ::crucible::safety::AllocClass<AllocClassTag_v::Heap,  int>;
using AC_int_mmap  = ::crucible::safety::AllocClass<AllocClassTag_v::Mmap,  int>;
using AC_int_huge  = ::crucible::safety::AllocClass<AllocClassTag_v::HugePage, int>;
using AC_dbl_arena = ::crucible::safety::AllocClass<AllocClassTag_v::Arena, double>;

static_assert(is_alloc_class_v<AC_int_stack>);
static_assert(is_alloc_class_v<AC_int_pool>);
static_assert(is_alloc_class_v<AC_int_arena>);
static_assert(is_alloc_class_v<AC_int_heap>);
static_assert(is_alloc_class_v<AC_int_mmap>);
static_assert(is_alloc_class_v<AC_int_huge>);
static_assert(is_alloc_class_v<AC_dbl_arena>);
static_assert(is_alloc_class_v<AC_int_arena&>);
static_assert(is_alloc_class_v<AC_int_arena const&>);

static_assert(!is_alloc_class_v<int>);
static_assert(!is_alloc_class_v<int*>);
static_assert(!is_alloc_class_v<AC_int_arena*>);

struct LookalikeAllocClass { int v; AllocClassTag_v t; };
static_assert(!is_alloc_class_v<LookalikeAllocClass>);

static_assert(IsAllocClass<AC_int_arena>);
static_assert(!IsAllocClass<int>);

static_assert(std::is_same_v<alloc_class_value_t<AC_int_arena>, int>);
static_assert(std::is_same_v<alloc_class_value_t<AC_dbl_arena>, double>);
static_assert(alloc_class_tag_v<AC_int_stack> == AllocClassTag_v::Stack);
static_assert(alloc_class_tag_v<AC_int_arena> == AllocClassTag_v::Arena);
static_assert(alloc_class_tag_v<AC_int_huge>  == AllocClassTag_v::HugePage);

}  // namespace detail::is_alloc_class_self_test

inline bool is_alloc_class_smoke_test() noexcept {
    using namespace detail::is_alloc_class_self_test;
    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_alloc_class_v<AC_int_arena>;
        ok = ok && !is_alloc_class_v<int>;
        ok = ok && IsAllocClass<AC_int_arena&&>;
        ok = ok && (alloc_class_tag_v<AC_int_huge> == AllocClassTag_v::HugePage);
    }
    return ok;
}

}  // namespace crucible::safety::extract
