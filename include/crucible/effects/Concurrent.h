#pragma once

// Two ops scheduled together on one device add their demands, so
// combining their rows is arithmetic over magnitudes rather than the
// set union that combines rows of effect atoms.  Without a row that
// adds, the compiler sees each op's demand alone and never the
// combined load that actually contends on the silicon.
//
// "Concurrent" separates this from the sequential case, where the
// second op starts only once the first has released its budgets.  That
// case takes the maximum per axis rather than the sum, and has no
// operation here.
//
// Budgets are uint64_t and their sum wraps in silence.  A wrapped sum
// reads as a smaller demand than either operand, which would let an
// oversubscribed schedule pass a fitting check.  Every path that adds
// budgets therefore proves first that the addition does not wrap.

#include <crucible/effects/Resources.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <type_traits>

namespace crucible::effects {

// A row may name one axis more than once.  Nothing rejects such a row,
// and the sum below folds the duplicates away.
template <ResourceTag... Tags>
struct ConcurrentRow {
    static constexpr std::size_t size = sizeof...(Tags);
};

using EmptyConcurrentRow = ConcurrentRow<>;

namespace detail {

template <typename T>
struct is_concurrent_row : std::false_type {};

template <ResourceTag... Ts>
struct is_concurrent_row<ConcurrentRow<Ts...>> : std::true_type {};

}  // namespace detail

template <typename T>
inline constexpr bool is_concurrent_row_v = detail::is_concurrent_row<T>::value;

template <typename T>
concept IsConcurrentRow = is_concurrent_row_v<T>;

template <ResourceKind K, typename R>
struct concurrent_row_value;

template <ResourceKind K>
struct concurrent_row_value<K, ConcurrentRow<>> {
    static constexpr std::uint64_t value = 0;
};

template <ResourceKind K, ResourceTag... Ts>
struct concurrent_row_value<K, ConcurrentRow<Ts...>> {
    static constexpr std::uint64_t value = ((Ts::kind == K ? Ts::value : std::uint64_t{0}) + ...);
};

template <ResourceKind K, typename R>
inline constexpr std::uint64_t concurrent_row_value_v = concurrent_row_value<K, R>::value;

// One specialization per axis, spelled out rather than derived, so
// that a new axis has to be paired with its tag here before any sum
// over that axis can be built.
namespace detail {

template <ResourceKind K, std::uint64_t V>
struct kind_to_tag;

#define CRUCIBLE_KIND_TO_TAG(KindEnum, TagName)                 \
    template <std::uint64_t V>                                  \
    struct kind_to_tag<ResourceKind::KindEnum, V> {             \
        using type = ::crucible::effects::resource::TagName<V>; \
    }

CRUCIBLE_KIND_TO_TAG(Sm, SmBudget);
CRUCIBLE_KIND_TO_TAG(WarpScheduler, WarpSchedulerSlots);
CRUCIBLE_KIND_TO_TAG(RegistersPerWarp, RegistersPerWarp);
CRUCIBLE_KIND_TO_TAG(Smem, SmemBytes);
CRUCIBLE_KIND_TO_TAG(L2, L2Bytes);
CRUCIBLE_KIND_TO_TAG(HbmBytes, HbmBytes);
CRUCIBLE_KIND_TO_TAG(HbmBw, HbmBandwidth);
CRUCIBLE_KIND_TO_TAG(NvlinkBw, NvlinkBandwidth);
CRUCIBLE_KIND_TO_TAG(PcieBw, PcieBandwidth);
CRUCIBLE_KIND_TO_TAG(NicQ, NicQueueBudget);
CRUCIBLE_KIND_TO_TAG(NicRing, NicRingDepth);
CRUCIBLE_KIND_TO_TAG(NicQp, NicQp);
CRUCIBLE_KIND_TO_TAG(NicCq, NicCq);
CRUCIBLE_KIND_TO_TAG(NicMr, NicMr);
CRUCIBLE_KIND_TO_TAG(SwitchEgressBw, SwitchEgressBw);
CRUCIBLE_KIND_TO_TAG(SwitchBuffer, SwitchBufferCells);
CRUCIBLE_KIND_TO_TAG(Tcam, TcamEntries);
CRUCIBLE_KIND_TO_TAG(CpuCore, CpuCoreBudget);
CRUCIBLE_KIND_TO_TAG(Llc, LlcBytes);
CRUCIBLE_KIND_TO_TAG(PowerWatts, PowerWatts);
CRUCIBLE_KIND_TO_TAG(ThermalCelsius, ThermalCelsius);
CRUCIBLE_KIND_TO_TAG(RackPowerKw, RackPowerKw);
CRUCIBLE_KIND_TO_TAG(CarbonGramsPerKwh, CarbonGramsPerKwh);

#undef CRUCIBLE_KIND_TO_TAG

template <ResourceKind K, std::uint64_t V>
using kind_to_tag_t = typename kind_to_tag<K, V>::type;

// Unsigned addition wraps, so a sum that came out no larger than one
// of its operands is a sum that wrapped.
[[nodiscard]] consteval bool sum_does_not_overflow(std::uint64_t a, std::uint64_t b) noexcept { return (a + b) >= a; }

template <typename Tag, typename Row>
struct concurrent_row_prepend;

template <typename Tag, ResourceTag... Ts>
struct concurrent_row_prepend<Tag, ConcurrentRow<Ts...>> {
    using type = ConcurrentRow<Tag, Ts...>;
};

template <typename Tag, typename Row>
using concurrent_row_prepend_t = typename concurrent_row_prepend<Tag, Row>::type;

// An axis whose sum is zero contributes no tag, which is what keeps
// the result canonical: at most one tag per axis, and none for an axis
// neither input mentioned.
template <ResourceKind K, std::uint64_t Sum, typename Row>
struct conditional_emit {
    using type = std::conditional_t<(Sum > 0), concurrent_row_prepend_t<kind_to_tag_t<K, Sum>, Row>, Row>;
};

template <ResourceKind K, std::uint64_t Sum, typename Row>
using conditional_emit_t = typename conditional_emit<K, Sum, Row>::type;

// The nesting below reads outward from the first axis of the catalog,
// but it builds inward-out: the innermost emit runs first and each
// enclosing one prepends.  The last axis is therefore prepended first
// and the finished pack comes out in catalog order.
template <typename R1, typename R2>
struct build_canonical_row {
    using type = conditional_emit_t<
        ResourceKind::Sm, concurrent_row_value_v<ResourceKind::Sm, R1> + concurrent_row_value_v<ResourceKind::Sm, R2>,
        conditional_emit_t<
            ResourceKind::WarpScheduler,
            concurrent_row_value_v<ResourceKind::WarpScheduler, R1>
                + concurrent_row_value_v<ResourceKind::WarpScheduler, R2>,
            conditional_emit_t<
                ResourceKind::RegistersPerWarp,
                concurrent_row_value_v<ResourceKind::RegistersPerWarp, R1>
                    + concurrent_row_value_v<ResourceKind::RegistersPerWarp, R2>,
                conditional_emit_t<
                    ResourceKind::Smem,
                    concurrent_row_value_v<ResourceKind::Smem, R1> + concurrent_row_value_v<ResourceKind::Smem, R2>,
                    conditional_emit_t<
                        ResourceKind::L2,
                        concurrent_row_value_v<ResourceKind::L2, R1> + concurrent_row_value_v<ResourceKind::L2, R2>,
                        conditional_emit_t<
                            ResourceKind::HbmBytes,
                            concurrent_row_value_v<ResourceKind::HbmBytes, R1>
                                + concurrent_row_value_v<ResourceKind::HbmBytes, R2>,
                            conditional_emit_t<
                                ResourceKind::HbmBw,
                                concurrent_row_value_v<ResourceKind::HbmBw, R1>
                                    + concurrent_row_value_v<ResourceKind::HbmBw, R2>,
                                conditional_emit_t<
                                    ResourceKind::NvlinkBw,
                                    concurrent_row_value_v<ResourceKind::NvlinkBw, R1>
                                        + concurrent_row_value_v<ResourceKind::NvlinkBw, R2>,
                                    conditional_emit_t<
                                        ResourceKind::PcieBw,
                                        concurrent_row_value_v<ResourceKind::PcieBw, R1>
                                            + concurrent_row_value_v<ResourceKind::PcieBw, R2>,
                                        conditional_emit_t<
                                            ResourceKind::NicQ,
                                            concurrent_row_value_v<ResourceKind::NicQ, R1>
                                                + concurrent_row_value_v<ResourceKind::NicQ, R2>,
                                            conditional_emit_t<
                                                ResourceKind::NicRing,
                                                concurrent_row_value_v<ResourceKind::NicRing, R1>
                                                    + concurrent_row_value_v<ResourceKind::NicRing, R2>,
                                                conditional_emit_t<
                                                    ResourceKind::NicQp,
                                                    concurrent_row_value_v<ResourceKind::NicQp, R1>
                                                        + concurrent_row_value_v<ResourceKind::NicQp, R2>,
                                                    conditional_emit_t<
                                                        ResourceKind::NicCq,
                                                        concurrent_row_value_v<ResourceKind::NicCq, R1>
                                                            + concurrent_row_value_v<ResourceKind::NicCq, R2>,
                                                        conditional_emit_t<
                                                            ResourceKind::NicMr,
                                                            concurrent_row_value_v<ResourceKind::NicMr, R1>
                                                                + concurrent_row_value_v<ResourceKind::NicMr, R2>,
                                                            conditional_emit_t<
                                                                ResourceKind::SwitchEgressBw,
                                                                concurrent_row_value_v<ResourceKind::SwitchEgressBw, R1>
                                                                    + concurrent_row_value_v<
                                                                        ResourceKind::SwitchEgressBw, R2>,
                                                                conditional_emit_t<
                                                                    ResourceKind::SwitchBuffer,
                                                                    concurrent_row_value_v<ResourceKind::SwitchBuffer,
                                                                                           R1>
                                                                        + concurrent_row_value_v<
                                                                            ResourceKind::SwitchBuffer, R2>,
                                                                    conditional_emit_t<
                                                                        ResourceKind::Tcam,
                                                                        concurrent_row_value_v<ResourceKind::Tcam, R1>
                                                                            + concurrent_row_value_v<ResourceKind::Tcam,
                                                                                                     R2>,
                                                                        conditional_emit_t<
                                                                            ResourceKind::CpuCore,
                                                                            concurrent_row_value_v<
                                                                                ResourceKind::CpuCore, R1>
                                                                                + concurrent_row_value_v<
                                                                                    ResourceKind::CpuCore, R2>,
                                                                            conditional_emit_t<
                                                                                ResourceKind::Llc,
                                                                                concurrent_row_value_v<
                                                                                    ResourceKind::Llc, R1>
                                                                                    + concurrent_row_value_v<
                                                                                        ResourceKind::Llc, R2>,
                                                                                conditional_emit_t<
                                                                                    ResourceKind::PowerWatts,
                                                                                    concurrent_row_value_v<
                                                                                        ResourceKind::PowerWatts, R1>
                                                                                        + concurrent_row_value_v<
                                                                                            ResourceKind::PowerWatts,
                                                                                            R2>,
                                                                                    conditional_emit_t<
                                                                                        ResourceKind::ThermalCelsius,
                                                                                        concurrent_row_value_v<
                                                                                            ResourceKind::
                                                                                                ThermalCelsius,
                                                                                            R1>
                                                                                            + concurrent_row_value_v<
                                                                                                ResourceKind::
                                                                                                    ThermalCelsius,
                                                                                                R2>,
                                                                                        conditional_emit_t<
                                                                                            ResourceKind::RackPowerKw,
                                                                                            concurrent_row_value_v<
                                                                                                ResourceKind::
                                                                                                    RackPowerKw,
                                                                                                R1>
                                                                                                + concurrent_row_value_v<
                                                                                                    ResourceKind::
                                                                                                        RackPowerKw,
                                                                                                    R2>,
                                                                                            conditional_emit_t<
                                                                                                ResourceKind::
                                                                                                    CarbonGramsPerKwh,
                                                                                                concurrent_row_value_v<
                                                                                                    ResourceKind::
                                                                                                        CarbonGramsPerKwh,
                                                                                                    R1>
                                                                                                    + concurrent_row_value_v<
                                                                                                        ResourceKind::
                                                                                                            CarbonGramsPerKwh,
                                                                                                        R2>,
                                                                                                ConcurrentRow<>>>>>>>>>>>>>>>>>>>>>>>>;
};

}  // namespace detail

// The result is canonical and its order does not depend on the order
// of the two inputs.  Asking for this type does not itself prove the
// sums are safe: a caller that needs that guarantee constrains on
// ConcurrentlySchedulable first.
template <typename R1, typename R2>
using concurrent_row_sum_t = typename detail::build_canonical_row<R1, R2>::type;

// The fold direction is fixed even though the sum is commutative and
// associative, so that the same inputs always give the same type.
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
    using type = typename concurrent_row_n<concurrent_row_sum_t<R1, R2>, Rest...>::type;
};

}  // namespace detail

template <typename... Rs>
using concurrent_row_n_t = typename detail::concurrent_row_n<Rs...>::type;

// Guarding the sum of two rows is not enough, because the per-axis
// value of one row is itself a fold that can wrap.  A row naming one
// axis twice, with values that sum past the top of the range, reports
// that axis as zero.  A guard reading that zero then finds nothing
// wrong, and a fitting check reads a demand of zero against any
// ceiling.
//
// The trait below closes that hole: it walks one axis of one row and
// refuses if any partial sum wraps.
namespace detail {

template <ResourceKind K, typename R>
struct intra_row_kind_safe;

template <ResourceKind K>
struct intra_row_kind_safe<K, ConcurrentRow<>> {
    static constexpr bool value = true;
};

template <ResourceKind K, ResourceTag... Ts>
struct intra_row_kind_safe<K, ConcurrentRow<Ts...>> {
    static constexpr bool value = []() consteval -> bool {
        constexpr std::size_t N = sizeof...(Ts);
        constexpr std::array<ResourceKind, N> kinds{Ts::kind...};
        constexpr std::array<std::uint64_t, N> values{Ts::value...};
        std::uint64_t acc = 0;
        for (std::size_t i = 0; i < N; ++i) {
            if (kinds[i] == K) {
                if (!sum_does_not_overflow(acc, values[i])) {
                    return false;
                }
                acc += values[i];
            }
        }
        return true;
    }();
};

// Reading the axis set through reflection means a new axis extends
// this guard without an edit here.
template <typename R>
[[nodiscard]] consteval bool eval_intra_row_overflow_safe_() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^ResourceKind));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto ea : enumerators) {
        constexpr ResourceKind kind = [:ea:];
        if (!intra_row_kind_safe<kind, R>::value) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

template <typename R>
inline constexpr bool intra_row_overflow_safe_v = eval_intra_row_overflow_safe_<R>();

}  // namespace detail

template <typename R>
inline constexpr bool intra_row_overflow_safe_v = detail::intra_row_overflow_safe_v<R>;

template <typename R>
concept IntraRowOverflowSafe = IsConcurrentRow<R> && intra_row_overflow_safe_v<R>;

// A row is canonical when it names each axis at most once, and then
// its per-axis value is exactly the value written in its own pack.
// Such a row is safe from the wrapping above by construction, because
// there is nothing to fold.
//
// The gates that add rows do not demand canonical input, since summing
// non-canonical rows is a supported operation.  A caller that wants
// the stronger property asks for it through this concept.
namespace detail {

template <ResourceKind K, typename R>
struct kind_occurrence_count;

template <ResourceKind K>
struct kind_occurrence_count<K, ConcurrentRow<>> {
    static constexpr std::size_t value = 0;
};

template <ResourceKind K, ResourceTag... Ts>
struct kind_occurrence_count<K, ConcurrentRow<Ts...>> {
    static constexpr std::size_t value = ((Ts::kind == K ? std::size_t{1} : std::size_t{0}) + ... + std::size_t{0});
};

template <typename R>
[[nodiscard]] consteval bool eval_is_canonical_concurrent_row_() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^ResourceKind));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto ea : enumerators) {
        constexpr ResourceKind kind = [:ea:];
        if (kind_occurrence_count<kind, R>::value > 1) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

template <typename R>
inline constexpr bool is_canonical_concurrent_row_v = eval_is_canonical_concurrent_row_<R>();

}  // namespace detail

template <typename R>
inline constexpr bool is_canonical_concurrent_row_v = detail::is_canonical_concurrent_row_v<R>;

template <typename R>
concept IsCanonicalConcurrentRow = IsConcurrentRow<R> && is_canonical_concurrent_row_v<R>;

// Two rows are schedulable together when each is safe on its own and
// their sum is safe on every axis.  The first half has to come first:
// without it the per-axis lookups the second half reads could already
// have wrapped.
//
// This is necessary and not sufficient.  A separate check compares the
// combined demand against what the hardware offers.
namespace detail {

template <ResourceKind K, typename R1, typename R2>
inline constexpr bool kind_no_overflow_v =
    sum_does_not_overflow(concurrent_row_value_v<K, R1>, concurrent_row_value_v<K, R2>);

// Reading the axis set through reflection means a new axis extends
// this guard without an edit here.  A hand-written conjunction would
// weaken the guard the moment someone forgot a line.
template <typename R1, typename R2>
[[nodiscard]] consteval bool eval_concurrently_schedulable_() noexcept {
    // The local must be static.  An expansion statement needs its
    // operand to be a constant expression, and a non-static constexpr
    // local has a per-invocation address, which is not one.
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^ResourceKind));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto ea : enumerators) {
        constexpr ResourceKind kind = [:ea:];
        if (!kind_no_overflow_v<kind, R1, R2>) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

template <typename R1, typename R2>
inline constexpr bool concurrently_schedulable_v = eval_concurrently_schedulable_<R1, R2>();

}  // namespace detail

template <typename R1, typename R2>
concept ConcurrentlySchedulable = detail::intra_row_overflow_safe_v<R1> && detail::intra_row_overflow_safe_v<R2>
                               && detail::concurrently_schedulable_v<R1, R2>;

// One descriptor per tag, for code that walks a row at runtime without
// knowing the tag types.  The array holds the tags in the order the
// row names them and does not re-sort, so a hand-written row keeps its
// own order while a summed row is already in catalog order.
template <typename R>
struct concurrent_row_descriptors;

template <ResourceTag... Ts>
struct concurrent_row_descriptors<ConcurrentRow<Ts...>> {
    static constexpr std::array<ResourceTagDescriptor, sizeof...(Ts)> value{
        ResourceTagDescriptor{Ts::kind, Ts::value, Ts::name}...};
};

template <typename R>
inline constexpr auto concurrent_row_descriptors_v = concurrent_row_descriptors<R>::value;

namespace detail::concurrent_row_self_test {

static_assert(std::is_same_v<concurrent_row_sum_t<ConcurrentRow<>, ConcurrentRow<>>, ConcurrentRow<>>);

static_assert(std::is_same_v<concurrent_row_sum_t<ConcurrentRow<resource::SmBudget<32>>, ConcurrentRow<>>,
                             ConcurrentRow<resource::SmBudget<32>>>);

static_assert(std::is_same_v<concurrent_row_sum_t<ConcurrentRow<>, ConcurrentRow<resource::SmBudget<32>>>,
                             ConcurrentRow<resource::SmBudget<32>>>);

static_assert(
    std::is_same_v<concurrent_row_sum_t<ConcurrentRow<resource::SmBudget<32>>, ConcurrentRow<resource::SmBudget<64>>>,
                   ConcurrentRow<resource::SmBudget<96>>>);

// The two axes come out in catalog order, which puts the compute axis
// before the network one.
static_assert(
    std::is_same_v<concurrent_row_sum_t<ConcurrentRow<resource::SmBudget<32>>, ConcurrentRow<resource::NicQp<4>>>,
                   ConcurrentRow<resource::SmBudget<32>, resource::NicQp<4>>>);

static_assert(std::is_same_v<concurrent_row_sum_t<ConcurrentRow<resource::SmBudget<32>, resource::NicQp<4>>,
                                                  ConcurrentRow<resource::SmBudget<64>, resource::NicQp<2>>>,
                             ConcurrentRow<resource::SmBudget<96>, resource::NicQp<6>>>);

static_assert(
    std::is_same_v<concurrent_row_n_t<ConcurrentRow<resource::SmBudget<10>>, ConcurrentRow<resource::SmBudget<20>>,
                                      ConcurrentRow<resource::SmBudget<30>>, ConcurrentRow<resource::SmBudget<40>>>,
                   ConcurrentRow<resource::SmBudget<100>>>);

static_assert(std::is_same_v<concurrent_row_n_t<>, ConcurrentRow<>>);

static_assert(
    std::is_same_v<concurrent_row_n_t<ConcurrentRow<resource::SmBudget<32>>>, ConcurrentRow<resource::SmBudget<32>>>);

// Swapping the inputs of the earlier cross-axis case gives the same
// result type.
static_assert(
    std::is_same_v<concurrent_row_sum_t<ConcurrentRow<resource::NicQp<4>>, ConcurrentRow<resource::SmBudget<32>>>,
                   ConcurrentRow<resource::SmBudget<32>, resource::NicQp<4>>>);

static_assert(concurrent_row_value_v<ResourceKind::Sm, ConcurrentRow<resource::SmBudget<32>>> == 32);
static_assert(concurrent_row_value_v<ResourceKind::NicQp, ConcurrentRow<resource::SmBudget<32>>> == 0);
static_assert(concurrent_row_value_v<ResourceKind::Sm, ConcurrentRow<>> == 0);

static_assert(
    concurrent_row_value_v<ResourceKind::Sm,
                           ConcurrentRow<resource::SmBudget<10>, resource::SmBudget<20>, resource::SmBudget<30>>>
    == 60);

static_assert(std::is_same_v<concurrent_row_sum_t<ConcurrentRow<resource::SmBudget<10>, resource::SmBudget<20>>,
                                                  ConcurrentRow<resource::SmBudget<30>>>,
                             ConcurrentRow<resource::SmBudget<60>>>);

static_assert(ConcurrentlySchedulable<ConcurrentRow<resource::SmBudget<32>>, ConcurrentRow<resource::SmBudget<64>>>);

static_assert(ConcurrentlySchedulable<ConcurrentRow<resource::HbmBytes<40'000'000'000ULL>>,
                                      ConcurrentRow<resource::HbmBytes<40'000'000'000ULL>>>);

static_assert(ConcurrentlySchedulable<ConcurrentRow<>, ConcurrentRow<>>);

static_assert(
    !ConcurrentlySchedulable<ConcurrentRow<resource::HbmBytes<UINT64_MAX>>, ConcurrentRow<resource::HbmBytes<1>>>);

// One wrapping axis is enough to reject the pair.
static_assert(!ConcurrentlySchedulable<ConcurrentRow<resource::SmBudget<32>, resource::HbmBytes<UINT64_MAX>>,
                                       ConcurrentRow<resource::SmBudget<64>, resource::HbmBytes<1>>>);

static_assert(ConcurrentlySchedulable<ConcurrentRow<>, ConcurrentRow<>>);

static_assert(ConcurrentlySchedulable<ConcurrentRow<resource::SmBudget<1>>, ConcurrentRow<>>);

// The carbon axis is the last in the catalog.  Overflowing it shows
// the walk reaches the end and does not stop at some earlier prefix.
static_assert(!ConcurrentlySchedulable<ConcurrentRow<resource::CarbonGramsPerKwh<UINT64_MAX>>,
                                       ConcurrentRow<resource::CarbonGramsPerKwh<1>>>);

static_assert(intra_row_overflow_safe_v<ConcurrentRow<>>);

// A row naming an axis once cannot wrap, whatever the value, because
// the fold starts from zero.
static_assert(intra_row_overflow_safe_v<ConcurrentRow<resource::SmBudget<UINT64_MAX>>>);

static_assert(intra_row_overflow_safe_v<ConcurrentRow<resource::SmBudget<10>, resource::SmBudget<20>>>);

static_assert(
    intra_row_overflow_safe_v<ConcurrentRow<resource::SmBudget<10>, resource::SmBudget<20>, resource::SmBudget<30>>>);

// This is the row that would otherwise report a demand of zero.
static_assert(!intra_row_overflow_safe_v<ConcurrentRow<resource::SmBudget<UINT64_MAX>, resource::SmBudget<1>>>);

// One wrapping axis is enough to reject the row.
static_assert(!intra_row_overflow_safe_v<
              ConcurrentRow<resource::SmBudget<32>, resource::HbmBytes<UINT64_MAX>, resource::HbmBytes<1>>>);

// The last axis of the catalog again, for the per-row walk.
static_assert(
    !intra_row_overflow_safe_v<ConcurrentRow<resource::CarbonGramsPerKwh<UINT64_MAX>, resource::CarbonGramsPerKwh<1>>>);

// Here the wrap happens at the second of three additions and the third
// value would bring the running total back down.  Only a check on
// every partial sum catches it.
static_assert(!intra_row_overflow_safe_v<
              ConcurrentRow<resource::SmBudget<UINT64_MAX - 10>, resource::SmBudget<20>, resource::SmBudget<1>>>);

static_assert(IntraRowOverflowSafe<ConcurrentRow<>>);
static_assert(IntraRowOverflowSafe<ConcurrentRow<resource::SmBudget<10>, resource::SmBudget<20>>>);
static_assert(!IntraRowOverflowSafe<ConcurrentRow<resource::SmBudget<UINT64_MAX>, resource::SmBudget<1>>>);
static_assert(!IntraRowOverflowSafe<int>);

// A row that wraps on its own is rejected before the pairwise check
// reads its zero, whichever side it sits on.
static_assert(
    !ConcurrentlySchedulable<ConcurrentRow<resource::SmBudget<UINT64_MAX>, resource::SmBudget<1>>, ConcurrentRow<>>);

static_assert(
    !ConcurrentlySchedulable<ConcurrentRow<>, ConcurrentRow<resource::SmBudget<UINT64_MAX>, resource::SmBudget<1>>>);

static_assert(!ConcurrentlySchedulable<ConcurrentRow<resource::SmBudget<UINT64_MAX>, resource::SmBudget<1>>,
                                       ConcurrentRow<resource::HbmBytes<UINT64_MAX>, resource::HbmBytes<1>>>);

static_assert(is_canonical_concurrent_row_v<ConcurrentRow<>>);

static_assert(is_canonical_concurrent_row_v<ConcurrentRow<resource::SmBudget<32>>>);

static_assert(is_canonical_concurrent_row_v<ConcurrentRow<resource::SmBudget<32>, resource::NicQp<4>>>);

// Order does not bear on it.  Naming an axis twice does.
static_assert(
    is_canonical_concurrent_row_v<ConcurrentRow<resource::NicQp<4>, resource::SmBudget<32>, resource::HbmBytes<1024>>>);

static_assert(!is_canonical_concurrent_row_v<ConcurrentRow<resource::SmBudget<10>, resource::SmBudget<20>>>);

static_assert(!is_canonical_concurrent_row_v<
              ConcurrentRow<resource::SmBudget<10>, resource::SmBudget<20>, resource::SmBudget<30>>>);

static_assert(
    !is_canonical_concurrent_row_v<ConcurrentRow<resource::SmBudget<32>, resource::NicQp<4>, resource::SmBudget<64>>>);

// The last axis of the catalog again, for the duplicate walk.
static_assert(
    !is_canonical_concurrent_row_v<ConcurrentRow<resource::CarbonGramsPerKwh<10>, resource::CarbonGramsPerKwh<20>>>);

static_assert(
    IsCanonicalConcurrentRow<concurrent_row_sum_t<ConcurrentRow<resource::SmBudget<10>, resource::SmBudget<20>>,
                                                  ConcurrentRow<resource::SmBudget<30>>>>);

static_assert(
    IsCanonicalConcurrentRow<concurrent_row_sum_t<ConcurrentRow<resource::SmBudget<32>, resource::NicQp<4>>,
                                                  ConcurrentRow<resource::SmBudget<64>, resource::NicQp<2>>>>);

static_assert(IsCanonicalConcurrentRow<ConcurrentRow<>>);
static_assert(IsCanonicalConcurrentRow<ConcurrentRow<resource::SmBudget<32>>>);
static_assert(!IsCanonicalConcurrentRow<int>);
static_assert(!IsCanonicalConcurrentRow<ConcurrentRow<resource::SmBudget<10>, resource::SmBudget<20>>>);

// Every canonical row above is also safe on its own, since a fold over
// one value cannot wrap.
static_assert(IntraRowOverflowSafe<ConcurrentRow<resource::SmBudget<32>, resource::NicQp<4>>>);

static_assert(IsConcurrentRow<ConcurrentRow<>>);
static_assert(IsConcurrentRow<ConcurrentRow<resource::SmBudget<32>>>);
static_assert(!IsConcurrentRow<int>);
static_assert(!IsConcurrentRow<resource::SmBudget<32>>);

static_assert(concurrent_row_descriptors_v<ConcurrentRow<>>.size() == 0);

static_assert(concurrent_row_descriptors_v<ConcurrentRow<resource::SmBudget<32>>>.size() == 1);
static_assert(concurrent_row_descriptors_v<ConcurrentRow<resource::SmBudget<32>>>[0].kind == ResourceKind::Sm);
static_assert(concurrent_row_descriptors_v<ConcurrentRow<resource::SmBudget<32>>>[0].value == 32);

static_assert(concurrent_row_descriptors_v<ConcurrentRow<resource::SmBudget<32>, resource::NicQp<4>>>.size() == 2);
static_assert(concurrent_row_descriptors_v<ConcurrentRow<resource::SmBudget<32>, resource::NicQp<4>>>[0].kind
              == ResourceKind::Sm);
static_assert(concurrent_row_descriptors_v<ConcurrentRow<resource::SmBudget<32>, resource::NicQp<4>>>[1].kind
              == ResourceKind::NicQp);
static_assert(concurrent_row_descriptors_v<ConcurrentRow<resource::SmBudget<32>, resource::NicQp<4>>>[1].value == 4);

// The row below names its axes out of catalog order, and the
// descriptors keep that order.
static_assert(concurrent_row_descriptors_v<ConcurrentRow<resource::NicQp<4>, resource::SmBudget<32>>>[0].kind
              == ResourceKind::NicQp);
static_assert(concurrent_row_descriptors_v<ConcurrentRow<resource::NicQp<4>, resource::SmBudget<32>>>[1].kind
              == ResourceKind::Sm);

static_assert(std::is_empty_v<ConcurrentRow<>>);
static_assert(std::is_empty_v<ConcurrentRow<resource::SmBudget<32>>>);
static_assert(std::is_empty_v<ConcurrentRow<resource::SmBudget<32>, resource::NicQp<4>>>);

// Every operation is driven here with non-constant arguments.  The
// static_assert wall above only proves the constant-evaluated path,
// and a header whose bodies are never instantiated in a translation
// unit is never checked against the project warning flags at all.
inline void runtime_smoke_test() {
    ConcurrentRow<> empty{};
    ConcurrentRow<resource::SmBudget<32>> sm{};
    ConcurrentRow<resource::SmBudget<32>, resource::NicQp<4>> mixed{};
    [[maybe_unused]] auto empty_size = sizeof(empty);
    [[maybe_unused]] auto sm_size = sizeof(sm);
    [[maybe_unused]] auto mixed_size = sizeof(mixed);
    [[maybe_unused]] bool is_empty_row = IsConcurrentRow<decltype(empty)>;
    [[maybe_unused]] bool is_sm_row = IsConcurrentRow<decltype(sm)>;
    [[maybe_unused]] bool not_a_row = IsConcurrentRow<int>;

    // The loop reads every field so that none of them is stripped
    // before the accessor paths have been compiled.
    std::uint64_t value_sum = 0;
    for (auto const& desc : concurrent_row_descriptors_v<decltype(mixed)>) {
        value_sum += desc.value;
        if (desc.kind == ResourceKind::Sm) {
            [[maybe_unused]] auto n = desc.name;
        }
    }
    [[maybe_unused]] auto witness_sum = value_sum;
}

}  // namespace detail::concurrent_row_self_test

}  // namespace crucible::effects

// Without this specialization every row falls through to the primary
// template and contributes nothing, so two wrapper stacks that differ
// only in their declared budgets would share a federation cache key.
//
// Two rows are equivalent when their per-axis sums agree, whether or
// not either row names an axis twice.  The hash has to agree with
// that, so the fold below reads per-axis sums in catalog order rather
// than the tags as written, and emits only the axes with a non-zero
// sum.  Anything else would split the cache between two spellings of
// the same demand.
//
// The seed counts the non-zero axes, so an empty row hashes to
// something of its own rather than to the primary template's zero.
namespace crucible::safety::diag {

template <::crucible::effects::ResourceTag... Tags>
struct row_hash_contribution<::crucible::effects::ConcurrentRow<Tags...>> {
    static constexpr std::uint64_t value = []() consteval -> std::uint64_t {
        using R = ::crucible::effects::ConcurrentRow<Tags...>;
        using K = ::crucible::effects::ResourceKind;
        namespace eff = ::crucible::effects;
        // The axes are listed in catalog order and spelled out, so a
        // new axis has to be added here too.  The cardinality pin at
        // the catalog fires when this list goes stale.
        constexpr std::array<std::uint64_t, 23> sums{
            eff::concurrent_row_value_v<K::Sm, R>,
            eff::concurrent_row_value_v<K::WarpScheduler, R>,
            eff::concurrent_row_value_v<K::RegistersPerWarp, R>,
            eff::concurrent_row_value_v<K::Smem, R>,
            eff::concurrent_row_value_v<K::L2, R>,
            eff::concurrent_row_value_v<K::HbmBytes, R>,
            eff::concurrent_row_value_v<K::HbmBw, R>,
            eff::concurrent_row_value_v<K::NvlinkBw, R>,
            eff::concurrent_row_value_v<K::PcieBw, R>,
            eff::concurrent_row_value_v<K::NicQ, R>,
            eff::concurrent_row_value_v<K::NicRing, R>,
            eff::concurrent_row_value_v<K::NicQp, R>,
            eff::concurrent_row_value_v<K::NicCq, R>,
            eff::concurrent_row_value_v<K::NicMr, R>,
            eff::concurrent_row_value_v<K::SwitchEgressBw, R>,
            eff::concurrent_row_value_v<K::SwitchBuffer, R>,
            eff::concurrent_row_value_v<K::Tcam, R>,
            eff::concurrent_row_value_v<K::CpuCore, R>,
            eff::concurrent_row_value_v<K::Llc, R>,
            eff::concurrent_row_value_v<K::PowerWatts, R>,
            eff::concurrent_row_value_v<K::ThermalCelsius, R>,
            eff::concurrent_row_value_v<K::RackPowerKw, R>,
            eff::concurrent_row_value_v<K::CarbonGramsPerKwh, R>,
        };
        std::size_t card = 0;
        for (std::uint64_t s : sums) {
            if (s > 0) ++card;
        }
        std::uint64_t h = detail::combine_ids(detail::WRAPPER_CONCURRENT_ROW_TAG, static_cast<std::uint64_t>(card));
        // The axis index is salted before it is mixed, which keeps the
        // axis bits out of the value space of the sums.
        for (std::size_t k = 0; k < sums.size(); ++k) {
            if (sums[k] > 0) {
                h = detail::combine_ids(
                    h, detail::combine_ids(detail::WRAPPER_RESOURCE_TAG_TAG | static_cast<std::uint64_t>(k), sums[k]));
            }
        }
        return h;
    }();
};

namespace detail::row_hash_concurrent_row_self_test {

using ::crucible::effects::ConcurrentRow;
using ::crucible::effects::EmptyConcurrentRow;
using ::crucible::effects::resource::SmBudget;
using ::crucible::effects::resource::NicQp;
using ::crucible::effects::resource::HbmBytes;

static_assert(row_hash_contribution_v<EmptyConcurrentRow> != 0);
static_assert(row_hash_contribution_v<EmptyConcurrentRow> != row_hash_contribution_v<ConcurrentRow<SmBudget<32>>>);

static_assert(row_hash_contribution_v<ConcurrentRow<SmBudget<32>>> != row_hash_contribution_v<ConcurrentRow<NicQp<4>>>);
static_assert(row_hash_contribution_v<ConcurrentRow<SmBudget<32>>>
              != row_hash_contribution_v<ConcurrentRow<HbmBytes<32>>>);

static_assert(row_hash_contribution_v<ConcurrentRow<SmBudget<32>>>
              != row_hash_contribution_v<ConcurrentRow<SmBudget<64>>>);

// The two rows below declare the same demand in two spellings, so
// they must land in the same cache slot.
static_assert(row_hash_contribution_v<ConcurrentRow<SmBudget<10>, SmBudget<20>>>
              == row_hash_contribution_v<ConcurrentRow<SmBudget<30>>>);

static_assert(row_hash_contribution_v<ConcurrentRow<SmBudget<10>, SmBudget<20>, SmBudget<30>>>
              == row_hash_contribution_v<ConcurrentRow<SmBudget<60>>>);

// Naming the axes in the other order is a third spelling of one
// demand.
static_assert(row_hash_contribution_v<ConcurrentRow<SmBudget<32>, NicQp<4>>>
              == row_hash_contribution_v<ConcurrentRow<NicQp<4>, SmBudget<32>>>);

// A row holding one tag and that tag on its own are different things,
// and the salt in the seed keeps them apart whatever the inner fold
// produces.
static_assert(row_hash_contribution_v<ConcurrentRow<SmBudget<32>>> != row_hash_contribution_v<SmBudget<32>>);

}  // namespace detail::row_hash_concurrent_row_self_test

}  // namespace crucible::safety::diag
