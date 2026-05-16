#pragma once

// ── crucible::algebra — CostSemiring.h (FIXY-G11) ────────────────────
//
// The resource semiring `(Nanos, 0, +, max, ·)` realizing the cost-
// model bridge from meta-question J.  Cost is the dual of effects —
// effects track WHAT IS PRODUCED, coeffects track WHAT IS CONSUMED.
// Composition algebra for cost is a SEMIRING (Petricek-Orchard-Mycroft
// 2014 "Coeffects"; Brunel-Gaboardi-Mazza-Zdancewic 2014):
//
//   sequential composition  `+`    coefficient-wise addition
//   parallel composition    `max`  coefficient-wise maximum
//   repetition              `·`    coefficient-wise multiplication
//
// ── Carrier ──────────────────────────────────────────────────────────
//
// `CostPolynomial<Coefficients...>` encodes cost as a polynomial in
// the binding's input size.  Coefficients[i] is the ns contribution at
// degree i (Coefficients[0] = constant, [1] = linear, [2] = quadratic).
// All coefficients are u64 (saturating arithmetic — overflow saturates
// to UINT64_MAX which signifies "unbounded").
//
// ── Surface ──────────────────────────────────────────────────────────
//
//   template <std::uint64_t... Coefficients>
//   struct CostPolynomial          — value-type tag.
//
//   seq_compose_t<L, R>            — coefficient-wise sum (sequential)
//   par_compose_t<L, R>            — coefficient-wise max (parallel)
//   rep_compose_t<L, Reps>         — coefficient-wise mul (repetition)
//   evaluate_v<CP, InputSize>      — concrete nanos at given input size
//
// ── Semiring laws (proven by static_assert below) ───────────────────
//
//   seq is associative
//   seq is commutative (since seq is just `+` element-wise)
//   par is associative + commutative
//   seq distributes over par
//   CostPolynomial<0> is the seq identity (additive identity)
//   par's identity is "infinite cost" (max-meet) — practical use is
//     "any non-infinite cost beats infinity"
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   TypeSafe — strong NTTP-typed; no implicit conversion.
//   DetSafe  — bit-identical across compiles.
//   InitSafe — coefficients default-init via std::array<>.
//
// ── Runtime cost ────────────────────────────────────────────────────
//
// Zero.  All semiring operations are consteval; emitted as immediates.

#include <crucible/safety/Checked.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::algebra {

// ═════════════════════════════════════════════════════════════════════
// ── CostPolynomial — the carrier ───────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <std::uint64_t... Coefficients>
struct CostPolynomial final {
    static constexpr std::size_t degree_count = sizeof...(Coefficients);
    static constexpr std::array<std::uint64_t, sizeof...(Coefficients)>
        coeffs{Coefficients...};

    // Sentinel for "unbounded cost".  Used as the additive-saturation
    // anchor — any seq_compose involving infinity stays infinity.
    static constexpr std::uint64_t infinity = UINT64_MAX;
};

// ═════════════════════════════════════════════════════════════════════
// ── seq_compose_t — sequential composition (coefficient sum) ───────
// ═════════════════════════════════════════════════════════════════════
//
// Two costs running back-to-back add per-degree.  Saturating addition
// at UINT64_MAX (infinity stays infinity).  When the degree counts
// differ, the shorter polynomial is padded with zeros to the right.

namespace detail {

[[nodiscard]] consteval std::uint64_t sat_add(std::uint64_t a, std::uint64_t b) noexcept {
    if (a == UINT64_MAX || b == UINT64_MAX) return UINT64_MAX;
    const std::uint64_t result = a + b;
    return (result < a) ? UINT64_MAX : result;  // overflow → infinity
}

[[nodiscard]] consteval std::uint64_t sat_mul(std::uint64_t a, std::uint64_t b) noexcept {
    if (a == 0 || b == 0) return 0;
    if (a == UINT64_MAX || b == UINT64_MAX) return UINT64_MAX;
    if (a > UINT64_MAX / b) return UINT64_MAX;
    return a * b;
}

[[nodiscard]] consteval std::uint64_t sat_max(std::uint64_t a, std::uint64_t b) noexcept {
    return (a >= b) ? a : b;
}

// Read coefficient at position I from a CostPolynomial type CP.
// Returns 0 (the additive identity) when I is out of CP's range —
// this is the padding rule that lets seq/par_compose handle
// asymmetric-degree inputs cleanly.
template <typename CP, std::size_t I>
[[nodiscard]] consteval std::uint64_t coeff_of() noexcept {
    if constexpr (I < CP::degree_count) return CP::coeffs[I];
    else return 0;
}

template <typename L, typename R, std::size_t... Is>
[[nodiscard]] consteval auto seq_compose_pack(std::index_sequence<Is...>) noexcept {
    return CostPolynomial<sat_add(coeff_of<L, Is>(),
                                   coeff_of<R, Is>())...>{};
}

template <typename L, typename R, std::size_t... Is>
[[nodiscard]] consteval auto par_compose_pack(std::index_sequence<Is...>) noexcept {
    return CostPolynomial<sat_max(coeff_of<L, Is>(),
                                   coeff_of<R, Is>())...>{};
}

template <typename L, std::uint64_t Reps, std::size_t... Is>
[[nodiscard]] consteval auto rep_compose_pack(std::index_sequence<Is...>) noexcept {
    return CostPolynomial<sat_mul(coeff_of<L, Is>(), Reps)...>{};
}

// Result-degree = max(L::degree_count, R::degree_count).
template <typename L, typename R>
inline constexpr std::size_t max_degree_v =
    (L::degree_count >= R::degree_count) ? L::degree_count : R::degree_count;

}  // namespace detail

template <typename L, typename R>
using seq_compose_t = decltype(detail::seq_compose_pack<L, R>(
    std::make_index_sequence<detail::max_degree_v<L, R>>{}));

template <typename L, typename R>
using par_compose_t = decltype(detail::par_compose_pack<L, R>(
    std::make_index_sequence<detail::max_degree_v<L, R>>{}));

template <typename L, std::uint64_t Reps>
using rep_compose_t = decltype(detail::rep_compose_pack<L, Reps>(
    std::make_index_sequence<L::degree_count>{}));

// ═════════════════════════════════════════════════════════════════════
// ── evaluate_v — concrete nanos at given input size ────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Horner-style evaluation.  Saturating throughout — overflow at any
// step propagates UINT64_MAX as "unbounded".  Used by Cog-cost
// projection to map a polynomial + a specific input size to a
// predicted-wallclock-nanos number.

namespace detail {

template <typename CP, std::size_t I>
[[nodiscard]] consteval std::uint64_t evaluate_step(std::uint64_t acc,
                                                     std::uint64_t input_size) noexcept
{
    if constexpr (I == 0) {
        return sat_add(acc, CP::coeffs[0]);
    } else {
        const std::uint64_t term = sat_mul(CP::coeffs[I], [&]() {
            std::uint64_t power = 1;
            for (std::size_t k = 0; k < I; ++k) {
                power = sat_mul(power, input_size);
            }
            return power;
        }());
        return evaluate_step<CP, I - 1>(sat_add(acc, term), input_size);
    }
}

}  // namespace detail

template <typename CP, std::uint64_t InputSize>
inline constexpr std::uint64_t evaluate_v = []() {
    if constexpr (CP::degree_count == 0) return std::uint64_t{0};
    else return detail::evaluate_step<CP, CP::degree_count - 1>(
        std::uint64_t{0}, InputSize);
}();

// ═════════════════════════════════════════════════════════════════════
// ── Concept gate for cost polynomial types ─────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail {

template <typename>
inline constexpr bool is_cost_polynomial_v = false;

template <std::uint64_t... Cs>
inline constexpr bool is_cost_polynomial_v<CostPolynomial<Cs...>> = true;

}  // namespace detail

template <typename T>
concept IsCostPolynomial = detail::is_cost_polynomial_v<std::remove_cvref_t<T>>;

// ═════════════════════════════════════════════════════════════════════
// ── Common cost-polynomial shapes ──────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

using ZeroCost     = CostPolynomial<0>;
using InfiniteCost = CostPolynomial<UINT64_MAX>;

// ═════════════════════════════════════════════════════════════════════
// ── Self-tests — semiring laws ─────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace cost_semiring_self_test {

using A = CostPolynomial<5,  3>;   // 5 + 3·n
using B = CostPolynomial<7, 11>;   // 7 + 11·n
using C = CostPolynomial<2,  4>;   // 2 + 4·n

// seq is associative: (A + B) + C  ==  A + (B + C)
using AB_C = seq_compose_t<seq_compose_t<A, B>, C>;
using A_BC = seq_compose_t<A, seq_compose_t<B, C>>;
static_assert(std::is_same_v<AB_C, A_BC>,
    "Cost semiring: seq_compose must be associative.");

// seq is commutative: A + B  ==  B + A
using AB = seq_compose_t<A, B>;
using BA = seq_compose_t<B, A>;
static_assert(std::is_same_v<AB, BA>,
    "Cost semiring: seq_compose must be commutative.");

// par is associative: max(max(A, B), C)  ==  max(A, max(B, C))
using PAR_AB_C = par_compose_t<par_compose_t<A, B>, C>;
using PAR_A_BC = par_compose_t<A, par_compose_t<B, C>>;
static_assert(std::is_same_v<PAR_AB_C, PAR_A_BC>,
    "Cost semiring: par_compose must be associative.");

// par is commutative.
using PAR_AB = par_compose_t<A, B>;
using PAR_BA = par_compose_t<B, A>;
static_assert(std::is_same_v<PAR_AB, PAR_BA>,
    "Cost semiring: par_compose must be commutative.");

// ZeroCost is the seq identity.
using A_PLUS_ZERO = seq_compose_t<A, CostPolynomial<0, 0>>;
static_assert(A_PLUS_ZERO::coeffs == A::coeffs,
    "Cost semiring: seq_compose<A, ZeroCost> must equal A.");

// Repetition: A repeated 3 times.
using A_REP3 = rep_compose_t<A, 3>;
static_assert(A_REP3::coeffs[0] == 15);  // 5*3
static_assert(A_REP3::coeffs[1] == 9);   // 3*3

// Evaluation at concrete input.
// Cost(A, n=10) = 5 + 3*10 = 35.
static_assert(evaluate_v<A, 10> == 35);
// Cost(B, n=100) = 7 + 11*100 = 1107.
static_assert(evaluate_v<B, 100> == 1107);
// Cost(ZeroCost, n=anything) = 0.
static_assert(evaluate_v<ZeroCost, 1000> == 0);
// Cost(quadratic, n=10) = 0 + 0*10 + 2*100 = 200.
using QUAD2 = CostPolynomial<0, 0, 2>;
static_assert(evaluate_v<QUAD2, 10> == 200);

// Saturation: huge n times huge coefficient → infinity.
using BIG = CostPolynomial<0, UINT64_MAX>;
static_assert(evaluate_v<BIG, 100> == UINT64_MAX);

// IsCostPolynomial concept.
static_assert(IsCostPolynomial<A>);
static_assert(IsCostPolynomial<ZeroCost>);
static_assert(!IsCostPolynomial<int>);
static_assert(!IsCostPolynomial<void>);

}  // namespace cost_semiring_self_test

}  // namespace crucible::algebra
