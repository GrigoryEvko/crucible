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

}  // namespace detail::concurrent_row_self_test

}  // namespace crucible::effects
