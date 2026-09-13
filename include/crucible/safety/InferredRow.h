#pragma once

// A context tag contributes its own atom and is not expanded into the atoms it
// implies.  A caller that needs the expanded form takes the union itself.

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/safety/SignatureTraits.h>

#include <type_traits>

namespace crucible::safety::extract {

namespace detail {

template <typename P, typename Acc>
[[nodiscard]] consteval auto accumulate_param_effect() noexcept {
    using namespace ::crucible::effects;
    using PB = std::remove_cvref_t<P>;

    if constexpr (std::is_same_v<PB, cap::Alloc>) {
        return std::type_identity<::crucible::effects::detail::row_insert_unique_t<Acc, Effect::Alloc>>{};
    } else if constexpr (std::is_same_v<PB, cap::IO>) {
        return std::type_identity<::crucible::effects::detail::row_insert_unique_t<Acc, Effect::IO>>{};
    } else if constexpr (std::is_same_v<PB, cap::Block>) {
        return std::type_identity<::crucible::effects::detail::row_insert_unique_t<Acc, Effect::Block>>{};
    } else if constexpr (std::is_same_v<PB, Bg>) {
        return std::type_identity<::crucible::effects::detail::row_insert_unique_t<Acc, Effect::Bg>>{};
    } else if constexpr (std::is_same_v<PB, Init>) {
        return std::type_identity<::crucible::effects::detail::row_insert_unique_t<Acc, Effect::Init>>{};
    } else if constexpr (std::is_same_v<PB, Test>) {
        return std::type_identity<::crucible::effects::detail::row_insert_unique_t<Acc, Effect::Test>>{};
    } else {
        return std::type_identity<Acc>{};
    }
}

template <typename P, typename Acc>
using accumulate_param_effect_t = typename decltype(accumulate_param_effect<P, Acc>())::type;

template <auto FnPtr, std::size_t I, typename Acc, bool AtEnd>
struct infer_row_step;

template <auto FnPtr, std::size_t I, typename Acc>
struct infer_row_step<FnPtr, I, Acc, true> {
    using type = Acc;
};

template <auto FnPtr, std::size_t I, typename Acc>
struct infer_row_step<FnPtr, I, Acc, false> {
    using P_I = param_type_t<FnPtr, I>;
    using NextAcc = accumulate_param_effect_t<P_I, Acc>;
    static constexpr std::size_t Next = I + 1;
    using type = typename infer_row_step<FnPtr, Next, NextAcc, (Next >= arity_v<FnPtr>)>::type;
};

template <auto FnPtr>
using infer_row_raw = typename infer_row_step<FnPtr, 0, ::crucible::effects::EmptyRow, (0 >= arity_v<FnPtr>)>::type;

}  // namespace detail

// The row keeps declaration order, and the Row type compares by order.  Two
// signatures that carry the same atoms in different order therefore yield
// different Row types and must be compared with the subrow relation.
template <auto FnPtr>
using inferred_row_t = detail::infer_row_raw<FnPtr>;

template <auto FnPtr>
inline constexpr std::size_t inferred_row_count_v = inferred_row_t<FnPtr>::size;

template <auto FnPtr, ::crucible::effects::Effect E>
inline constexpr bool function_has_effect_v = ::crucible::effects::row_contains_v<
    inferred_row_t<FnPtr>,
    E>;  // ROW-CONTAINS-OK: inferred-row point query over inferred_row_t<FnPtr>, not a Ctx capability check

template <auto FnPtr>
inline constexpr bool is_pure_function_v = inferred_row_count_v<FnPtr> == 0;

template <auto FnPtr>
concept IsPureFunction = is_pure_function_v<FnPtr>;

namespace detail::infer_row_self_test {

inline void f_pure(int, double) noexcept {}
inline void f_alloc(::crucible::effects::Alloc, std::size_t) noexcept {}
inline void f_bg(::crucible::effects::Bg, int) noexcept {}
inline void f_alloc_io(::crucible::effects::Alloc, ::crucible::effects::IO, int) noexcept {}
inline void f_alloc_dup(::crucible::effects::Alloc, ::crucible::effects::Alloc, int) noexcept {}

static_assert(std::is_same_v<inferred_row_t<&f_pure>, ::crucible::effects::EmptyRow>);
static_assert(inferred_row_count_v<&f_pure> == 0);
static_assert(is_pure_function_v<&f_pure>);
static_assert(IsPureFunction<&f_pure>);

static_assert(std::is_same_v<inferred_row_t<&f_alloc>, ::crucible::effects::Row<::crucible::effects::Effect::Alloc>>);
static_assert(inferred_row_count_v<&f_alloc> == 1);
static_assert(!is_pure_function_v<&f_alloc>);
static_assert(function_has_effect_v<&f_alloc, ::crucible::effects::Effect::Alloc>);
static_assert(!function_has_effect_v<&f_alloc, ::crucible::effects::Effect::IO>);

static_assert(std::is_same_v<inferred_row_t<&f_bg>, ::crucible::effects::Row<::crucible::effects::Effect::Bg>>);
static_assert(function_has_effect_v<&f_bg, ::crucible::effects::Effect::Bg>);
static_assert(!function_has_effect_v<&f_bg, ::crucible::effects::Effect::Alloc>);

static_assert(
    std::is_same_v<inferred_row_t<&f_alloc_io>,
                   ::crucible::effects::Row<::crucible::effects::Effect::Alloc, ::crucible::effects::Effect::IO>>);
static_assert(inferred_row_count_v<&f_alloc_io> == 2);

static_assert(
    std::is_same_v<inferred_row_t<&f_alloc_dup>, ::crucible::effects::Row<::crucible::effects::Effect::Alloc>>);
static_assert(inferred_row_count_v<&f_alloc_dup> == 1);

}  // namespace detail::infer_row_self_test

inline bool inferred_row_smoke_test() noexcept {
    using namespace detail::infer_row_self_test;
    using namespace ::crucible::effects;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_pure_function_v<&f_pure>;
        ok = ok && !is_pure_function_v<&f_alloc>;
        ok = ok && (inferred_row_count_v<&f_alloc_io> == 2);
        ok = ok && function_has_effect_v<&f_bg, Effect::Bg>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
