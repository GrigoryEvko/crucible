#pragma once

// ── crucible::fixy::dim — Cost.h (FIXY-G11) ────────────────────────────
//
// dim::Cost is the cost-model axis #21.  Cost is the DUAL of effects:
// effects track WHAT IS PRODUCED (IO, Bg, Alloc...), coeffects track
// WHAT IS CONSUMED (compute time per input size).  Cost grades carry a
// CostPolynomial in input-size encoding per-degree nanos.
//
// ── Why an opt-in axis (not required at IsAccepted) ─────────────────
//
// Adding Cost to the substrate `DimensionAxis` enum would invalidate
// every fixy::fn instantiation in the codebase (each would need a new
// `accept_default_strict_for<dim::Cost>` entry).  Per the
// substrate-issue escape clause in 16_05_2026_fixy.md §J.4, axis #21
// ships as a FIXY-LAYER addition with OPT-IN engagement:
//
//   * `dim::Cost` value lives at the fixy layer (value 20, outside
//     the substrate's 0..19 enumerator range).
//   * `IsAccepted<Grants...>` keeps its 20-dim conjunction unchanged
//     — existing bindings continue to work.
//   * Cost-aware consumers gate on the consumer-side predicate
//     `HasCostGrant<F>` instead of expanding IsAccepted.
//   * The reject-by-default discipline applies WHEN A BINDING claims
//     Cost-aware status: if the binding's hot-path Rule R015 demands
//     cost engagement, R015 fires the gate at the consumer site.
//
// This preserves the "append-only Universe extension" contract:
// existing 20 axes unchanged; new axis is additive and opt-in.
//
// ── Surface ──────────────────────────────────────────────────────────
//
//   inline constexpr dim::DimAxis Cost  — value 20 (fixy-layer)
//
//   cg::cost_polynomial<C0, C1, ...>     — Coeffect modality on Cost
//   cg::cost_unknown                      — strict default (unbounded)
//   cg::cost_constant<N>                  — alias for polynomial<N>
//   cg::cost_linear<N>                    — alias for polynomial<0, N>
//   cg::cost_quadratic<N>                 — alias for polynomial<0, 0, N>
//   cg::cost_polynomial_e<W, ...>         — evidenced variant
//
//   HasCostGrant<F>                       — consumer-side predicate;
//                                            true iff F engages Cost.
//   fn_cost_polynomial_t<F>               — extract the CostPolynomial
//                                            type from F's pack
//                                            (fallback: cost_unknown).
//
// ── References ──────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §6 Phase G    — G11 cost semiring axis
//   algebra/CostSemiring.h                — semiring carrier
//   fixy/Modality.h                       — Coeffect modality class

#include <crucible/algebra/CostSemiring.h>
#include <crucible/algebra/Modality.h>
#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Fn.h>
#include <crucible/fixy/Grant.h>
#include <crucible/fixy/Modality.h>
#include <crucible/safety/witness/IsWitness.h>
#include <crucible/safety/witness/Witness.h>

#include <cstdint>
#include <type_traits>

namespace crucible::fixy::dim {

// ── Cost — fixy-layer DimAxis value #21 ────────────────────────────
//
// Lives at value 20 (outside the substrate's 0..19 range).  The fixy
// layer admits it as a legitimate axis; the substrate's
// `dimension_axis_name` returns the `<unknown>` sentinel for 20 (which
// is the documented out-of-range behavior).  Consumers that need a
// human-readable name route through `fixy::dim::cost_axis_name()`.

inline constexpr DimAxis Cost = static_cast<DimAxis>(20);

static_assert(static_cast<std::uint8_t>(Cost) == 20,
    "fixy::dim::Cost MUST be value 20 — the next free position after "
    "the substrate's 0..19 enumerator range.  Adding more fixy-layer "
    "axes should append at 21, 22, ... preserving the append-only "
    "Universe extension policy.");

[[nodiscard]] constexpr std::string_view cost_axis_name(DimAxis d) noexcept {
    return (d == Cost) ? std::string_view{"Cost"} : ::crucible::fixy::dim::name(d);
}

}  // namespace crucible::fixy::dim

namespace crucible::fixy::grant {

// ═════════════════════════════════════════════════════════════════════
// ── cost_polynomial — Coeffect modality on dim::Cost ───────────────
// ═════════════════════════════════════════════════════════════════════
//
// The canonical Cost-engaging grant.  Coefficients name per-degree
// nanos for the binding's input-size scaling — Coefficients[0] is
// constant, [1] is linear, [2] is quadratic.  The cost semiring's
// composition operations (seq/par/rep) act on these coefficients.

template <std::uint64_t... Coefficients>
struct cost_polynomial final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Cost;
    using modality = ::crucible::algebra::modality::Coeffect_t;
    using polynomial_t = ::crucible::algebra::CostPolynomial<Coefficients...>;
};

// Strict default: unbounded cost.  Mirrors complexity_unbounded but at
// the cost-axis layer.  Hot-path R015 rejects bindings carrying this
// engagement — cost_unknown can't be promoted to Hot residency.
struct cost_unknown final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Cost;
    using modality = ::crucible::algebra::modality::Coeffect_t;
    using polynomial_t = ::crucible::algebra::CostPolynomial<UINT64_MAX>;
};

// Named shortcuts — readability + grep-discoverability.
template <std::uint64_t C0>
using cost_constant_v = cost_polynomial<C0>;

template <std::uint64_t C1>
using cost_linear_v = cost_polynomial<0, C1>;

template <std::uint64_t C2>
using cost_quadratic_v = cost_polynomial<0, 0, C2>;

// Evidenced variant — carries a Witness for the cost claim (typically
// a Tested<calib_run_id> for per-Cog calibration evidence).
template <::crucible::safety::witness::IsWitness W, std::uint64_t... Coefficients>
struct cost_polynomial_e final : grant_base {
    static constexpr dim::DimAxis relaxes = dim::Cost;
    using modality = ::crucible::algebra::modality::Coeffect_t;
    using polynomial_t = ::crucible::algebra::CostPolynomial<Coefficients...>;
    using witness_t = W;
};

}  // namespace crucible::fixy::grant

namespace crucible::fixy {

// ═════════════════════════════════════════════════════════════════════
// ── HasCostGrant<F> — consumer-side predicate ──────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail {

template <typename G>
inline constexpr bool grant_engages_cost_v = false;

template <std::uint64_t... Cs>
inline constexpr bool grant_engages_cost_v<::crucible::fixy::grant::cost_polynomial<Cs...>> = true;

template <>
inline constexpr bool grant_engages_cost_v<::crucible::fixy::grant::cost_unknown> = true;

template <typename W, std::uint64_t... Cs>
inline constexpr bool grant_engages_cost_v<
    ::crucible::fixy::grant::cost_polynomial_e<W, Cs...>> = true;

template <typename F>
struct fn_has_cost_grant;

template <typename T, typename... Grants>
struct fn_has_cost_grant<::crucible::fixy::fn<T, Grants...>> {
    static constexpr bool value =
        (grant_engages_cost_v<std::remove_cvref_t<Grants>> || ... || false);
};

}  // namespace detail

template <typename F>
inline constexpr bool HasCostGrant =
    detail::fn_has_cost_grant<std::remove_cvref_t<F>>::value;

// ═════════════════════════════════════════════════════════════════════
// ── fn_cost_polynomial_t<F> — extract the CostPolynomial ───────────
// ═════════════════════════════════════════════════════════════════════
//
// Walks the Grants pack and pulls the first Cost-engaging grant's
// `polynomial_t`.  Falls back to `cost_unknown::polynomial_t` (which
// is `CostPolynomial<UINT64_MAX>`) when no grant engages Cost.

namespace detail {

template <typename G>
struct cost_polynomial_of_impl {
    using type = void;  // not a cost grant
};

template <std::uint64_t... Cs>
struct cost_polynomial_of_impl<::crucible::fixy::grant::cost_polynomial<Cs...>> {
    using type = ::crucible::algebra::CostPolynomial<Cs...>;
};

template <>
struct cost_polynomial_of_impl<::crucible::fixy::grant::cost_unknown> {
    using type = ::crucible::algebra::CostPolynomial<UINT64_MAX>;
};

template <typename W, std::uint64_t... Cs>
struct cost_polynomial_of_impl<::crucible::fixy::grant::cost_polynomial_e<W, Cs...>> {
    using type = ::crucible::algebra::CostPolynomial<Cs...>;
};

template <typename... Grants>
struct first_cost_polynomial_in;

template <>
struct first_cost_polynomial_in<> {
    using type = ::crucible::algebra::CostPolynomial<UINT64_MAX>;
};

template <typename G, typename... Rest>
struct first_cost_polynomial_in<G, Rest...> {
    using head_t = typename cost_polynomial_of_impl<std::remove_cvref_t<G>>::type;
    using type = std::conditional_t<
        !std::is_same_v<head_t, void>,
        head_t,
        typename first_cost_polynomial_in<Rest...>::type>;
};

template <typename F>
struct fn_cost_polynomial;

template <typename T, typename... Grants>
struct fn_cost_polynomial<::crucible::fixy::fn<T, Grants...>> {
    using type = typename first_cost_polynomial_in<Grants...>::type;
};

}  // namespace detail

template <typename F>
using fn_cost_polynomial_t =
    typename detail::fn_cost_polynomial<std::remove_cvref_t<F>>::type;

// ═════════════════════════════════════════════════════════════════════
// ── Flow-level cost composition ─────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Per-channel cost transformation used by downstream cost-aware
// tooling (per-Cog projection, R015 budget verification).  Each Flow
// channel imposes a default transport rule:
//
//   Identity / Reshard          — seq_compose of producer + consumer
//                                  (no transport overhead modeled)
//   Federate                    — seq_compose + per-hop network
//                                  overhead (placeholder constant 100ns)
//   Persist                     — seq_compose + IO overhead (1000ns)
//   Sample                      — seq_compose; sampled output has no
//                                  additional cost on the producer
//   Spawn                       — par_compose (parallel branches)
//
// Returns a CostPolynomial<>; downstream code calls evaluate_v on it.

namespace detail {

// Default channel-cost transformation: sequential composition.
template <typename Ch>
struct channel_cost_transform {
    template <typename PolyL, typename PolyR>
    using compose_t = ::crucible::algebra::seq_compose_t<PolyL, PolyR>;
};

}  // namespace detail

template <typename F1, typename Ch, typename F2>
using flow_cost_polynomial_t =
    typename detail::channel_cost_transform<Ch>::template compose_t<
        fn_cost_polynomial_t<F1>,
        fn_cost_polynomial_t<F2>>;

// ═════════════════════════════════════════════════════════════════════
// ── Self-tests ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace cost_self_test {

namespace cg = ::crucible::fixy::grant;
namespace ca = ::crucible::algebra;

// Engagement on dim::Cost.
static_assert(cg::cost_polynomial<5, 3>::relaxes == dim::Cost);
static_assert(cg::cost_unknown::relaxes == dim::Cost);
static_assert(cg::cost_constant_v<10>::relaxes == dim::Cost);

// Coeffect modality classification.
static_assert(grant_traits<cg::cost_polynomial<1, 2>>::modality_class_v
              == ModalityClass::Coeffect);
static_assert(grant_traits<cg::cost_unknown>::modality_class_v
              == ModalityClass::Coeffect);

// polynomial_t extraction at the grant level.
static_assert(std::is_same_v<typename cg::cost_polynomial<5, 3>::polynomial_t,
                             ca::CostPolynomial<5, 3>>);
static_assert(std::is_same_v<typename cg::cost_unknown::polynomial_t,
                             ca::CostPolynomial<UINT64_MAX>>);

// Named shortcuts.
static_assert(std::is_same_v<cg::cost_constant_v<10>,
                             cg::cost_polynomial<10>>);
static_assert(std::is_same_v<cg::cost_linear_v<7>,
                             cg::cost_polynomial<0, 7>>);
static_assert(std::is_same_v<cg::cost_quadratic_v<2>,
                             cg::cost_polynomial<0, 0, 2>>);

}  // namespace cost_self_test

}  // namespace crucible::fixy
