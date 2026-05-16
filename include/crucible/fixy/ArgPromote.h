#pragma once

// ── crucible::fixy — ArgPromote.h (FIXY-G5) ────────────────────────────
//
// Argument-grade discipline.  When a fixy::fn<F> binding declares
// function-pointer-typed arguments with already-wrapped substrate
// types (Refined<>, Tagged<>, Linear<>, etc.), a downstream call site
// shouldn't have to spell out the wrapper chain — `mint_arg<F, N>(raw)`
// performs the typed promotion, including refinement-predicate
// checks at construction.
//
// **Surface.**
//
//   fixy::arg_grade_t<F, N>               — F's declared type for arg N.
//   fixy::arg_promote_t<F, N, RawType>    — wrapper type that bridges
//                                            RawType → arg_grade_t<F,N>,
//                                            or PromoteImpossible<...>
//                                            carrier on failure.
//   fixy::mint_arg<F, N>(raw)             — runtime promoter; calls
//                                            wrapper ctor with raw.
//   fixy::call_typed<F>(bound, args...)   — auto-promote every arg via
//                                            mint_arg<F, i>; concept-
//                                            gated for promotability.
//
// ── Scope ────────────────────────────────────────────────────────────
//
// Phase G5 ships:
//   * The metafunction surface (arg_grade_t / arg_promote_t).
//   * Identity-promote path: when RawType already matches
//     arg_grade_t<F, N>, mint_arg is a no-op forward.
//   * Convertible-promote path: when arg_grade_t<F, N> is constructible
//     from RawType (e.g., Refined<Pred, int> from int when the predicate
//     accepts), mint_arg constructs the wrapper.
//   * Refusal path: when no promote-path exists, the diagnostic carrier
//     PromoteImpossible<F, N, RawType> is emitted via the requires-
//     clause; static_assert in the resolver names the offending arg.
//
// Future iterations grow specific promote paths per substrate wrapper
// (Tagged<T, source::FromUser> from a raw T via sanitize<...> in the
// binding's grants; Linear<T> from rvalue T via std::move; ...).
//
// ── Axiom coverage ────────────────────────────────────────────────────
//
//   InitSafe   — wrapper ctors fire NSDMI + contract checks at construction.
//   TypeSafe   — arg_grade_t<> emits the declared wrapper type exactly.
//   NullSafe   — promote-impossible is a compile error, not a silent nullptr.
//   MemSafe    — constructed wrappers are move-only / EBO-collapsing as the
//                substrate dictates.
//   BorrowSafe — non-owning promotes forward references; owning promotes
//                move into the wrapper.
//   ThreadSafe — pure construction; no shared state.
//   LeakSafe   — wrapper ctors are RAII; mint_arg is a function-local op.
//   DetSafe    — promotion path is purely type-driven; same inputs →
//                same wrapped output.
//
// ── References ────────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §5 Phase G    — arg-grade promotion
//   safety/Refined.h                      — refinement predicates
//   safety/Linear.h                       — linear move-only token
//   fixy/Reflect.h                        — companion reflection layer

#include <crucible/fixy/Fn.h>
#include <crucible/fixy/Reflect.h>
#include <crucible/fixy/Reject.h>
#include <crucible/safety/Fn.h>

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::fixy {

// ─── PromoteImpossible carrier ─────────────────────────────────────
//
// When no promote-path exists from RawType to arg_grade_t<F, N>, the
// type-level diagnostic carrier names F, N, and RawType.  Greppable
// from compiler errors.

template <typename F, std::size_t N, typename RawType>
struct PromoteImpossible {
    // Intentionally empty.  Type identity carries the diagnostic.
};

// ─── arg_grade_t<F, N> ─────────────────────────────────────────────
//
// Projects the Nth argument of F's underlying callable type to its
// declared (wrapped) form.  Handles function pointers, std::function-
// like types via function-traits.

namespace detail {

template <typename T>
struct function_traits;

template <typename R, typename... A>
struct function_traits<R(A...)> {
    using return_type = R;
    using arg_tuple   = std::tuple<A...>;
    static constexpr std::size_t arity = sizeof...(A);
};

template <typename R, typename... A>
struct function_traits<R(*)(A...)> : function_traits<R(A...)> {};

template <typename R, typename... A>
struct function_traits<R(*)(A...) noexcept> : function_traits<R(A...)> {};

template <typename F>
struct arg_grade_impl;

template <typename T, typename... Grants>
struct arg_grade_impl<::crucible::fixy::fn<T, Grants...>> {
    template <std::size_t N>
    using nth_arg =
        std::tuple_element_t<N, typename function_traits<T>::arg_tuple>;
};

}  // namespace detail

template <typename F, std::size_t N>
    requires IsFixyFn<F>
using arg_grade_t =
    typename detail::arg_grade_impl<std::remove_cvref_t<F>>::template nth_arg<N>;

// ─── arg_promote_t<F, N, RawType> ──────────────────────────────────
//
// Returns the wrapper type that bridges RawType → arg_grade_t<F, N>.
// Identity (RawType same as arg_grade_t) bypasses; otherwise checks
// constructibility and returns arg_grade_t<F, N>; otherwise the
// diagnostic carrier PromoteImpossible<F, N, RawType>.

namespace detail {

template <typename Target, typename Raw>
inline constexpr bool can_promote_v =
    std::is_same_v<std::remove_cvref_t<Target>, std::remove_cvref_t<Raw>>
    || std::is_constructible_v<std::remove_cvref_t<Target>, Raw>
    || std::is_convertible_v<Raw, std::remove_cvref_t<Target>>;

template <typename Target, typename Raw, bool = can_promote_v<Target, Raw>>
struct promote_result {
    using type = Target;
};

template <typename Target, typename Raw>
struct promote_result<Target, Raw, false> {
    // Substituted via tag dispatch in arg_promote_t below — produces
    // the diagnostic carrier instead.
    using type = void;  // sentinel; the real diagnostic is emitted by
                        // the static_assert in mint_arg / call_typed.
};

}  // namespace detail

template <typename F, std::size_t N, typename RawType>
    requires IsFixyFn<F>
using arg_promote_t =
    typename detail::promote_result<arg_grade_t<F, N>, RawType>::type;

template <typename F, std::size_t N, typename RawType>
concept ArgPromotable =
    IsFixyFn<F> &&
    detail::can_promote_v<arg_grade_t<F, N>, RawType>;

// ─── mint_arg<F, N>(raw) ───────────────────────────────────────────
//
// Constructs the typed wrapper for arg N of F from a raw value.  When
// no promote path exists, the static_assert at the top of the body
// emits a PromoteImpossible<F, N, RawType> diagnostic naming all three
// pieces.  Single overload (not concept-gated) — the assertion fires
// at instantiation time, producing a clear error rather than a
// substitution failure scattered across overload resolution.

template <typename F, std::size_t N, typename RawType>
    requires IsFixyFn<F>
[[nodiscard]] constexpr auto mint_arg(RawType&& raw)
    noexcept(std::is_nothrow_constructible_v<arg_grade_t<F, N>, RawType>)
{
    static_assert(ArgPromotable<F, N, std::remove_cvref_t<RawType>>,
        "fixy::mint_arg<F, N>(raw): cannot promote raw argument to "
        "F's declared arg type.  No promote path exists — RawType is "
        "neither identical to arg_grade_t<F, N> nor constructible/"
        "convertible to it.  PromoteImpossible<F, N, RawType> carrier "
        "names the failure in the diagnostic.");
    if constexpr (ArgPromotable<F, N, std::remove_cvref_t<RawType>>) {
        // Use parentheses-init (not brace-init) so implicit narrowing
        // conversions admitted by `std::is_constructible_v<int, long>`
        // are allowed by mint_arg.  Brace-init would reject narrowing
        // even though the can_promote_v concept accepted the path.
        return arg_grade_t<F, N>(std::forward<RawType>(raw));
    } else {
        // Diagnostic carrier — type-level marker for the build log.
        return PromoteImpossible<std::remove_cvref_t<F>, N,
                                 std::remove_cvref_t<RawType>>{};
    }
}

// ─── call_typed<F>(bound, raw_args...) ─────────────────────────────
//
// Calls bound.value()(mint_arg<F, 0>(arg0), mint_arg<F, 1>(arg1), ...)
// — auto-promoting every raw arg through the declared wrapper chain.
// The concept gate requires every arg to be promotable; missing paths
// fire the PromoteImpossible diagnostic at the static_assert above.

namespace detail {

template <typename F, std::size_t... Is, typename Bound, typename Tup>
constexpr decltype(auto)
call_typed_impl(std::index_sequence<Is...>, Bound&& bound, Tup&& tup) noexcept(
    noexcept(std::forward<Bound>(bound).value()(
        mint_arg<F, Is>(std::get<Is>(std::forward<Tup>(tup)))...)))
{
    return std::forward<Bound>(bound).value()(
        mint_arg<F, Is>(std::get<Is>(std::forward<Tup>(tup)))...);
}

}  // namespace detail

// `F` is the explicit fixy::fn type the caller wants to drive; `Bound`
// is the forwarding-reference parameter that may bind an lvalue or
// rvalue.  Splitting these two preserves grep-target ergonomics
// (`call_typed<MyFn>(bound, ...)`) while not stealing the value's
// type via template-deduction trickery.

template <typename F, typename Bound, typename... RawArgs>
    requires IsFixyFn<F>
constexpr decltype(auto) call_typed(Bound&& bound, RawArgs&&... args) {
    using traits = detail::function_traits<typename std::remove_cvref_t<F>::type_t>;
    static_assert(sizeof...(RawArgs) == traits::arity,
        "fixy::call_typed<F>: arg pack arity does not match F's "
        "declared callable arity.  Pass exactly arity-many raw args; "
        "each is promoted through mint_arg<F, i> independently.");
    return detail::call_typed_impl<std::remove_cvref_t<F>>(
        std::make_index_sequence<sizeof...(RawArgs)>{},
        std::forward<Bound>(bound),
        std::forward_as_tuple(std::forward<RawArgs>(args)...));
}

}  // namespace crucible::fixy
