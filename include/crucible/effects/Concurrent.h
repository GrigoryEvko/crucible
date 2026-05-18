#pragma once

// ── crucible::effects::ConcurrentRow — additive resource budget rows
//
// GAPS-190.  When two ops are scheduled concurrently on the same Cog
// (compute kernel + collective comm kernel running in parallel via
// stream overlap), the row-union must SUM their resource consumption
// so the compiler can verify the combined load fits the Cog's caps.
// This is distinct from the existing `effects::Row<Effect::*>` set-
// union semantics — that's boolean over capability atoms; this is
// arithmetic over budget magnitudes.
//
// Without this primitive the compile-time budget check is impossible:
// the optimizer would only see per-op rows in isolation and miss the
// combined load that actually races on the silicon.
//
//   Axiom coverage: TypeSafe — ConcurrentRow holds only ResourceTag
//                   instantiations (concept-gated).  Non-resource
//                   types fail at template substitution, not at use
//                   site.
//                   DetSafe — every operation is consteval; result
//                   types are content-addressed (same input row pair
//                   yields the same canonical output type on every
//                   compiler / TU).
//                   InitSafe — empty rows have well-defined size 0
//                   semantics; per-kind aggregation defaults to 0
//                   for absent kinds.
//   Runtime cost:   zero — rows have no runtime representation; the
//                   sum lives purely in the type system / consteval
//                   evaluation, mirroring effects::Row<Es...>.
//
// Public surface:
//   ConcurrentRow<Tags...>          — concrete row carrying
//                                     ResourceTag instantiations.  By
//                                     convention canonical (each
//                                     ResourceKind appears at most
//                                     once); construction from
//                                     non-canonical packs is allowed
//                                     but `concurrent_row_sum` always
//                                     produces canonical output.
//   concurrent_row_value_v<K, R>    — sum of K-kind values in R
//                                     (0 if K absent)
//   concurrent_row_sum_t<R1, R2>    — pairwise additive union;
//                                     canonical
//   concurrent_row_n_t<Rs...>       — variadic N-way fold over
//                                     concurrent_row_sum_t
//   ConcurrentlySchedulable<R1, R2> — concept: the pairwise sum is
//                                     well-formed (no uint64_t
//                                     overflow on any summed kind)
//
// Naming rationale.  The "concurrent" prefix marks this as the
// COMBINED-LOAD operation the Forge scheduler performs when
// reasoning about stream-overlap parallelism — distinct from
// `row_union_t` in EffectRow.h (set semantics on Effect atoms) and
// from the "sequential" composition where the second op cannot start
// until the first releases its budgets.  Sequential composition is
// max-of-budgets per kind, not sum-of-budgets; that's a future
// `sequential_row_sum_t` if it ever lands (Forge currently treats all
// per-step parallelism as concurrent for the budget check).
//
// ── Overflow discipline ─────────────────────────────────────────────
//
// Per-kind budget values are uint64_t.  A naive sum
//   SmBudget<UINT64_MAX> + SmBudget<1>
// would silently wrap to SmBudget<0> — a critical violation that
// would let an oversubscribed schedule pass the FitsCog gate.  The
// ConcurrentlySchedulable concept includes a no-overflow check per
// summed kind via the standard formula `A + B >= A`.  Concurrent
// scheduling that overflows ANY kind is rejected at template
// substitution, with a routed diagnostic naming the offending kind.
//
// ── Gates ───────────────────────────────────────────────────────────
//
//   Consumed by:
//     GAPS-191 cog/FitsCog.h           — concept: Row ≤
//                                        Cog::TargetCaps; consumes
//                                        `concurrent_row_sum_t` to
//                                        compute the combined load
//                                        before the comparison.
//     GAPS-165 AdaptiveOptimizer       — per-step concurrency
//                                        decisions read combined
//                                        load, not per-op load.
//     GAPS-195 CommOverlap             — Forge Phase H scheduler
//                                        reasons about combined
//                                        compute+comm rows.
//
//   Depends on:
//     effects/Resources.h              — the 23 ResourceTag types
//                                        and the ResourceKind atom
//                                        catalog.
//
// References:
//   misc/03_05_2026_networking.md §4.3 (compile-time budget
//   verification), 25_04_2026.md §3.3 (Met(X) row machinery),
//   Tang-Lindley POPL 2026 (arXiv:2507.10301).

#include <crucible/effects/Resources.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace crucible::effects {

// ── ConcurrentRow<Tags...> ──────────────────────────────────────────
//
// Carries a pack of ResourceTag instantiations.  No runtime state.
// All algebra is consteval-only via the trait family below.
//
// Concept-gated: every Tag MUST be a ResourceTag.  A non-resource
// type fails at template substitution, not at the eventual sum site.
template <ResourceTag... Tags>
struct ConcurrentRow {
    static constexpr std::size_t size = sizeof...(Tags);
};

using EmptyConcurrentRow = ConcurrentRow<>;

// ── Per-kind value lookup ───────────────────────────────────────────
//
// `concurrent_row_value_v<K, R>` is the sum of values for tags of
// kind K in R.  Absent kind → 0.  Present-once → that tag's value.
// Present-multiple (non-canonical row) → the arithmetic sum (silent
// merging of duplicate-kind entries).
//
// This is the building block for `concurrent_row_sum_t`: the result
// row contains, for each ResourceKind that appears in R1 or R2 with
// non-zero value, a single tag carrying the SUM of values from both
// rows.

template <ResourceKind K, typename R>
struct concurrent_row_value;

template <ResourceKind K>
struct concurrent_row_value<K, ConcurrentRow<>> {
    static constexpr std::uint64_t value = 0;
};

template <ResourceKind K, ResourceTag... Ts>
struct concurrent_row_value<K, ConcurrentRow<Ts...>> {
    static constexpr std::uint64_t value =
        ((Ts::kind == K ? Ts::value : std::uint64_t{0}) + ...);
};

template <ResourceKind K, typename R>
inline constexpr std::uint64_t concurrent_row_value_v =
    concurrent_row_value<K, R>::value;

// ── kind_to_tag<K, V> ───────────────────────────────────────────────
//
// Materializes a tag class for a given ResourceKind + budget value.
// 23 explicit specializations — mechanical, mirrors the
// resource_kind_name() switch in Resources.h.  Macro-expanded to
// keep the surface honest (every kind requires a paired tag, with
// the catalog name spelled out so a future renumbering or insertion
// fires here too).
namespace detail {

template <ResourceKind K, std::uint64_t V>
struct kind_to_tag;

#define CRUCIBLE_KIND_TO_TAG(KindEnum, TagName)                            \
    template <std::uint64_t V>                                             \
    struct kind_to_tag<ResourceKind::KindEnum, V> {                        \
        using type = ::crucible::effects::resource::TagName<V>;            \
    }

CRUCIBLE_KIND_TO_TAG(Sm,                SmBudget);
CRUCIBLE_KIND_TO_TAG(WarpScheduler,     WarpSchedulerSlots);
CRUCIBLE_KIND_TO_TAG(RegistersPerWarp,  RegistersPerWarp);
CRUCIBLE_KIND_TO_TAG(Smem,              SmemBytes);
CRUCIBLE_KIND_TO_TAG(L2,                L2Bytes);
CRUCIBLE_KIND_TO_TAG(HbmBytes,          HbmBytes);
CRUCIBLE_KIND_TO_TAG(HbmBw,             HbmBandwidth);
CRUCIBLE_KIND_TO_TAG(NvlinkBw,          NvlinkBandwidth);
CRUCIBLE_KIND_TO_TAG(PcieBw,            PcieBandwidth);
CRUCIBLE_KIND_TO_TAG(NicQ,              NicQueueBudget);
CRUCIBLE_KIND_TO_TAG(NicRing,           NicRingDepth);
CRUCIBLE_KIND_TO_TAG(NicQp,             NicQp);
CRUCIBLE_KIND_TO_TAG(NicCq,             NicCq);
CRUCIBLE_KIND_TO_TAG(NicMr,             NicMr);
CRUCIBLE_KIND_TO_TAG(SwitchEgressBw,    SwitchEgressBw);
CRUCIBLE_KIND_TO_TAG(SwitchBuffer,      SwitchBufferCells);
CRUCIBLE_KIND_TO_TAG(Tcam,              TcamEntries);
CRUCIBLE_KIND_TO_TAG(CpuCore,           CpuCoreBudget);
CRUCIBLE_KIND_TO_TAG(Llc,               LlcBytes);
CRUCIBLE_KIND_TO_TAG(PowerWatts,        PowerWatts);
CRUCIBLE_KIND_TO_TAG(ThermalCelsius,    ThermalCelsius);
CRUCIBLE_KIND_TO_TAG(RackPowerKw,       RackPowerKw);
CRUCIBLE_KIND_TO_TAG(CarbonGramsPerKwh, CarbonGramsPerKwh);

#undef CRUCIBLE_KIND_TO_TAG

template <ResourceKind K, std::uint64_t V>
using kind_to_tag_t = typename kind_to_tag<K, V>::type;

// ── No-overflow check ───────────────────────────────────────────────
//
// `A + B >= A` is the standard unsigned-overflow detection idiom.
// Holds iff the sum did not wrap.  Used by ConcurrentlySchedulable
// to reject schedules that would silently corrupt the budget total.
[[nodiscard]] consteval bool sum_does_not_overflow(std::uint64_t a,
                                                   std::uint64_t b) noexcept {
    return (a + b) >= a;
}

// ── Build canonical ConcurrentRow from per-kind sums ────────────────
//
// `build_canonical_row<R1, R2>::type` = ConcurrentRow holding, for
// each ResourceKind whose pairwise sum is non-zero, exactly one tag
// carrying that sum.  Kinds with zero sum are omitted to keep the
// canonical form stable — reflecting the "no row" sentinel for that
// kind.
//
// Implementation: for each of the 23 ResourceKind atoms we compute
// the pairwise sum and fold a `ConcurrentRow<...>` via
// std::conditional_t (kind sum > 0 ⇒ append tag; else skip).  The
// 23 atoms are visited in catalog order, giving deterministic output.

// concat — append a tag to a ConcurrentRow.
template <typename Tag, typename Row>
struct concurrent_row_prepend;

template <typename Tag, ResourceTag... Ts>
struct concurrent_row_prepend<Tag, ConcurrentRow<Ts...>> {
    using type = ConcurrentRow<Tag, Ts...>;
};

template <typename Tag, typename Row>
using concurrent_row_prepend_t =
    typename concurrent_row_prepend<Tag, Row>::type;

// Conditional emit for a single kind: if sum > 0, prepend the tag;
// else passthrough.  Cardinality stays bounded because every kind
// emits at most one tag.
template <ResourceKind K, std::uint64_t Sum, typename Row>
struct conditional_emit {
    using type = std::conditional_t<
        (Sum > 0),
        concurrent_row_prepend_t<kind_to_tag_t<K, Sum>, Row>,
        Row>;
};

template <ResourceKind K, std::uint64_t Sum, typename Row>
using conditional_emit_t = typename conditional_emit<K, Sum, Row>::type;

// Build canonical row via a 23-fold over ResourceKind atoms.  The
// atoms are visited in REVERSE catalog order so prepending yields a
// final pack in catalog order — matching resource_kind_name()'s
// discoverable iteration order.  Reflection-driven iteration would
// be cleaner but the 23 atoms are stable enough to spell out
// (correctness > clever; one fold per atom is unambiguous).
template <typename R1, typename R2>
struct build_canonical_row {
    using type =
        // Walk catalog from CarbonGramsPerKwh (22) down to Sm (0),
        // prepending non-zero sums.  Fold: start with EmptyRow,
        // accumulate one kind at a time.
        conditional_emit_t<ResourceKind::Sm,
            concurrent_row_value_v<ResourceKind::Sm, R1>
                + concurrent_row_value_v<ResourceKind::Sm, R2>,
        conditional_emit_t<ResourceKind::WarpScheduler,
            concurrent_row_value_v<ResourceKind::WarpScheduler, R1>
                + concurrent_row_value_v<ResourceKind::WarpScheduler, R2>,
        conditional_emit_t<ResourceKind::RegistersPerWarp,
            concurrent_row_value_v<ResourceKind::RegistersPerWarp, R1>
                + concurrent_row_value_v<ResourceKind::RegistersPerWarp, R2>,
        conditional_emit_t<ResourceKind::Smem,
            concurrent_row_value_v<ResourceKind::Smem, R1>
                + concurrent_row_value_v<ResourceKind::Smem, R2>,
        conditional_emit_t<ResourceKind::L2,
            concurrent_row_value_v<ResourceKind::L2, R1>
                + concurrent_row_value_v<ResourceKind::L2, R2>,
        conditional_emit_t<ResourceKind::HbmBytes,
            concurrent_row_value_v<ResourceKind::HbmBytes, R1>
                + concurrent_row_value_v<ResourceKind::HbmBytes, R2>,
        conditional_emit_t<ResourceKind::HbmBw,
            concurrent_row_value_v<ResourceKind::HbmBw, R1>
                + concurrent_row_value_v<ResourceKind::HbmBw, R2>,
        conditional_emit_t<ResourceKind::NvlinkBw,
            concurrent_row_value_v<ResourceKind::NvlinkBw, R1>
                + concurrent_row_value_v<ResourceKind::NvlinkBw, R2>,
        conditional_emit_t<ResourceKind::PcieBw,
            concurrent_row_value_v<ResourceKind::PcieBw, R1>
                + concurrent_row_value_v<ResourceKind::PcieBw, R2>,
        conditional_emit_t<ResourceKind::NicQ,
            concurrent_row_value_v<ResourceKind::NicQ, R1>
                + concurrent_row_value_v<ResourceKind::NicQ, R2>,
        conditional_emit_t<ResourceKind::NicRing,
            concurrent_row_value_v<ResourceKind::NicRing, R1>
                + concurrent_row_value_v<ResourceKind::NicRing, R2>,
        conditional_emit_t<ResourceKind::NicQp,
            concurrent_row_value_v<ResourceKind::NicQp, R1>
                + concurrent_row_value_v<ResourceKind::NicQp, R2>,
        conditional_emit_t<ResourceKind::NicCq,
            concurrent_row_value_v<ResourceKind::NicCq, R1>
                + concurrent_row_value_v<ResourceKind::NicCq, R2>,
        conditional_emit_t<ResourceKind::NicMr,
            concurrent_row_value_v<ResourceKind::NicMr, R1>
                + concurrent_row_value_v<ResourceKind::NicMr, R2>,
        conditional_emit_t<ResourceKind::SwitchEgressBw,
            concurrent_row_value_v<ResourceKind::SwitchEgressBw, R1>
                + concurrent_row_value_v<ResourceKind::SwitchEgressBw, R2>,
        conditional_emit_t<ResourceKind::SwitchBuffer,
            concurrent_row_value_v<ResourceKind::SwitchBuffer, R1>
                + concurrent_row_value_v<ResourceKind::SwitchBuffer, R2>,
        conditional_emit_t<ResourceKind::Tcam,
            concurrent_row_value_v<ResourceKind::Tcam, R1>
                + concurrent_row_value_v<ResourceKind::Tcam, R2>,
        conditional_emit_t<ResourceKind::CpuCore,
            concurrent_row_value_v<ResourceKind::CpuCore, R1>
                + concurrent_row_value_v<ResourceKind::CpuCore, R2>,
        conditional_emit_t<ResourceKind::Llc,
            concurrent_row_value_v<ResourceKind::Llc, R1>
                + concurrent_row_value_v<ResourceKind::Llc, R2>,
        conditional_emit_t<ResourceKind::PowerWatts,
            concurrent_row_value_v<ResourceKind::PowerWatts, R1>
                + concurrent_row_value_v<ResourceKind::PowerWatts, R2>,
        conditional_emit_t<ResourceKind::ThermalCelsius,
            concurrent_row_value_v<ResourceKind::ThermalCelsius, R1>
                + concurrent_row_value_v<ResourceKind::ThermalCelsius, R2>,
        conditional_emit_t<ResourceKind::RackPowerKw,
            concurrent_row_value_v<ResourceKind::RackPowerKw, R1>
                + concurrent_row_value_v<ResourceKind::RackPowerKw, R2>,
        conditional_emit_t<ResourceKind::CarbonGramsPerKwh,
            concurrent_row_value_v<ResourceKind::CarbonGramsPerKwh, R1>
                + concurrent_row_value_v<ResourceKind::CarbonGramsPerKwh, R2>,
        ConcurrentRow<>
        >>>>>>>>>>>>>>>>>>>>>>>;
};

}  // namespace detail

// ── concurrent_row_sum_t<R1, R2> ────────────────────────────────────
//
// The pairwise additive union: for every ResourceKind, the result
// row carries a single tag whose value is the sum of R1's and R2's
// contributions for that kind.  Kinds absent from both rows are
// omitted (canonical form).  Result is order-stable (catalog order
// of ResourceKind atoms) regardless of R1/R2 input ordering.
//
// Per the overflow discipline (see file head): users who need
// guaranteed-no-overflow composition should constrain on
// `ConcurrentlySchedulable` BEFORE asking for the sum type.
template <typename R1, typename R2>
using concurrent_row_sum_t =
    typename detail::build_canonical_row<R1, R2>::type;

// ── concurrent_row_n_t<Rs...> — variadic N-way fold ─────────────────
//
// Left-fold over `concurrent_row_sum_t`.  Order doesn't matter for
// the result (sum is commutative + associative on uint64_t modulo
// the overflow guard) but the fold direction is fixed for
// implementation determinism.
namespace detail {

template <typename... Rs>
struct concurrent_row_n;

template <>
struct concurrent_row_n<> {
    using type = ConcurrentRow<>;
};

template <typename R>
struct concurrent_row_n<R> {
    using type = R;
};

template <typename R1, typename R2, typename... Rest>
struct concurrent_row_n<R1, R2, Rest...> {
    using type = typename concurrent_row_n<
        concurrent_row_sum_t<R1, R2>,
        Rest...
    >::type;
};

}  // namespace detail

template <typename... Rs>
using concurrent_row_n_t = typename detail::concurrent_row_n<Rs...>::type;

// ── ConcurrentlySchedulable concept ─────────────────────────────────
//
// R1 and R2 are concurrently schedulable iff the pairwise sum on
// every kind does NOT overflow uint64_t.  Without this gate, an
// oversubscribed schedule (e.g., two ops each declaring HbmBytes
// near UINT64_MAX) would silently wrap and pass the FitsCog check.
//
// The check evaluates one no-overflow predicate per ResourceKind
// — bounded compile-time cost, no runtime trace.
//
// Note: this is a NECESSARY condition for valid concurrent
// scheduling but not sufficient.  GAPS-191 cog/FitsCog.h adds the
// `combined ≤ Cog::TargetCaps` check; together the two form the
// full compile-time budget verification.

namespace detail {

// Per-kind no-overflow guard.  Returns true iff the pairwise sum on
// kind K does not wrap.
template <ResourceKind K, typename R1, typename R2>
inline constexpr bool kind_no_overflow_v = sum_does_not_overflow(
    concurrent_row_value_v<K, R1>,
    concurrent_row_value_v<K, R2>);

// All-kinds no-overflow guard.  Folds && across the 23 ResourceKind
// atoms.  Catalog order matches resource_kind_name() — a future
// extension adds the new atom to BOTH switches.  The guard is
// reflection-checkable but spelled out for now to keep the surface
// transparent (a reflection-driven version is a future fold
// improvement; correctness comes first).
template <typename R1, typename R2>
inline constexpr bool concurrently_schedulable_v =
    kind_no_overflow_v<ResourceKind::Sm, R1, R2> &&
    kind_no_overflow_v<ResourceKind::WarpScheduler, R1, R2> &&
    kind_no_overflow_v<ResourceKind::RegistersPerWarp, R1, R2> &&
    kind_no_overflow_v<ResourceKind::Smem, R1, R2> &&
    kind_no_overflow_v<ResourceKind::L2, R1, R2> &&
    kind_no_overflow_v<ResourceKind::HbmBytes, R1, R2> &&
    kind_no_overflow_v<ResourceKind::HbmBw, R1, R2> &&
    kind_no_overflow_v<ResourceKind::NvlinkBw, R1, R2> &&
    kind_no_overflow_v<ResourceKind::PcieBw, R1, R2> &&
    kind_no_overflow_v<ResourceKind::NicQ, R1, R2> &&
    kind_no_overflow_v<ResourceKind::NicRing, R1, R2> &&
    kind_no_overflow_v<ResourceKind::NicQp, R1, R2> &&
    kind_no_overflow_v<ResourceKind::NicCq, R1, R2> &&
    kind_no_overflow_v<ResourceKind::NicMr, R1, R2> &&
    kind_no_overflow_v<ResourceKind::SwitchEgressBw, R1, R2> &&
    kind_no_overflow_v<ResourceKind::SwitchBuffer, R1, R2> &&
    kind_no_overflow_v<ResourceKind::Tcam, R1, R2> &&
    kind_no_overflow_v<ResourceKind::CpuCore, R1, R2> &&
    kind_no_overflow_v<ResourceKind::Llc, R1, R2> &&
    kind_no_overflow_v<ResourceKind::PowerWatts, R1, R2> &&
    kind_no_overflow_v<ResourceKind::ThermalCelsius, R1, R2> &&
    kind_no_overflow_v<ResourceKind::RackPowerKw, R1, R2> &&
    kind_no_overflow_v<ResourceKind::CarbonGramsPerKwh, R1, R2>;

}  // namespace detail

template <typename R1, typename R2>
concept ConcurrentlySchedulable =
    detail::concurrently_schedulable_v<R1, R2>;

// IsConcurrentRow concept — recognizes the carrier shape.  Useful
// for downstream generic algorithms (FitsCog) that need to dispatch
// on "is this a ConcurrentRow?".
namespace detail {

template <typename T>
struct is_concurrent_row : std::false_type {};

template <ResourceTag... Ts>
struct is_concurrent_row<ConcurrentRow<Ts...>> : std::true_type {};

}  // namespace detail

template <typename T>
inline constexpr bool is_concurrent_row_v =
    detail::is_concurrent_row<T>::value;

template <typename T>
concept IsConcurrentRow = is_concurrent_row_v<T>;

// ── Self-test block ─────────────────────────────────────────────────
namespace detail::concurrent_row_self_test {

// Empty row sums to itself.
static_assert(std::is_same_v<
    concurrent_row_sum_t<ConcurrentRow<>, ConcurrentRow<>>,
    ConcurrentRow<>>);

// Singleton row + empty = singleton row.
static_assert(std::is_same_v<
    concurrent_row_sum_t<ConcurrentRow<resource::SmBudget<32>>,
                        ConcurrentRow<>>,
    ConcurrentRow<resource::SmBudget<32>>>);

// Empty + singleton = singleton (commutativity check via swap).
static_assert(std::is_same_v<
    concurrent_row_sum_t<ConcurrentRow<>,
                        ConcurrentRow<resource::SmBudget<32>>>,
    ConcurrentRow<resource::SmBudget<32>>>);

// Same-kind summation: SmBudget<32> + SmBudget<64> = SmBudget<96>.
static_assert(std::is_same_v<
    concurrent_row_sum_t<ConcurrentRow<resource::SmBudget<32>>,
                        ConcurrentRow<resource::SmBudget<64>>>,
    ConcurrentRow<resource::SmBudget<96>>>);

// Cross-kind merging: SmBudget<32> ⊕ NicQp<4> = ConcurrentRow with
// both, in catalog order (Sm=0 before NicQp=11).
static_assert(std::is_same_v<
    concurrent_row_sum_t<ConcurrentRow<resource::SmBudget<32>>,
                        ConcurrentRow<resource::NicQp<4>>>,
    ConcurrentRow<resource::SmBudget<32>, resource::NicQp<4>>>);

// Mixed: same-kind summed, cross-kind merged.
static_assert(std::is_same_v<
    concurrent_row_sum_t<
        ConcurrentRow<resource::SmBudget<32>, resource::NicQp<4>>,
        ConcurrentRow<resource::SmBudget<64>, resource::NicQp<2>>>,
    ConcurrentRow<resource::SmBudget<96>, resource::NicQp<6>>>);

// Variadic N-way: 4-way same-kind sum.
static_assert(std::is_same_v<
    concurrent_row_n_t<
        ConcurrentRow<resource::SmBudget<10>>,
        ConcurrentRow<resource::SmBudget<20>>,
        ConcurrentRow<resource::SmBudget<30>>,
        ConcurrentRow<resource::SmBudget<40>>>,
    ConcurrentRow<resource::SmBudget<100>>>);

// Variadic N-way: zero rows = empty.
static_assert(std::is_same_v<
    concurrent_row_n_t<>,
    ConcurrentRow<>>);

// Variadic N-way: single row = identity.
static_assert(std::is_same_v<
    concurrent_row_n_t<ConcurrentRow<resource::SmBudget<32>>>,
    ConcurrentRow<resource::SmBudget<32>>>);

// Order independence: catalog order in result regardless of input
// order.  Sm (0) comes before NicQp (11) in both directions.
static_assert(std::is_same_v<
    concurrent_row_sum_t<
        ConcurrentRow<resource::NicQp<4>>,
        ConcurrentRow<resource::SmBudget<32>>>,
    ConcurrentRow<resource::SmBudget<32>, resource::NicQp<4>>>);

// Per-kind value lookup: present and absent kinds.
static_assert(concurrent_row_value_v<ResourceKind::Sm,
    ConcurrentRow<resource::SmBudget<32>>> == 32);
static_assert(concurrent_row_value_v<ResourceKind::NicQp,
    ConcurrentRow<resource::SmBudget<32>>> == 0);
static_assert(concurrent_row_value_v<ResourceKind::Sm,
    ConcurrentRow<>> == 0);

// Non-canonical input: same-kind duplicates sum at lookup time.
static_assert(concurrent_row_value_v<ResourceKind::Sm,
    ConcurrentRow<resource::SmBudget<10>,
                  resource::SmBudget<20>,
                  resource::SmBudget<30>>> == 60);

// concurrent_row_sum canonicalizes non-canonical input.  After
// summation, the result has at most one tag per kind.
static_assert(std::is_same_v<
    concurrent_row_sum_t<
        ConcurrentRow<resource::SmBudget<10>,
                      resource::SmBudget<20>>,
        ConcurrentRow<resource::SmBudget<30>>>,
    ConcurrentRow<resource::SmBudget<60>>>);

// ── ConcurrentlySchedulable: positive cases ────────────────────────
static_assert(ConcurrentlySchedulable<
    ConcurrentRow<resource::SmBudget<32>>,
    ConcurrentRow<resource::SmBudget<64>>>);

static_assert(ConcurrentlySchedulable<
    ConcurrentRow<resource::HbmBytes<40'000'000'000ULL>>,
    ConcurrentRow<resource::HbmBytes<40'000'000'000ULL>>>);

static_assert(ConcurrentlySchedulable<
    ConcurrentRow<>,
    ConcurrentRow<>>);

// ── ConcurrentlySchedulable: overflow rejection ────────────────────
//
// UINT64_MAX + 1 wraps to 0 — the canonical overflow case.  The
// concept must reject it.  Compile-time check: if the assertion
// below holds, the overflow guard is functioning.
static_assert(!ConcurrentlySchedulable<
    ConcurrentRow<resource::HbmBytes<UINT64_MAX>>,
    ConcurrentRow<resource::HbmBytes<1>>>);

// Cross-kind: overflow on one kind taints the entire pair.
static_assert(!ConcurrentlySchedulable<
    ConcurrentRow<resource::SmBudget<32>,
                  resource::HbmBytes<UINT64_MAX>>,
    ConcurrentRow<resource::SmBudget<64>,
                  resource::HbmBytes<1>>>);

// ── IsConcurrentRow concept ────────────────────────────────────────
static_assert(IsConcurrentRow<ConcurrentRow<>>);
static_assert(IsConcurrentRow<ConcurrentRow<resource::SmBudget<32>>>);
static_assert(!IsConcurrentRow<int>);
static_assert(!IsConcurrentRow<resource::SmBudget<32>>);

// ── Size invariant ─────────────────────────────────────────────────
//
// ConcurrentRow has zero runtime cost (empty struct) like
// effects::Row.
static_assert(std::is_empty_v<ConcurrentRow<>>);
static_assert(std::is_empty_v<ConcurrentRow<resource::SmBudget<32>>>);
static_assert(std::is_empty_v<
    ConcurrentRow<resource::SmBudget<32>, resource::NicQp<4>>>);

// ── Runtime smoke test ─────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline.md, every
// algebra/* and effects/* header MUST ship a runtime body that
// exercises constexpr/consteval functions with non-constant args.
// Pure static_assert masks consteval/SFINAE/inline-body bugs and
// the header-only blind spot (see
// feedback_header_only_static_assert_blind_spot.md) — the sentinel
// TU test/test_smoke_algebra_effects.cpp invokes this from main()
// under project warning flags.
inline void runtime_smoke_test() {
    ConcurrentRow<> empty{};
    ConcurrentRow<resource::SmBudget<32>> sm{};
    ConcurrentRow<resource::SmBudget<32>, resource::NicQp<4>> mixed{};
    [[maybe_unused]] auto empty_size = sizeof(empty);
    [[maybe_unused]] auto sm_size    = sizeof(sm);
    [[maybe_unused]] auto mixed_size = sizeof(mixed);
    [[maybe_unused]] bool is_empty_row = IsConcurrentRow<decltype(empty)>;
    [[maybe_unused]] bool is_sm_row    = IsConcurrentRow<decltype(sm)>;
    [[maybe_unused]] bool not_a_row    = IsConcurrentRow<int>;
}

}  // namespace detail::concurrent_row_self_test

}  // namespace crucible::effects

// ── A3-002: row_hash_contribution<ConcurrentRow<Tags...>> ───────────
//
// Co-located with the carrier per the "specialization next to
// declaration" discipline (A1-018).  Federation cache key contribution
// for the additive resource-budget carrier.  Without this every
// `ConcurrentRow<...>` instantiation falls through to the primary
// template and contributes 0; any wrapper-stack that embeds a
// ConcurrentRow as payload (`Computation<R, ConcurrentRow<...>>`,
// `Vendor<NV, ConcurrentRow<...>>`, etc.) would fold ConcurrentRow's
// content out of the federation cache slot identity, causing every
// ResourceTag-bearing kernel that differs ONLY in its declared
// budget to collide at the same federation slot.
//
// Hash discipline.  ConcurrentRow's semantic-equivalence rule
// (canonical-vs-non-canonical via `concurrent_row_sum_t`) says two
// rows are equivalent iff per-kind value sums match.  The hash must
// agree:
//
//   ConcurrentRow<SmBudget<10>, SmBudget<20>>   (non-canonical, sum=30)
//   ConcurrentRow<SmBudget<30>>                  (canonical, sum=30)
//
// must produce IDENTICAL hashes — otherwise canonical-vs-non-canonical
// drift fragments the federation cache the same way A3-001 fragmented
// Row<Es...> before its type-level canonicalization landed.  The
// implementation walks the 23 ResourceKind atoms in catalog order
// (Sm=0 .. CarbonGramsPerKwh=22), reads each kind's `concurrent_row
// _value_v` (the per-kind sum across all input tags of that kind),
// and emits a (kind, sum) hash for non-zero kinds only.  This is the
// fold over the CANONICAL form's content, not the raw template-arg
// pack, so the canonical/non-canonical equivalence drops out.
//
// Cardinality seed is the count of NON-ZERO kinds — analogous to
// Row<Es...>'s `unique_count_sorted` set-cardinality seed.  Empty
// ConcurrentRow yields its own distinct hash (cardinality 0 + the
// ConcurrentRow wrapper-tag salt) so it does not collide with bare
// EmptyRow.
namespace crucible::safety::diag {

template <::crucible::effects::ResourceTag... Tags>
struct row_hash_contribution<::crucible::effects::ConcurrentRow<Tags...>> {
    static constexpr std::uint64_t value = []() consteval -> std::uint64_t {
        using R = ::crucible::effects::ConcurrentRow<Tags...>;
        using K = ::crucible::effects::ResourceKind;
        namespace eff = ::crucible::effects;
        // Per-kind sums in catalog order (Sm=0 .. CarbonGramsPerKwh=22).
        // Spelled out explicitly to mirror the resource_kind_name()
        // switch in Resources.h — a new ResourceKind atom requires
        // adding both arms in lockstep (the cardinality assertion in
        // Resources.h fires when this list goes stale).
        constexpr std::array<std::uint64_t, 23> sums{
            eff::concurrent_row_value_v<K::Sm,                R>,
            eff::concurrent_row_value_v<K::WarpScheduler,     R>,
            eff::concurrent_row_value_v<K::RegistersPerWarp,  R>,
            eff::concurrent_row_value_v<K::Smem,              R>,
            eff::concurrent_row_value_v<K::L2,                R>,
            eff::concurrent_row_value_v<K::HbmBytes,          R>,
            eff::concurrent_row_value_v<K::HbmBw,             R>,
            eff::concurrent_row_value_v<K::NvlinkBw,          R>,
            eff::concurrent_row_value_v<K::PcieBw,            R>,
            eff::concurrent_row_value_v<K::NicQ,              R>,
            eff::concurrent_row_value_v<K::NicRing,           R>,
            eff::concurrent_row_value_v<K::NicQp,             R>,
            eff::concurrent_row_value_v<K::NicCq,             R>,
            eff::concurrent_row_value_v<K::NicMr,             R>,
            eff::concurrent_row_value_v<K::SwitchEgressBw,    R>,
            eff::concurrent_row_value_v<K::SwitchBuffer,      R>,
            eff::concurrent_row_value_v<K::Tcam,              R>,
            eff::concurrent_row_value_v<K::CpuCore,           R>,
            eff::concurrent_row_value_v<K::Llc,               R>,
            eff::concurrent_row_value_v<K::PowerWatts,        R>,
            eff::concurrent_row_value_v<K::ThermalCelsius,    R>,
            eff::concurrent_row_value_v<K::RackPowerKw,       R>,
            eff::concurrent_row_value_v<K::CarbonGramsPerKwh, R>,
        };
        // Semantic cardinality — number of non-zero kinds.
        std::size_t card = 0;
        for (std::uint64_t s : sums) {
            if (s > 0) ++card;
        }
        // Seed mixes the ConcurrentRow wrapper-tag salt with the
        // semantic cardinality.  Empty row yields a hash distinct
        // from both bare EmptyRow and the primary-template zero.
        std::uint64_t h = detail::combine_ids(
            detail::WRAPPER_CONCURRENT_ROW_TAG,
            static_cast<std::uint64_t>(card));
        // Fold (kind, sum) for non-zero kinds in catalog order.  Kind
        // bits are wrapper-tag-salted to keep them distinct from the
        // sum value space (mirrors the per-tag specialization's
        // salt | kind layout in Resources.h above).
        for (std::size_t k = 0; k < sums.size(); ++k) {
            if (sums[k] > 0) {
                h = detail::combine_ids(
                    h,
                    detail::combine_ids(
                        detail::WRAPPER_RESOURCE_TAG_TAG
                            | static_cast<std::uint64_t>(k),
                        sums[k]));
            }
        }
        return h;
    }();
};

// ── Self-test block — ConcurrentRow hash invariants (A3-002) ────────
namespace detail::row_hash_concurrent_row_self_test {

using ::crucible::effects::ConcurrentRow;
using ::crucible::effects::EmptyConcurrentRow;
using ::crucible::effects::resource::SmBudget;
using ::crucible::effects::resource::NicQp;
using ::crucible::effects::resource::HbmBytes;

// Empty ConcurrentRow hashes distinctly from the primary-template
// zero AND from a singleton row — A3-002 fixes the silent collapse.
static_assert(row_hash_contribution_v<EmptyConcurrentRow> != 0);
static_assert(row_hash_contribution_v<EmptyConcurrentRow>
           != row_hash_contribution_v<ConcurrentRow<SmBudget<32>>>);

// Single-kind rows differ from each other.  This is THE structural
// witness for A3-002's federation cache slot collision claim — pre-
// fix `ConcurrentRow<SmBudget<32>>` and `ConcurrentRow<NicQp<4>>`
// both hashed to 0; post-fix they hash distinctly.
static_assert(row_hash_contribution_v<ConcurrentRow<SmBudget<32>>>
           != row_hash_contribution_v<ConcurrentRow<NicQp<4>>>);
static_assert(row_hash_contribution_v<ConcurrentRow<SmBudget<32>>>
           != row_hash_contribution_v<ConcurrentRow<HbmBytes<32>>>);

// Same-kind, different N: the N-value drift propagates into the hash.
static_assert(row_hash_contribution_v<ConcurrentRow<SmBudget<32>>>
           != row_hash_contribution_v<ConcurrentRow<SmBudget<64>>>);

// ── Canonical-vs-non-canonical semantic equivalence ────────────────
//
// `ConcurrentRow<SmBudget<10>, SmBudget<20>>` (non-canonical, two
// tags of kind Sm) is semantically equivalent to
// `ConcurrentRow<SmBudget<30>>` (canonical, sum already collapsed) —
// `concurrent_row_value_v<Sm, ...>` returns 30 for both.  The hash
// MUST agree, otherwise canonical-form drift fragments the cache
// (the A3-001 hazard surfacing in ConcurrentRow form).
static_assert(row_hash_contribution_v<
        ConcurrentRow<SmBudget<10>, SmBudget<20>>>
           == row_hash_contribution_v<ConcurrentRow<SmBudget<30>>>);

// Three-way same-kind: SmBudget<10> + SmBudget<20> + SmBudget<30> ≡
// SmBudget<60>.
static_assert(row_hash_contribution_v<
        ConcurrentRow<SmBudget<10>, SmBudget<20>, SmBudget<30>>>
           == row_hash_contribution_v<ConcurrentRow<SmBudget<60>>>);

// ── Order independence ────────────────────────────────────────────
//
// `ConcurrentRow<A, B>` and `ConcurrentRow<B, A>` hash identically —
// the catalog-order walk in the fold above produces the same emit
// sequence regardless of input pack order.
static_assert(row_hash_contribution_v<
        ConcurrentRow<SmBudget<32>, NicQp<4>>>
           == row_hash_contribution_v<
        ConcurrentRow<NicQp<4>, SmBudget<32>>>);

// ── Distinctness from a bare ResourceTag ──────────────────────────
//
// `ConcurrentRow<SmBudget<32>>` (the carrier with one tag) and bare
// `SmBudget<32>` (just the tag) MUST hash distinctly — they are
// semantically different (carrier wraps, tag does not), and the
// federation cache must distinguish a one-tag carrier from the tag
// itself or two semantically-different sites would alias.  The
// ConcurrentRow wrapper-tag salt (WRAPPER_CONCURRENT_ROW_TAG, byte
// 0x11) keeps the two disjoint regardless of the inner fold result.
static_assert(row_hash_contribution_v<ConcurrentRow<SmBudget<32>>>
           != row_hash_contribution_v<SmBudget<32>>);

}  // namespace detail::row_hash_concurrent_row_self_test

}  // namespace crucible::safety::diag
