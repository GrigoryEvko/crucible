#pragma once

// FOUND-D27 — wrapper-detection predicate for `MemOrder<Tag, T>`.

#include <crucible/safety/MemOrder.h>

#include <type_traits>

namespace crucible::safety::extract {

using ::crucible::safety::MemOrderTag_v;

namespace detail {

template <typename T>
struct is_mem_order_impl : std::false_type {
    using value_type = void;
};

template <MemOrderTag_v Tag, typename U>
struct is_mem_order_impl<::crucible::safety::MemOrder<Tag, U>>
    : std::true_type
{
    using value_type = U;
    static constexpr MemOrderTag_v tag = Tag;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_mem_order_v =
    detail::is_mem_order_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsMemOrder = is_mem_order_v<T>;

template <typename T>
    requires is_mem_order_v<T>
using mem_order_value_t =
    typename detail::is_mem_order_impl<std::remove_cvref_t<T>>::value_type;

template <typename T>
    requires is_mem_order_v<T>
inline constexpr MemOrderTag_v mem_order_tag_v =
    detail::is_mem_order_impl<std::remove_cvref_t<T>>::tag;

namespace detail::is_mem_order_self_test {

using MO_int_relaxed = ::crucible::safety::MemOrder<MemOrderTag_v::Relaxed, int>;
using MO_int_seqcst  = ::crucible::safety::MemOrder<MemOrderTag_v::SeqCst,  int>;
using MO_dbl_acqrel  = ::crucible::safety::MemOrder<MemOrderTag_v::AcqRel,  double>;

static_assert(is_mem_order_v<MO_int_relaxed>);
static_assert(is_mem_order_v<MO_int_seqcst>);
static_assert(is_mem_order_v<MO_dbl_acqrel>);
static_assert(is_mem_order_v<MO_int_relaxed&>);
static_assert(is_mem_order_v<MO_int_relaxed const&>);

static_assert(!is_mem_order_v<int>);
static_assert(!is_mem_order_v<int*>);
static_assert(!is_mem_order_v<MO_int_relaxed*>);

struct LookalikeMemOrder { int v; MemOrderTag_v t; };
static_assert(!is_mem_order_v<LookalikeMemOrder>);

static_assert(IsMemOrder<MO_int_relaxed>);
static_assert(!IsMemOrder<int>);

static_assert(std::is_same_v<mem_order_value_t<MO_int_relaxed>, int>);
static_assert(std::is_same_v<mem_order_value_t<MO_dbl_acqrel>, double>);
static_assert(mem_order_tag_v<MO_int_relaxed> == MemOrderTag_v::Relaxed);
static_assert(mem_order_tag_v<MO_int_seqcst>  == MemOrderTag_v::SeqCst);

}  // namespace detail::is_mem_order_self_test

inline bool is_mem_order_smoke_test() noexcept {
    using namespace detail::is_mem_order_self_test;
    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_mem_order_v<MO_int_relaxed>;
        ok = ok && !is_mem_order_v<int>;
        ok = ok && IsMemOrder<MO_int_relaxed&&>;
        ok = ok && (mem_order_tag_v<MO_int_seqcst> == MemOrderTag_v::SeqCst);
    }
    return ok;
}

}  // namespace crucible::safety::extract
