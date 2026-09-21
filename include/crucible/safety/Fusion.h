#pragma once

// Answers whether two callables may be run as one combined pass that
// keeps the intermediate value in a register instead of writing it out.
//
// Each condition earns its place:
//
//   Both sides must be pure, because fusion reorders effects in
//   observable time.  This bans even effects that would commute.
//
//   Both sides must be noexcept.  Unfused, a throw from the producer
//   happens after its whole output exists; fused, the partial output
//   is register state that no handler can observe cleanly.  The
//   project builds without exceptions, so today the clause only
//   records the requirement.
//
//   The consumer must be unary and the producer must return
//   something, because the fused form is a straight chained call.
//
//   The types must match exactly, without an implicit conversion.  A
//   conversion is hidden work, and avoiding hidden work is the whole
//   reason to fuse.
//
// Two things the answer does not cover.  A signature cannot surface a
// write to global state, so purity here is structural rather than
// semantic.  And legality is not profitability: whether fusing pays
// for itself is a question for the cost model.

#include <crucible/safety/InferredRow.h>
#include <crucible/safety/_SignatureTraits.h>

#include <type_traits>

namespace crucible::safety {

namespace detail {

template <auto Fn, std::size_t I, typename T>
inline constexpr bool param_matches_v = std::is_same_v<std::remove_cvref_t<extract::param_type_t<Fn, I>>, T>;

template <auto Fn>
inline constexpr bool fusable_producer_shape_v =
    extract::is_pure_function_v<Fn> && extract::is_noexcept_v<Fn> && !std::is_same_v<extract::return_type_t<Fn>, void>;

template <auto Fn>
inline constexpr bool fusable_consumer_shape_v =
    extract::is_pure_function_v<Fn> && extract::is_noexcept_v<Fn> && extract::arity_v<Fn> == 1;

}  // namespace detail

// The parameter-match step is reached only after the arity is known
// to be one.  Asking for parameter zero of a nullary function is a
// hard error rather than a substitution failure, so the `if constexpr`
// chain, and not a conjunction, is what keeps a nullary consumer from
// breaking the build.

namespace detail {

template <auto Fn1, auto Fn2>
[[nodiscard]] consteval bool can_fuse_impl() noexcept {
    if constexpr (!fusable_producer_shape_v<Fn1>) {
        return false;
    } else if constexpr (!fusable_consumer_shape_v<Fn2>) {
        return false;
    } else {
        return param_matches_v<Fn2, 0, extract::return_type_t<Fn1>>;
    }
}

}  // namespace detail

template <auto Fn1, auto Fn2>
inline constexpr bool can_fuse_v = detail::can_fuse_impl<Fn1, Fn2>();

template <auto Fn1, auto Fn2>
concept IsFusable = can_fuse_v<Fn1, Fn2>;

// The result is a capture-free lambda rather than a type-erased
// callable.  A type-erased one would allocate and add an indirect
// call, and buys nothing here: a capture-free lambda already decays to
// a function pointer for any caller that wants one, and optimizers
// inline it into the same code a hand-written composition produces.
template <auto Fn1, auto Fn2>
    requires IsFusable<Fn1, Fn2>
[[nodiscard]] constexpr auto fuse() noexcept {
    return [](auto x) noexcept(noexcept(Fn2(Fn1(x)))) -> decltype(Fn2(Fn1(x))) { return Fn2(Fn1(x)); };
}

namespace detail::fuse_self_test {

inline int p_double(int x) noexcept { return x * 2; }
inline int p_inc(int x) noexcept { return x + 1; }
inline double p_to_double(int x) noexcept { return static_cast<double>(x); }
inline int p_to_int(double x) noexcept { return static_cast<int>(x); }

constexpr auto fused_double_then_inc = fuse<&p_double, &p_inc>();
static_assert(fused_double_then_inc(7) == 15);
static_assert(fused_double_then_inc(0) == 1);
static_assert(fused_double_then_inc(-3) == -5);

constexpr auto fused_promote = fuse<&p_to_double, &p_to_int>();
static_assert(fused_promote(42) == 42);

static_assert(noexcept(fused_double_then_inc(0)));
static_assert(noexcept(fused_promote(0)));

static_assert(std::is_same_v<decltype(fused_double_then_inc(0)), int>);
static_assert(std::is_same_v<decltype(fused_promote(0)), int>);

// The four properties below are what make a fused closure free to
// hold and to pass.  A hidden capture would push its size past one
// byte, which also stops the linker folding two identical fused
// closures onto one address.

static_assert(std::is_empty_v<decltype(fused_double_then_inc)>);
static_assert(std::is_empty_v<decltype(fused_promote)>);

static_assert(sizeof(decltype(fused_double_then_inc)) == 1);
static_assert(sizeof(decltype(fused_promote)) == 1);

static_assert(std::is_trivially_copyable_v<decltype(fused_double_then_inc)>);
static_assert(std::is_trivially_copyable_v<decltype(fused_promote)>);

static_assert(std::is_trivially_destructible_v<decltype(fused_double_then_inc)>);
static_assert(std::is_trivially_destructible_v<decltype(fused_promote)>);

}  // namespace detail::fuse_self_test

namespace detail::fusion_self_test {

inline int producer_int_to_int(int x) noexcept { return x * 2; }
inline int consumer_int_to_int(int x) noexcept { return x + 1; }
inline double producer_int_to_double(int x) noexcept { return static_cast<double>(x) * 1.5; }
inline int consumer_double_to_int(double x) noexcept { return static_cast<int>(x); }

static_assert(can_fuse_v<&producer_int_to_int, &consumer_int_to_int>);
static_assert(can_fuse_v<&producer_int_to_double, &consumer_double_to_int>);
static_assert(IsFusable<&producer_int_to_int, &consumer_int_to_int>);

static_assert(!can_fuse_v<&producer_int_to_int, &consumer_double_to_int>);

inline void producer_void(int) noexcept {}
static_assert(!can_fuse_v<&producer_void, &consumer_int_to_int>);

inline int consumer_nullary() noexcept { return 0; }
static_assert(!can_fuse_v<&producer_int_to_int, &consumer_nullary>);

inline int consumer_binary(int, int) noexcept { return 0; }
static_assert(!can_fuse_v<&producer_int_to_int, &consumer_binary>);

}  // namespace detail::fusion_self_test

}  // namespace crucible::safety
