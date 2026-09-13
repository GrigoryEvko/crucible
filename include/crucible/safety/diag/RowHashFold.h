#pragma once

// A cache key pairs a content hash with the row hash computed here. Two
// computations whose content hashes match but whose rows differ are the
// same work under different effect regimes, and they must land in
// different slots. A pure-row kernel is safe to share between
// installations. An IO-row kernel is not. One slot for both silently
// breaks that.
//
// Four properties hold, and consumers depend on all four.
//
// A row is a set of effect atoms, so the hash is invariant under
// permutation of the pack and under repeated atoms. Sorting and dedup
// before the fold buy that. Without them two spellings of one row would
// address two slots and fragment the cache.
//
// Cardinality participates, because a longer row is a strictly stronger
// capability claim than its prefix.
//
// A bare type contributes zero. That value means "no row" and matches
// the default a cache key is born with.
//
// The empty row is not zero. It is a real row that happens to carry no
// effects, so it is seeded from the hash offset basis instead. Were it
// zero, a computation carrying an empty row would alias its own bare
// payload, and those are different things.
//
// Portability bound. These hashes agree only within one compiler,
// standard library and ABI. Salts, enum values, non-type template
// arguments and the hash arithmetic are all portable, but a handful of
// wrapper kinds fold in a type id derived from a reflected name, and
// that name is implementation-specific. Two peers on different
// toolchains can compute different hashes for one type, or collide two
// types onto one slot if their ids happen to coincide. The bare hash is
// therefore a safe join key only among peers on one toolchain. Peers
// that are not must key through federation_key_with_toolchain, which
// makes the two toolchains disjoint by construction. The toolchain is
// deliberately kept out of row_hash_contribution itself, because folding
// it in would move every hash already published.

#include <crucible/Expr.h>
#include <crucible/Platform.h>
#include <crucible/Types.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/safety/diag/StableName.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// Everything below is forward-declared rather than included. A partial
// specialization only has to deduce its template parameters, and pulling
// the definitions in would drag the graded substrate, the whole lattice
// family and the reflection paths into every consumer of this header. A
// translation unit that names one of these types as an argument includes
// its own header for it.
namespace crucible::effects {
template <typename R, typename T>
class Computation;
}  // namespace crucible::effects

namespace crucible::algebra::lattices {
enum class AllocClassTag : std::uint8_t;
enum class CipherTierTag : std::uint8_t;
enum class Consistency : std::uint8_t;
enum class CrashClass : std::uint8_t;
enum class DetSafeTier : std::uint8_t;
enum class HotPathTier : std::uint8_t;
enum class Lifetime : std::uint8_t;
enum class MemOrderTag : std::uint8_t;
enum class ProgressClass : std::uint8_t;
enum class ResidencyHeatTag : std::uint8_t;
enum class Tolerance : std::uint8_t;
enum class VendorBackend : std::uint8_t;
enum class WaitStrategy : std::uint8_t;
enum class Witness : std::uint8_t;
enum class JoinPolicy : std::uint8_t;
enum class FpRounding : std::uint8_t;
enum class FpFtz : std::uint8_t;
enum class FpContract : std::uint8_t;
enum class FpTrapMask : std::uint8_t;
enum class FpDenormalInput : std::uint8_t;
enum class FpNanPolicy : std::uint8_t;
enum class FpInfPolicy : std::uint8_t;
enum class FpComplexLayout : std::uint8_t;
enum class FpLibmPolicy : std::uint8_t;
enum class FpReassociate : std::uint8_t;
enum class FpConstantRounding : std::uint8_t;
enum class HwInstruction : std::uint8_t;
enum class BarrierStrength : std::uint8_t;
enum class SimdIsa : std::uint8_t;
enum class MemoryScope : std::uint8_t;
enum class ClockSource : std::uint8_t;
enum class SchedulerPolicy : std::uint8_t;
enum class SuspendBehavior : std::uint8_t;
}  // namespace crucible::algebra::lattices

namespace crucible::safety {
template <algebra::lattices::AllocClassTag Tag, typename T>
class AllocClass;
template <algebra::lattices::CipherTierTag Tier, typename T>
class CipherTier;
template <algebra::lattices::DetSafeTier Tier, typename T>
class DetSafe;
template <algebra::lattices::HotPathTier Tier, typename T>
class HotPath;
template <algebra::lattices::HwInstruction Tier, typename T>
class Hw;
template <algebra::lattices::BarrierStrength Tier, typename T>
class BarrierGuarded;
template <algebra::lattices::SimdIsa W, typename T>
class SimdWidthPinned;
template <algebra::lattices::MemoryScope S, typename T>
class ScopedFence;
template <algebra::lattices::ClockSource Source, typename T>
class ClockSource;
template <algebra::lattices::SchedulerPolicy Policy, typename T, std::uint64_t RuntimeNs, std::uint64_t DeadlineNs,
          std::uint64_t PeriodNs>
class SchedClass;
template <algebra::lattices::SuspendBehavior Behavior, typename T>
class SuspendBehavior;
template <algebra::lattices::MemOrderTag Tag, typename T>
class MemOrder;
template <algebra::lattices::ProgressClass Class, typename T>
class Progress;
template <algebra::lattices::ResidencyHeatTag Tier, typename T>
class ResidencyHeat;
template <algebra::lattices::Tolerance Tier, typename T>
class NumericalTier;
template <algebra::lattices::VendorBackend Backend, typename T>
class Vendor;
template <algebra::lattices::WaitStrategy Strategy, typename T>
class Wait;
template <typename T>
class Linear;
template <auto Pred, typename T>
class Refined;
template <typename T>
class Secret;
template <typename T>
class Stale;
template <typename T, typename Tag>
class Tagged;
template <auto Pred, typename T>
class SealedRefined;
template <typename T, std::size_t N, typename Tag>
class TimeOrdered;
template <typename T, typename Cmp>
class Monotonic;
template <typename T, template <typename...> class Storage>
class AppendOnly;
template <algebra::lattices::Consistency Level, typename T>
class Consistency;
template <algebra::lattices::Lifetime Scope, typename T>
class OpaqueLifetime;
template <algebra::lattices::CrashClass Class, typename T>
class Crash;
template <typename T>
class Budgeted;
template <typename T>
class EpochVersioned;
template <typename T>
class NumaPlacement;
template <typename T>
class RecipeSpec;
template <algebra::lattices::Witness Tier, typename T>
class Witness;
template <algebra::lattices::JoinPolicy Tier, typename T>
class JoinPolicy;
// Each per-axis alias instantiates this one template with a Mode of a
// different enum type, which is what lets the specializations below
// dispatch on that type and pick the matching salt. The declaration
// carries no constraint: the definition constrains Mode to an FP axis
// through a predicate this header cannot reach.
template <auto Mode, typename T>
class FpModePinned;
}  // namespace crucible::safety

namespace crucible::safety::diag {

namespace detail {

// One salt per wrapper kind, each owning a distinct high byte. A
// specialization ors the wrapper's own tier or tag enumerator into the
// low byte, so an enumerator value can never be mistaken for the same
// value under a different wrapper. A new wrapper takes the next free
// high byte. Changing a salt already in use moves every hash published
// under it.
//
// Three salts here have no specialization in this file. The wrapper's
// own header carries that, next to the type, and refers back to the
// constant so the allocation stays checkable in one place.

inline constexpr std::uint64_t WRAPPER_HOTPATH_TAG = 0x0100'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_DETSAFE_TAG = 0x0200'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_NUMERICAL_TIER_TAG = 0x0300'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_VENDOR_TAG = 0x0400'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_RESIDENCY_HEAT_TAG = 0x0500'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_CIPHER_TIER_TAG = 0x0600'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_ALLOC_CLASS_TAG = 0x0700'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_WAIT_TAG = 0x0800'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_MEM_ORDER_TAG = 0x0900'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_PROGRESS_TAG = 0x0A00'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_STALE_TAG = 0x0B00'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_TAGGED_TAG = 0x0C00'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_REFINED_TAG = 0x0D00'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_SECRET_TAG = 0x0E00'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_LINEAR_TAG = 0x0F00'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_RESOURCE_TAG_TAG = 0x1000'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_CONCURRENT_ROW_TAG = 0x1100'0000'0000'0000ULL;

inline constexpr std::uint64_t WRAPPER_SEALED_REFINED_TAG = 0x1200'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_TIME_ORDERED_TAG = 0x1300'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_MONOTONIC_TAG = 0x1400'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_APPEND_ONLY_TAG = 0x1500'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_CONSISTENCY_TAG = 0x1600'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_OPAQUE_LIFETIME_TAG = 0x1700'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_CRASH_TAG = 0x1800'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_BUDGETED_TAG = 0x1900'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_EPOCH_VERSIONED_TAG = 0x1A00'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_NUMA_PLACEMENT_TAG = 0x1B00'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_RECIPE_SPEC_TAG = 0x1C00'0000'0000'0000ULL;

// A concrete type is needed to derive an id, and a storage policy
// arrives as a template template parameter. Instantiating that policy
// with this phantom yields a concrete type that identifies the policy
// and nothing else. An empty struct is enough, it is a valid argument to
// any reasonable container template, and it reads as a discriminator
// rather than a payload wherever it shows up.
struct StorageProbe {};

inline constexpr std::uint64_t WRAPPER_SAFETY_FN_TAG = 0x1D00'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_FIXY_FN_TAG = 0x1E00'0000'0000'0000ULL;

inline constexpr std::uint64_t WRAPPER_WITNESS_TAG = 0x1F00'0000'0000'0000ULL;

inline constexpr std::uint64_t WRAPPER_JOIN_POLICY_TAG = 0x2000'0000'0000'0000ULL;

// Each floating-point axis takes its own salt, because a value computed
// under one rounding mode is not byte-equivalent to the same value under
// another. The composite over all eleven axes needs no salt: it nests
// one pinned layer per axis, and the fold walks through them.
inline constexpr std::uint64_t WRAPPER_FP_ROUNDING_TAG = 0x2100'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_FP_FTZ_TAG = 0x2200'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_FP_CONTRACT_TAG = 0x2300'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_FP_TRAP_MASK_TAG = 0x2400'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_FP_DENORMAL_INPUT_TAG = 0x2500'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_FP_NAN_POLICY_TAG = 0x2600'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_FP_INF_POLICY_TAG = 0x2700'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_FP_COMPLEX_LAYOUT_TAG = 0x2800'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_FP_LIBM_POLICY_TAG = 0x2900'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_FP_REASSOCIATE_TAG = 0x2A00'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_FP_CONSTANT_ROUNDING_TAG = 0x2B00'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_HW_INSTRUCTION_TAG = 0x2C00'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_BARRIER_STRENGTH_TAG = 0x2D00'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_SIMD_ISA_TAG = 0x2E00'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_MEMORY_SCOPE_TAG = 0x2F00'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_CLOCK_SOURCE_TAG = 0x3000'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_SCHED_CLASS_TAG = 0x3100'0000'0000'0000ULL;
// The pinning mask is a class non-type template argument, not an enum,
// so a specialization needs that class complete. Keeping it out of this
// widely included header means the salt lives here and the
// specialization lives beside the mask.
inline constexpr std::uint64_t WRAPPER_CPU_PINNED_TAG = 0x3200'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_SUSPEND_BEHAVIOR_TAG = 0x3300'0000'0000'0000ULL;

// The sort is quadratic. The array holds one entry per effect atom and
// the universe is capped well below the point where that matters, which
// is cheaper than depending on the algorithm header for a bounded
// compile-time problem.
template <std::size_t N>
[[nodiscard]] consteval std::array<std::uint64_t, N> sorted_uints(std::array<std::uint64_t, N> xs) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
        for (std::size_t j = i + 1; j < N; ++j) {
            if (xs[j] < xs[i]) {
                std::uint64_t const tmp = xs[i];
                xs[i] = xs[j];
                xs[j] = tmp;
            }
        }
    }
    return xs;
}

// The array must already be sorted, and the caller must have encoded
// cardinality into the seed. An effect whose underlying value is zero
// would otherwise collide with the empty row, because mixing a seed with
// zero returns the mix of the seed alone.
template <std::size_t N>
[[nodiscard]] consteval std::uint64_t fmix64_fold(std::array<std::uint64_t, N> const& xs, std::uint64_t seed) noexcept {
    std::uint64_t h = seed;
    for (std::size_t i = 0; i < N; ++i) {
        h = ::crucible::detail::fmix64(h ^ xs[i]);
    }
    return h;
}

// The seed takes this count, not the pack size. A row written with a
// repeated atom must seed the same as the row written once, or the two
// diverge in the seed even though the dedup fold makes the rest of the
// digest identical.
template <std::size_t N>
[[nodiscard]] consteval std::size_t unique_count_sorted(std::array<std::uint64_t, N> const& xs) noexcept {
    if constexpr (N == 0) {
        return 0;
    } else {
        std::size_t count = 1;
        for (std::size_t i = 1; i < N; ++i) {
            if (xs[i] != xs[i - 1]) ++count;
        }
        return count;
    }
}

// Skipping adjacent repeats is what makes the row hash a function of the
// effect set. Two rows over the same atoms agree whatever their
// declaration order and whatever their multiplicity. The seed must carry
// the unique count for the same reason.
template <std::size_t N>
[[nodiscard]] consteval std::uint64_t fmix64_fold_unique_sorted(std::array<std::uint64_t, N> const& xs,
                                                                std::uint64_t seed) noexcept {
    if constexpr (N == 0) {
        return seed;
    } else {
        std::uint64_t h = ::crucible::detail::fmix64(seed ^ xs[0]);
        for (std::size_t i = 1; i < N; ++i) {
            if (xs[i] != xs[i - 1]) {
                h = ::crucible::detail::fmix64(h ^ xs[i]);
            }
        }
        return h;
    }
}

// Every row hash starts here. Mixing the cardinality into the seed keeps
// rows of different length apart whatever coincidences the fold over
// their bodies produces.
[[nodiscard]] consteval std::uint64_t cardinality_seed(std::uint64_t cardinality) noexcept {
    return ::crucible::detail::fmix64(FNV1A_OFFSET_BASIS ^ cardinality);
}

inline constexpr std::uint64_t EMPTY_ROW_HASH = cardinality_seed(0);

// This names the compiler and standard library through predefined
// macros alone. It touches no reflected name, so it is identical on any
// machine running one toolchain and differs across toolchains by
// construction. That is exactly the discriminator a cross-toolchain
// federation key needs, and it stays out of the row hash itself.
[[nodiscard]] consteval std::uint64_t federation_toolchain_id() noexcept {
    std::uint64_t h = FNV1A_OFFSET_BASIS;
    h = combine_ids(h, static_cast<std::uint64_t>(__GNUC__));
    h = combine_ids(h, static_cast<std::uint64_t>(__GNUC_MINOR__));
    h = combine_ids(h, static_cast<std::uint64_t>(__GNUC_PATCHLEVEL__));
    h = combine_ids(h, static_cast<std::uint64_t>(__GLIBCXX__));
    return h;
}

inline constexpr std::uint64_t FEDERATION_TOOLCHAIN_TAG = federation_toolchain_id();

}  // namespace detail

// A specialization written outside this file belongs in this same
// namespace and follows one shape:
//
//   row_hash_contribution<W<T, ...attrs...>>::value =
//       combine_ids(<W's tag bits>, row_hash_contribution<T>::value)
//
// The combiner is order-sensitive, so a wrapper's position in the stack
// changes the hash. Two stacks that nest the same wrappers in opposite
// order are different keys, which is the intent: nesting order carries
// meaning, and there is one canonical order for callers to build in.

template <typename T>
struct row_hash_contribution {
    static constexpr std::uint64_t value = 0;
};

template <typename T>
inline constexpr std::uint64_t row_hash_contribution_v = row_hash_contribution<T>::value;

// A row denotes a set of effect atoms, and union, intersection and
// difference over rows are all set-shaped, so hash equality has to match
// set equality. Composing two policies that each declare one effect can
// hand this a pack with a repeat, and without the dedup that pack would
// take a slot of its own for a row the type system already treats as
// equal.

template <effects::Effect... Es>
struct row_hash_contribution<effects::Row<Es...>> {
    static constexpr std::uint64_t value = []() consteval -> std::uint64_t {
        constexpr std::size_t N = sizeof...(Es);
        if constexpr (N == 0) {
            return detail::cardinality_seed(0);
        } else {
            std::array<std::uint64_t, N> const raw_vals{
                static_cast<std::uint64_t>(static_cast<std::underlying_type_t<effects::Effect>>(Es))...};
            auto const sorted = detail::sorted_uints(raw_vals);
            // The seed takes the number of distinct atoms, never the
            // pack size, and it is mixed before any atom. That also
            // keeps the atom whose underlying value is zero from folding
            // into the seed unchanged.
            std::size_t const unique_n = detail::unique_count_sorted(sorted);
            std::uint64_t const seed = detail::cardinality_seed(unique_n);
            return detail::fmix64_fold_unique_sorted(sorted, seed);
        }
    }();
};

// Combining the row with the payload contribution satisfies three
// properties this carrier owes the cache, and the self-tests pin all
// three.
//
// A carrier is distinct from its own bare row. A row is metadata and a
// carrier is a value, and the combiner returns something other than X
// when folding X with a zero payload contribution.
//
// A carrier is blind to a bare payload. Payload identity belongs to the
// content hash, the other half of the key, so a kernel returning one
// scalar type shares a row signature with a kernel returning another.
//
// A carrier does not collapse over a payload that carries a row of its
// own. A carrier nested inside a carrier keeps the inner row in the
// outer hash, so nesting cannot alias the flattened form.
//
// The row folds first and the payload second. That order is fixed, not
// incidental, because the combiner is order-sensitive.

template <typename R, typename T>
struct row_hash_contribution<effects::Computation<R, T>> {
    static constexpr std::uint64_t value = detail::combine_ids(row_hash_contribution_v<R>, row_hash_contribution_v<T>);
};

// Wrapper attributes participate wherever they are part of the
// type-level semantics: a tier folds its underlying value, a tagged
// value folds the source tag type, a refinement folds the predicate.

template <algebra::lattices::HotPathTier Tier, typename Inner>
struct row_hash_contribution<safety::HotPath<Tier, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_HOTPATH_TAG | static_cast<std::uint64_t>(Tier), row_hash_contribution_v<Inner>);
};

template <algebra::lattices::DetSafeTier Tier, typename Inner>
struct row_hash_contribution<safety::DetSafe<Tier, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_DETSAFE_TAG | static_cast<std::uint64_t>(Tier), row_hash_contribution_v<Inner>);
};

template <algebra::lattices::Tolerance Tier, typename Inner>
struct row_hash_contribution<safety::NumericalTier<Tier, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_NUMERICAL_TIER_TAG | static_cast<std::uint64_t>(Tier), row_hash_contribution_v<Inner>);
};

template <algebra::lattices::VendorBackend Backend, typename Inner>
struct row_hash_contribution<safety::Vendor<Backend, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_VENDOR_TAG | static_cast<std::uint64_t>(Backend), row_hash_contribution_v<Inner>);
};

template <algebra::lattices::HwInstruction Tier, typename Inner>
struct row_hash_contribution<safety::Hw<Tier, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_HW_INSTRUCTION_TAG | static_cast<std::uint64_t>(Tier), row_hash_contribution_v<Inner>);
};

template <algebra::lattices::BarrierStrength Tier, typename Inner>
struct row_hash_contribution<safety::BarrierGuarded<Tier, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_BARRIER_STRENGTH_TAG | static_cast<std::uint64_t>(Tier), row_hash_contribution_v<Inner>);
};

template <algebra::lattices::SimdIsa W, typename Inner>
struct row_hash_contribution<safety::SimdWidthPinned<W, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_SIMD_ISA_TAG | static_cast<std::uint64_t>(W), row_hash_contribution_v<Inner>);
};

// A memory scope packs its trunk into the high nibble of its underlying
// value, so scopes on different trunks and the two shared sentinels all
// occupy distinct low bytes of this salt.
template <algebra::lattices::MemoryScope S, typename Inner>
struct row_hash_contribution<safety::ScopedFence<S, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_MEMORY_SCOPE_TAG | static_cast<std::uint64_t>(S), row_hash_contribution_v<Inner>);
};

template <algebra::lattices::ClockSource Source, typename Inner>
struct row_hash_contribution<safety::ClockSource<Source, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_CLOCK_SOURCE_TAG | static_cast<std::uint64_t>(Source), row_hash_contribution_v<Inner>);
};

// Each budget folds through a combine step of its own. Pre-mixing the
// three with shifts and exclusive-or reads as cheaper and is wrong:
// distinct triples share one pre-mixed value whenever a flipped bit in
// one field is compensated in the shifted position of another, and no
// amount of avalanche downstream can undo a collision that already
// happened upstream.
template <algebra::lattices::SchedulerPolicy Policy, typename Inner, std::uint64_t RuntimeNs, std::uint64_t DeadlineNs,
          std::uint64_t PeriodNs>
struct row_hash_contribution<safety::SchedClass<Policy, Inner, RuntimeNs, DeadlineNs, PeriodNs>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::combine_ids(
            detail::combine_ids(
                detail::combine_ids(detail::WRAPPER_SCHED_CLASS_TAG | static_cast<std::uint64_t>(Policy), RuntimeNs),
                DeadlineNs),
            PeriodNs),
        row_hash_contribution_v<Inner>);
};

template <algebra::lattices::SuspendBehavior Behavior, typename Inner>
struct row_hash_contribution<safety::SuspendBehavior<Behavior, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_SUSPEND_BEHAVIOR_TAG | static_cast<std::uint64_t>(Behavior), row_hash_contribution_v<Inner>);
};

template <algebra::lattices::ResidencyHeatTag Tier, typename Inner>
struct row_hash_contribution<safety::ResidencyHeat<Tier, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_RESIDENCY_HEAT_TAG | static_cast<std::uint64_t>(Tier), row_hash_contribution_v<Inner>);
};

template <algebra::lattices::CipherTierTag Tier, typename Inner>
struct row_hash_contribution<safety::CipherTier<Tier, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_CIPHER_TIER_TAG | static_cast<std::uint64_t>(Tier), row_hash_contribution_v<Inner>);
};

template <algebra::lattices::AllocClassTag Tag, typename Inner>
struct row_hash_contribution<safety::AllocClass<Tag, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_ALLOC_CLASS_TAG | static_cast<std::uint64_t>(Tag), row_hash_contribution_v<Inner>);
};

template <algebra::lattices::WaitStrategy Strategy, typename Inner>
struct row_hash_contribution<safety::Wait<Strategy, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_WAIT_TAG | static_cast<std::uint64_t>(Strategy), row_hash_contribution_v<Inner>);
};

template <algebra::lattices::MemOrderTag Tag, typename Inner>
struct row_hash_contribution<safety::MemOrder<Tag, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_MEM_ORDER_TAG | static_cast<std::uint64_t>(Tag), row_hash_contribution_v<Inner>);
};

template <algebra::lattices::ProgressClass Class, typename Inner>
struct row_hash_contribution<safety::Progress<Class, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_PROGRESS_TAG | static_cast<std::uint64_t>(Class), row_hash_contribution_v<Inner>);
};

template <typename Inner>
struct row_hash_contribution<safety::Stale<Inner>> {
    static constexpr std::uint64_t value =
        detail::combine_ids(detail::WRAPPER_STALE_TAG, row_hash_contribution_v<Inner>);
};

template <typename Inner, typename Source>
struct row_hash_contribution<safety::Tagged<Inner, Source>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::combine_ids(detail::WRAPPER_TAGGED_TAG, stable_type_id<Source>), row_hash_contribution_v<Inner>);
};

// A refinement folds its predicate's identity into the key, and the
// default identity is the predicate's type. That identity is structural,
// not behavioral: two predicates that decide the same thing under
// different type names take different slots, and renaming a predicate
// orphans every entry cached under the old name.
//
// Specializing this trait maps a second predicate type onto an existing
// identity. That is what a rename or an alias needs in order to keep
// reaching entries already cached, and what peers need when they spell
// one predicate differently. Aliasing a predicate without specializing
// here fragments the cache silently.
template <auto Pred>
struct pred_canonical_id {
    static constexpr std::uint64_t value = stable_type_id<std::remove_cvref_t<decltype(Pred)>>;
};

template <auto Pred>
inline constexpr std::uint64_t pred_canonical_id_v = pred_canonical_id<Pred>::value;

template <auto Pred, typename Inner>
struct row_hash_contribution<safety::Refined<Pred, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::combine_ids(detail::WRAPPER_REFINED_TAG, pred_canonical_id_v<Pred>), row_hash_contribution_v<Inner>);
};

template <typename Inner>
struct row_hash_contribution<safety::Secret<Inner>> {
    static constexpr std::uint64_t value =
        detail::combine_ids(detail::WRAPPER_SECRET_TAG, row_hash_contribution_v<Inner>);
};

template <typename Inner>
struct row_hash_contribution<safety::Linear<Inner>> {
    static constexpr std::uint64_t value =
        detail::combine_ids(detail::WRAPPER_LINEAR_TAG, row_hash_contribution_v<Inner>);
};

// A wrapper with no specialization falls through to the primary
// template, contributes zero, and lets every instantiation of it share
// one slot with every bare type. Every wrapper carrying type-level
// meaning needs an entry here.

// This routes through the same predicate identity as the unsealed form,
// so one rename mapping covers both kinds of key.
template <auto Pred, typename Inner>
struct row_hash_contribution<safety::SealedRefined<Pred, Inner>> {
    static constexpr std::uint64_t value =
        detail::combine_ids(detail::combine_ids(detail::WRAPPER_SEALED_REFINED_TAG, pred_canonical_id_v<Pred>),
                            row_hash_contribution_v<Inner>);
};

// The bucket count folds in with no cast: it is already a 64-bit
// unsigned on every supported platform, and a cast there is diagnosed as
// useless.
template <typename Inner, std::size_t N, typename Tag>
struct row_hash_contribution<safety::TimeOrdered<Inner, N, Tag>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::combine_ids(detail::combine_ids(detail::WRAPPER_TIME_ORDERED_TAG, N), stable_type_id<Tag>),
        row_hash_contribution_v<Inner>);
};

// The comparator folds in: an ascending counter and a descending one
// over the same payload are different values.
template <typename Inner, typename Cmp>
struct row_hash_contribution<safety::Monotonic<Inner, Cmp>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::combine_ids(detail::WRAPPER_MONOTONIC_TAG, stable_type_id<Cmp>), row_hash_contribution_v<Inner>);
};

// The storage policy is part of the key. Two peers that pick different
// containers get different growth behaviour and different allocator
// interaction, so they must not share a slot. The policy arrives as a
// template template parameter and an id needs a concrete type, so the
// phantom probe stands in for the element type.
template <typename Inner, template <typename...> class Storage>
struct row_hash_contribution<safety::AppendOnly<Inner, Storage>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::combine_ids(detail::WRAPPER_APPEND_ONLY_TAG, stable_type_id<Storage<detail::StorageProbe>>),
        row_hash_contribution_v<Inner>);
};

template <algebra::lattices::Consistency Level, typename Inner>
struct row_hash_contribution<safety::Consistency<Level, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_CONSISTENCY_TAG | static_cast<std::uint64_t>(Level), row_hash_contribution_v<Inner>);
};

template <algebra::lattices::Lifetime Scope, typename Inner>
struct row_hash_contribution<safety::OpaqueLifetime<Scope, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_OPAQUE_LIFETIME_TAG | static_cast<std::uint64_t>(Scope), row_hash_contribution_v<Inner>);
};

// Each crash class is a different recovery contract, so each takes its
// own slot.
template <algebra::lattices::CrashClass Class, typename Inner>
struct row_hash_contribution<safety::Crash<Class, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_CRASH_TAG | static_cast<std::uint64_t>(Class), row_hash_contribution_v<Inner>);
};

// The four wrappers below each carry a per-instance grade at runtime,
// and none of it reaches the hash. That is deliberate: this key
// identifies a type, not an instance. A consumer that needs to tell two
// instances apart folds the grade in through the separate runtime
// surface, which adds to this hash rather than replacing it.
template <typename Inner>
struct row_hash_contribution<safety::Budgeted<Inner>> {
    static constexpr std::uint64_t value =
        detail::combine_ids(detail::WRAPPER_BUDGETED_TAG, row_hash_contribution_v<Inner>);
};

template <typename Inner>
struct row_hash_contribution<safety::EpochVersioned<Inner>> {
    static constexpr std::uint64_t value =
        detail::combine_ids(detail::WRAPPER_EPOCH_VERSIONED_TAG, row_hash_contribution_v<Inner>);
};

template <typename Inner>
struct row_hash_contribution<safety::NumaPlacement<Inner>> {
    static constexpr std::uint64_t value =
        detail::combine_ids(detail::WRAPPER_NUMA_PLACEMENT_TAG, row_hash_contribution_v<Inner>);
};

template <typename Inner>
struct row_hash_contribution<safety::RecipeSpec<Inner>> {
    static constexpr std::uint64_t value =
        detail::combine_ids(detail::WRAPPER_RECIPE_SPEC_TAG, row_hash_contribution_v<Inner>);
};

// The tier records how much is known about the payload. A verified
// result and a merely type-checked one are not interchangeable even when
// the payload bytes match, so they take different slots.
template <algebra::lattices::Witness Tier, typename Inner>
struct row_hash_contribution<safety::Witness<Tier, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_WITNESS_TAG | static_cast<std::uint64_t>(Tier), row_hash_contribution_v<Inner>);
};

// The tier records what the parent did with its children. A region that
// joined every worker and a region that abandoned them must not collide.
template <algebra::lattices::JoinPolicy Tier, typename Inner>
struct row_hash_contribution<safety::JoinPolicy<Tier, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_JOIN_POLICY_TAG | static_cast<std::uint64_t>(Tier), row_hash_contribution_v<Inner>);
};

// These are distinct partial specializations of one template because
// each takes a Mode of a different enum type. A composite pins one axis
// per layer, and the fold walks the layers, so it needs no entry of its
// own.
template <algebra::lattices::FpRounding Mode, typename Inner>
struct row_hash_contribution<safety::FpModePinned<Mode, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_FP_ROUNDING_TAG | static_cast<std::uint64_t>(Mode), row_hash_contribution_v<Inner>);
};

template <algebra::lattices::FpFtz Mode, typename Inner>
struct row_hash_contribution<safety::FpModePinned<Mode, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_FP_FTZ_TAG | static_cast<std::uint64_t>(Mode), row_hash_contribution_v<Inner>);
};

template <algebra::lattices::FpContract Mode, typename Inner>
struct row_hash_contribution<safety::FpModePinned<Mode, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_FP_CONTRACT_TAG | static_cast<std::uint64_t>(Mode), row_hash_contribution_v<Inner>);
};

template <algebra::lattices::FpTrapMask Mode, typename Inner>
struct row_hash_contribution<safety::FpModePinned<Mode, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_FP_TRAP_MASK_TAG | static_cast<std::uint64_t>(Mode), row_hash_contribution_v<Inner>);
};

template <algebra::lattices::FpDenormalInput Mode, typename Inner>
struct row_hash_contribution<safety::FpModePinned<Mode, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_FP_DENORMAL_INPUT_TAG | static_cast<std::uint64_t>(Mode), row_hash_contribution_v<Inner>);
};

template <algebra::lattices::FpNanPolicy Mode, typename Inner>
struct row_hash_contribution<safety::FpModePinned<Mode, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_FP_NAN_POLICY_TAG | static_cast<std::uint64_t>(Mode), row_hash_contribution_v<Inner>);
};

template <algebra::lattices::FpInfPolicy Mode, typename Inner>
struct row_hash_contribution<safety::FpModePinned<Mode, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_FP_INF_POLICY_TAG | static_cast<std::uint64_t>(Mode), row_hash_contribution_v<Inner>);
};

template <algebra::lattices::FpComplexLayout Mode, typename Inner>
struct row_hash_contribution<safety::FpModePinned<Mode, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_FP_COMPLEX_LAYOUT_TAG | static_cast<std::uint64_t>(Mode), row_hash_contribution_v<Inner>);
};

template <algebra::lattices::FpLibmPolicy Mode, typename Inner>
struct row_hash_contribution<safety::FpModePinned<Mode, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_FP_LIBM_POLICY_TAG | static_cast<std::uint64_t>(Mode), row_hash_contribution_v<Inner>);
};

template <algebra::lattices::FpReassociate Mode, typename Inner>
struct row_hash_contribution<safety::FpModePinned<Mode, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_FP_REASSOCIATE_TAG | static_cast<std::uint64_t>(Mode), row_hash_contribution_v<Inner>);
};

template <algebra::lattices::FpConstantRounding Mode, typename Inner>
struct row_hash_contribution<safety::FpModePinned<Mode, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_FP_CONSTANT_ROUNDING_TAG | static_cast<std::uint64_t>(Mode), row_hash_contribution_v<Inner>);
};

template <typename T>
[[nodiscard]] consteval RowHash row_hash_of() noexcept {
    return RowHash{row_hash_contribution_v<T>};
}

template <typename T>
inline constexpr RowHash row_hash_of_v = row_hash_of<T>();

// Peers on one toolchain join on the bare row hash and need nothing
// here. Peers that may run different compilers or different major
// versions of one compiler key through the function below, which folds
// the toolchain in and maps the same computation to disjoint slots on
// each side. That prevents both the silent collision and the false
// sharing of a slot whose reflected-name bits only happened to match.
[[nodiscard]] consteval std::uint64_t federation_toolchain_tag() noexcept { return detail::FEDERATION_TOOLCHAIN_TAG; }

template <typename T>
[[nodiscard]] consteval RowHash federation_key_with_toolchain() noexcept {
    return RowHash{detail::combine_ids(detail::FEDERATION_TOOLCHAIN_TAG, row_hash_contribution_v<T>)};
}

template <typename T>
inline constexpr RowHash federation_key_with_toolchain_v = federation_key_with_toolchain<T>();

namespace detail::row_hash_self_test {

using effects::Effect;
using effects::EmptyRow;
using effects::Row;

static_assert(row_hash_contribution_v<int> == 0);
static_assert(row_hash_contribution_v<float> == 0);
static_assert(row_hash_contribution_v<double> == 0);
static_assert(row_hash_contribution_v<void> == 0);
static_assert(row_hash_contribution_v<unsigned> == 0);

static_assert(row_hash_of_v<int> == RowHash{0});
static_assert(row_hash_of_v<float> == RowHash{0});

// A zero row hash is a row-defaulted entry, not an empty slot.
static_assert(!row_hash_of_v<int>.is_sentinel());

static_assert(row_hash_contribution_v<Row<Effect::Alloc>> != 0);
static_assert(row_hash_contribution_v<Row<Effect::IO>> != 0);
static_assert(row_hash_contribution_v<Row<Effect::Block>> != 0);
static_assert(row_hash_contribution_v<Row<Effect::Bg>> != 0);
static_assert(row_hash_contribution_v<Row<Effect::Init>> != 0);
static_assert(row_hash_contribution_v<Row<Effect::Test>> != 0);

static_assert(row_hash_contribution_v<Row<Effect::Alloc>> != row_hash_contribution_v<Row<Effect::IO>>);
static_assert(row_hash_contribution_v<Row<Effect::Alloc>> != row_hash_contribution_v<Row<Effect::Block>>);
static_assert(row_hash_contribution_v<Row<Effect::IO>> != row_hash_contribution_v<Row<Effect::Bg>>);

static_assert(row_hash_contribution_v<Row<Effect::Alloc, Effect::IO>>
              == row_hash_contribution_v<Row<Effect::IO, Effect::Alloc>>);

static_assert(row_hash_contribution_v<Row<Effect::Block, Effect::Bg>>
              == row_hash_contribution_v<Row<Effect::Bg, Effect::Block>>);

static_assert(row_hash_contribution_v<Row<Effect::Alloc, Effect::IO, Effect::Block>>
              == row_hash_contribution_v<Row<Effect::Alloc, Effect::Block, Effect::IO>>);
static_assert(row_hash_contribution_v<Row<Effect::Alloc, Effect::IO, Effect::Block>>
              == row_hash_contribution_v<Row<Effect::IO, Effect::Alloc, Effect::Block>>);
static_assert(row_hash_contribution_v<Row<Effect::Alloc, Effect::IO, Effect::Block>>
              == row_hash_contribution_v<Row<Effect::IO, Effect::Block, Effect::Alloc>>);
static_assert(row_hash_contribution_v<Row<Effect::Alloc, Effect::IO, Effect::Block>>
              == row_hash_contribution_v<Row<Effect::Block, Effect::Alloc, Effect::IO>>);
static_assert(row_hash_contribution_v<Row<Effect::Alloc, Effect::IO, Effect::Block>>
              == row_hash_contribution_v<Row<Effect::Block, Effect::IO, Effect::Alloc>>);

using FullRow_canonical = Row<Effect::Alloc, Effect::IO, Effect::Block, Effect::Bg, Effect::Init, Effect::Test>;
using FullRow_reversed = Row<Effect::Test, Effect::Init, Effect::Bg, Effect::Block, Effect::IO, Effect::Alloc>;
using FullRow_shuffled = Row<Effect::Block, Effect::Alloc, Effect::Test, Effect::IO, Effect::Init, Effect::Bg>;

static_assert(row_hash_contribution_v<FullRow_canonical> == row_hash_contribution_v<FullRow_reversed>);
static_assert(row_hash_contribution_v<FullRow_canonical> == row_hash_contribution_v<FullRow_shuffled>);

// Nothing canonicalizes a pack before it reaches the hash, so these
// cases are the only thing keeping a row written with repeats out of a
// slot of its own.
static_assert(row_hash_contribution_v<Row<Effect::Alloc, Effect::Alloc>>
              == row_hash_contribution_v<Row<Effect::Alloc>>);
static_assert(row_hash_contribution_v<Row<Effect::IO, Effect::IO>> == row_hash_contribution_v<Row<Effect::IO>>);
static_assert(row_hash_contribution_v<Row<Effect::Block, Effect::Block>>
              == row_hash_contribution_v<Row<Effect::Block>>);
static_assert(row_hash_contribution_v<Row<Effect::Bg, Effect::Bg>> == row_hash_contribution_v<Row<Effect::Bg>>);

static_assert(row_hash_contribution_v<Row<Effect::IO, Effect::IO, Effect::IO>>
              == row_hash_contribution_v<Row<Effect::IO>>);

static_assert(row_hash_contribution_v<Row<Effect::Alloc, Effect::Alloc, Effect::IO>>
              == row_hash_contribution_v<Row<Effect::Alloc, Effect::IO>>);
static_assert(row_hash_contribution_v<Row<Effect::Alloc, Effect::IO, Effect::IO>>
              == row_hash_contribution_v<Row<Effect::Alloc, Effect::IO>>);

static_assert(row_hash_contribution_v<Row<Effect::Bg, Effect::IO, Effect::Bg, Effect::IO>>
              == row_hash_contribution_v<Row<Effect::IO, Effect::Bg>>);
static_assert(row_hash_contribution_v<Row<Effect::IO, Effect::Bg, Effect::Bg, Effect::IO>>
              == row_hash_contribution_v<Row<Effect::IO, Effect::Bg>>);

// A pack of four with two distinct atoms must seed on two, which pins
// that the seed reads the unique count and not the pack size.
static_assert(row_hash_contribution_v<Row<Effect::Alloc, Effect::Alloc, Effect::IO, Effect::IO>>
              == row_hash_contribution_v<Row<Effect::Alloc, Effect::IO>>);

static_assert(row_hash_contribution_v<Row<Effect::Alloc>> != row_hash_contribution_v<Row<Effect::Alloc, Effect::IO>>);

static_assert(row_hash_contribution_v<Row<Effect::Alloc, Effect::IO>>
              != row_hash_contribution_v<Row<Effect::Alloc, Effect::IO, Effect::Block>>);

static_assert(row_hash_contribution_v<Row<Effect::Alloc>> != row_hash_contribution_v<Row<Effect::IO>>);

static_assert(row_hash_contribution_v<Row<Effect::Alloc, Effect::IO>>
              != row_hash_contribution_v<Row<Effect::Block, Effect::Bg>>);

static_assert(row_hash_contribution_v<EmptyRow> != 0);
static_assert(row_hash_contribution_v<EmptyRow> == detail::EMPTY_ROW_HASH);

static_assert(row_hash_contribution_v<EmptyRow> != row_hash_contribution_v<int>);

// The atom whose underlying value is zero is the dangerous one. Folding
// zero into a seed leaves the seed alone, which is how a singleton over
// that atom would alias the empty row. Every singleton is pinned apart
// from the empty row for that reason.
static_assert(row_hash_contribution_v<EmptyRow> != row_hash_contribution_v<Row<Effect::Alloc>>);
static_assert(row_hash_contribution_v<EmptyRow> != row_hash_contribution_v<Row<Effect::IO>>);
static_assert(row_hash_contribution_v<EmptyRow> != row_hash_contribution_v<Row<Effect::Block>>);
static_assert(row_hash_contribution_v<EmptyRow> != row_hash_contribution_v<Row<Effect::Bg>>);
static_assert(row_hash_contribution_v<EmptyRow> != row_hash_contribution_v<Row<Effect::Init>>);
static_assert(row_hash_contribution_v<EmptyRow> != row_hash_contribution_v<Row<Effect::Test>>);

// Exhaustive over pairs of atoms, up to symmetry. A renumbering of the
// effect enum that made two atoms share a value would show up here.

static_assert(row_hash_contribution_v<Row<Effect::Alloc>> != row_hash_contribution_v<Row<Effect::IO>>);
static_assert(row_hash_contribution_v<Row<Effect::Alloc>> != row_hash_contribution_v<Row<Effect::Block>>);
static_assert(row_hash_contribution_v<Row<Effect::Alloc>> != row_hash_contribution_v<Row<Effect::Bg>>);
static_assert(row_hash_contribution_v<Row<Effect::Alloc>> != row_hash_contribution_v<Row<Effect::Init>>);
static_assert(row_hash_contribution_v<Row<Effect::Alloc>> != row_hash_contribution_v<Row<Effect::Test>>);
static_assert(row_hash_contribution_v<Row<Effect::IO>> != row_hash_contribution_v<Row<Effect::Block>>);
static_assert(row_hash_contribution_v<Row<Effect::IO>> != row_hash_contribution_v<Row<Effect::Bg>>);
static_assert(row_hash_contribution_v<Row<Effect::IO>> != row_hash_contribution_v<Row<Effect::Init>>);
static_assert(row_hash_contribution_v<Row<Effect::IO>> != row_hash_contribution_v<Row<Effect::Test>>);
static_assert(row_hash_contribution_v<Row<Effect::Block>> != row_hash_contribution_v<Row<Effect::Bg>>);
static_assert(row_hash_contribution_v<Row<Effect::Block>> != row_hash_contribution_v<Row<Effect::Init>>);
static_assert(row_hash_contribution_v<Row<Effect::Block>> != row_hash_contribution_v<Row<Effect::Test>>);
static_assert(row_hash_contribution_v<Row<Effect::Bg>> != row_hash_contribution_v<Row<Effect::Init>>);
static_assert(row_hash_contribution_v<Row<Effect::Bg>> != row_hash_contribution_v<Row<Effect::Test>>);
static_assert(row_hash_contribution_v<Row<Effect::Init>> != row_hash_contribution_v<Row<Effect::Test>>);

static_assert(std::is_same_v<decltype(row_hash_of_v<int>), const RowHash>);
static_assert(row_hash_of_v<EmptyRow>.raw() == detail::EMPTY_ROW_HASH);

static_assert(row_hash_of_v<Row<Effect::Alloc>> == RowHash{row_hash_contribution_v<Row<Effect::Alloc>>});

// The all-ones value marks an empty cache slot. A real row that landed
// on it would claim that slot. The rows in actual use are checked here.

static_assert(row_hash_contribution_v<EmptyRow> != static_cast<std::uint64_t>(-1));
static_assert(row_hash_contribution_v<Row<Effect::Alloc>> != static_cast<std::uint64_t>(-1));
static_assert(row_hash_contribution_v<FullRow_canonical> != static_cast<std::uint64_t>(-1));

static_assert(row_hash_contribution_v<Row<Effect::IO>> != static_cast<std::uint64_t>(-1));
static_assert(row_hash_contribution_v<Row<Effect::Block>> != static_cast<std::uint64_t>(-1));
static_assert(row_hash_contribution_v<Row<Effect::Bg>> != static_cast<std::uint64_t>(-1));
static_assert(row_hash_contribution_v<Row<Effect::Init>> != static_cast<std::uint64_t>(-1));
static_assert(row_hash_contribution_v<Row<Effect::Test>> != static_cast<std::uint64_t>(-1));

// A carrier over the empty row is not a bare payload.
static_assert(row_hash_contribution_v<effects::Computation<EmptyRow, int>> != row_hash_contribution_v<int>);
static_assert(row_hash_contribution_v<effects::Computation<EmptyRow, int>> != 0);

// A carrier is not its own row. This is what folding with a zero payload
// contribution has to buy: the cache can tell the carrier from the
// metadata.
static_assert(row_hash_contribution_v<effects::Computation<EmptyRow, int>> != row_hash_contribution_v<EmptyRow>);
static_assert(row_hash_contribution_v<effects::Computation<Row<Effect::Alloc>, int>>
              != row_hash_contribution_v<Row<Effect::Alloc>>);
static_assert(row_hash_contribution_v<effects::Computation<Row<Effect::IO>, int>>
              != row_hash_contribution_v<Row<Effect::IO>>);

// Payload identity belongs to the content hash, so two kernels that
// return different scalar types share a row signature and separate on
// the other half of the key.
static_assert(row_hash_contribution_v<effects::Computation<EmptyRow, int>>
              == row_hash_contribution_v<effects::Computation<EmptyRow, double>>);
static_assert(row_hash_contribution_v<effects::Computation<EmptyRow, int>>
              == row_hash_contribution_v<effects::Computation<EmptyRow, float>>);
static_assert(row_hash_contribution_v<effects::Computation<Row<Effect::Alloc>, int>>
              == row_hash_contribution_v<effects::Computation<Row<Effect::Alloc>, char>>);

// Two kernels that compute the same value under different effect rows
// must not share a slot. This is the property the whole key exists for.
static_assert(row_hash_contribution_v<effects::Computation<Row<Effect::Alloc>, int>>
              != row_hash_contribution_v<effects::Computation<Row<Effect::IO>, int>>);
static_assert(row_hash_contribution_v<effects::Computation<Row<Effect::Alloc>, int>>
              != row_hash_contribution_v<effects::Computation<EmptyRow, int>>);

// Permutation invariance and cardinality both lift through the carrier.
// Each follows from the row specialization, and each is pinned here so a
// change to the combiner cannot quietly break it.
static_assert(row_hash_contribution_v<effects::Computation<Row<Effect::Alloc, Effect::IO>, int>>
              == row_hash_contribution_v<effects::Computation<Row<Effect::IO, Effect::Alloc>, int>>);

static_assert(row_hash_contribution_v<effects::Computation<Row<Effect::Alloc>, int>>
              != row_hash_contribution_v<effects::Computation<Row<Effect::Alloc, Effect::IO>, int>>);

// A carrier nested inside a carrier keeps the inner row visible in the
// outer hash, so it cannot alias the flattened form. That matters before
// anything flattens it, because the two differ until then.
static_assert(row_hash_contribution_v<effects::Computation<EmptyRow, effects::Computation<Row<Effect::IO>, int>>>
              != row_hash_contribution_v<effects::Computation<EmptyRow, int>>);
static_assert(row_hash_contribution_v<effects::Computation<EmptyRow, effects::Computation<Row<Effect::IO>, int>>>
              != row_hash_contribution_v<effects::Computation<Row<Effect::IO>, int>>);

static_assert(row_hash_contribution_v<effects::Computation<EmptyRow, int>> != static_cast<std::uint64_t>(-1));
static_assert(row_hash_contribution_v<effects::Computation<FullRow_canonical, int>> != static_cast<std::uint64_t>(-1));

// These literals are the on-the-wire contract. Every published cache
// entry whose key includes one of these rows carries that exact 64-bit
// value in its serialized form. Changing an underlying effect value,
// changing the mixer or the offset basis, or changing the seed and fold
// here invalidates every entry already published, everywhere.
//
// Do not edit a literal to make the build pass. A literal changes only
// as part of a deliberate wire-format break, announced the way that
// procedure requires. A drift without it corrupts every peer's cache
// silently. Adding a pin for a row not covered here is always safe.

static_assert(row_hash_contribution_v<EmptyRow> == 0xEFD01F60BA992926ULL,
              "the empty row hash drifted, which breaks the federation wire format");
static_assert(row_hash_contribution_v<Row<Effect::Alloc>> == 0x436DAF9EDCB565C3ULL,
              "Row<Alloc> row_hash drifted — federation wire-format break.");
static_assert(row_hash_contribution_v<Row<Effect::IO>> == 0x6FBFD0F707B63BECULL,
              "Row<IO> row_hash drifted — federation wire-format break.");
static_assert(row_hash_contribution_v<Row<Effect::Block>> == 0x3117F06B828C9247ULL,
              "Row<Block> row_hash drifted — federation wire-format break.");
static_assert(row_hash_contribution_v<Row<Effect::Bg>> == 0x008A519814C8FC81ULL,
              "Row<Bg> row_hash drifted — federation wire-format break.");
static_assert(row_hash_contribution_v<Row<Effect::Init>> == 0x9E23FC5AC81DA675ULL,
              "Row<Init> row_hash drifted — federation wire-format break.");
static_assert(row_hash_contribution_v<Row<Effect::Test>> == 0x26A9EB08E748D58FULL,
              "Row<Test> row_hash drifted — federation wire-format break.");
static_assert(row_hash_contribution_v<Row<Effect::Alloc, Effect::IO>> == 0x6CC046F52E6D7663ULL,
              "Row<Alloc, IO> row_hash drifted — federation wire-format break.");
static_assert(
    row_hash_contribution_v<Row<Effect::Alloc, Effect::IO, Effect::Block, Effect::Bg, Effect::Init, Effect::Test>>
        == 0x1C9D0E4F548FAAD6ULL,
    "Full-Universe row row_hash drifted — federation wire-format break.");

// The carrier pins catch a drift in the combiner, in the carrier
// specialization, or in a row contribution, and they carry the same
// severity as the row pins above.

static_assert(row_hash_contribution_v<effects::Computation<EmptyRow, int>> == 0x49A55BE1CFC23FB0ULL,
              "Computation<EmptyRow, int> row_hash drifted — federation wire-format break. Either combine_ids or "
              "the Computation specialization changed.");
static_assert(row_hash_contribution_v<effects::Computation<Row<Effect::Bg>, int>> == 0x3ACE35615F0F9243ULL,
              "Computation<Row<Bg>, int> row_hash drifted — wire-format break.");
static_assert(row_hash_contribution_v<effects::Computation<Row<Effect::Alloc, Effect::IO>, int>>
                  == 0x83D432DE6CDEACA7ULL,
              "Computation<Row<Alloc, IO>, int> row_hash drifted — break.");
static_assert(row_hash_contribution_v<effects::Computation<EmptyRow, effects::Computation<Row<Effect::IO>, int>>>
                  == 0x94EC56B861A6B8FDULL,
              "Nested Computation<EmptyRow, Computation<Row<IO>, int>> "
              "row_hash drifted — wire-format break.  Inner-row "
              "non-collapsing through combine_ids must remain bit-stable.");

// One anchor per effect atom, so that a routing regression on any single
// atom reddens the build instead of moving the cache slot for every
// kernel that declares it.

static_assert(row_hash_contribution_v<effects::Computation<Row<Effect::Alloc>, int>> == 0x058CA6EFB434D439ULL,
              "Computation<Row<Alloc>, int> row_hash drifted — wire-format break.");
static_assert(row_hash_contribution_v<effects::Computation<Row<Effect::IO>, int>> == 0xCCFE717213BBA49CULL,
              "Computation<Row<IO>, int> row_hash drifted — wire-format break.");
static_assert(row_hash_contribution_v<effects::Computation<Row<Effect::Block>, int>> == 0x6D28A236D0E146C7ULL,
              "Computation<Row<Block>, int> row_hash drifted — wire-format break.");
static_assert(row_hash_contribution_v<effects::Computation<Row<Effect::Init>, int>> == 0x64EF4D0126C4A4E3ULL,
              "Computation<Row<Init>, int> row_hash drifted — wire-format break.");
static_assert(row_hash_contribution_v<effects::Computation<Row<Effect::Test>, int>> == 0xF4060D16B464EFDEULL,
              "Computation<Row<Test>, int> row_hash drifted — wire-format break.");

// Nested EmptyRow-over-singleton (IO already pinned above) — exercises
// the inner-row-engaged-outer-row-empty composition, which is the
// shape that an outer caller wraps in a top-level no-effect frame.

static_assert(row_hash_contribution_v<effects::Computation<EmptyRow, effects::Computation<Row<Effect::Alloc>, int>>>
                  == 0x0BECBF75AD6D7A0CULL,
              "Computation<EmptyRow, Computation<Row<Alloc>, int>> drifted.");
static_assert(row_hash_contribution_v<effects::Computation<EmptyRow, effects::Computation<Row<Effect::Block>, int>>>
                  == 0x32894FE89819DEA1ULL,
              "Computation<EmptyRow, Computation<Row<Block>, int>> drifted.");
static_assert(row_hash_contribution_v<effects::Computation<EmptyRow, effects::Computation<Row<Effect::Bg>, int>>>
                  == 0xEDF6E609659BD93CULL,
              "Computation<EmptyRow, Computation<Row<Bg>, int>> drifted.");
static_assert(row_hash_contribution_v<effects::Computation<EmptyRow, effects::Computation<Row<Effect::Init>, int>>>
                  == 0x93C6E9DAD4DDF07AULL,
              "Computation<EmptyRow, Computation<Row<Init>, int>> drifted.");
static_assert(row_hash_contribution_v<effects::Computation<EmptyRow, effects::Computation<Row<Effect::Test>, int>>>
                  == 0x792A21E2C4F20C13ULL,
              "Computation<EmptyRow, Computation<Row<Test>, int>> drifted.");

// The reverse arrangement, with the row on the outside and the empty row
// within, must hash differently from the forms above. The combiner does
// not commute, and this pins that.

static_assert(row_hash_contribution_v<effects::Computation<Row<Effect::Bg>, effects::Computation<EmptyRow, int>>>
                  == 0x40D0E7791202A526ULL,
              "Computation<Row<Bg>, Computation<EmptyRow, int>> drifted — federation wire-format break.");

// One row nested inside itself must not collapse to the single form.

static_assert(row_hash_contribution_v<effects::Computation<Row<Effect::Bg>, effects::Computation<Row<Effect::Bg>, int>>>
                  == 0xAFCB34F7B12A2F95ULL,
              "Computation<Row<Bg>, Computation<Row<Bg>, int>> drifted — a row nested in itself must stay distinct "
              "from the single form.");

static_assert(
    row_hash_contribution_v<effects::Computation<Row<Effect::Alloc>, effects::Computation<Row<Effect::IO>, int>>>
        == 0xB25AFEA0CE322A7EULL,
    "Computation<Row<Alloc>, Computation<Row<IO>, int>> drifted.");

static_assert(
    row_hash_contribution_v<effects::Computation<
            Row<Effect::Alloc>, effects::Computation<Row<Effect::IO>, effects::Computation<Row<Effect::Block>, int>>>>
        == 0xAC3F22322B23C1FEULL,
    "the triple-nested carrier row_hash drifted — the chained fold must stay bit-stable");

static_assert(detail::sorted_uints(std::array<std::uint64_t, 0>{}) == std::array<std::uint64_t, 0>{});
static_assert(detail::sorted_uints(std::array<std::uint64_t, 1>{42}) == std::array<std::uint64_t, 1>{42});
static_assert(detail::sorted_uints(std::array<std::uint64_t, 3>{3, 1, 2}) == std::array<std::uint64_t, 3>{1, 2, 3});
static_assert(detail::sorted_uints(std::array<std::uint64_t, 4>{4, 3, 2, 1})
              == std::array<std::uint64_t, 4>{1, 2, 3, 4});
static_assert(detail::sorted_uints(std::array<std::uint64_t, 4>{1, 1, 1, 1})
              == std::array<std::uint64_t, 4>{1, 1, 1, 1});

static_assert(detail::unique_count_sorted(std::array<std::uint64_t, 0>{}) == 0);
static_assert(detail::unique_count_sorted(std::array<std::uint64_t, 1>{42}) == 1);
static_assert(detail::unique_count_sorted(std::array<std::uint64_t, 3>{1, 2, 3}) == 3);
static_assert(detail::unique_count_sorted(std::array<std::uint64_t, 4>{1, 1, 1, 1}) == 1);
static_assert(detail::unique_count_sorted(std::array<std::uint64_t, 4>{1, 1, 2, 2}) == 2);
static_assert(detail::unique_count_sorted(std::array<std::uint64_t, 5>{1, 1, 2, 3, 3}) == 3);
static_assert(detail::unique_count_sorted(std::array<std::uint64_t, 6>{0, 0, 1, 1, 2, 2}) == 3);

// Comparing the dedup fold against the plain fold over the canonical
// array is what pins the skip branch to the right side of its test.

static_assert(detail::fmix64_fold_unique_sorted(std::array<std::uint64_t, 1>{7}, 0xAA)
              == detail::fmix64_fold(std::array<std::uint64_t, 1>{7}, 0xAA));

static_assert(detail::fmix64_fold_unique_sorted(std::array<std::uint64_t, 2>{7, 7}, 0xAA)
              == detail::fmix64_fold(std::array<std::uint64_t, 1>{7}, 0xAA));

static_assert(detail::fmix64_fold_unique_sorted(std::array<std::uint64_t, 3>{1, 1, 2}, 0xBB)
              == detail::fmix64_fold(std::array<std::uint64_t, 2>{1, 2}, 0xBB));

static_assert(detail::fmix64_fold_unique_sorted(std::array<std::uint64_t, 0>{}, 0xCC) == 0xCC);

// Two probe templates of the same arity and different identity stand in
// for two real containers, so this header stays clear of the container
// headers.
template <typename T>
class FoundO49_ProbeA {
    T x_{};
};
template <typename T>
class FoundO49_ProbeB {
    T x_{};
};

static_assert(row_hash_contribution_v<safety::AppendOnly<int, FoundO49_ProbeA>>
                  != row_hash_contribution_v<safety::AppendOnly<int, FoundO49_ProbeB>>,
              "two append-only carriers over different storage policies share a row hash, so the storage policy is "
              "no longer folded in");

static_assert(row_hash_contribution_v<safety::AppendOnly<int, FoundO49_ProbeA>>
              == row_hash_contribution_v<safety::AppendOnly<int, FoundO49_ProbeA>>);

// The payload has to be a wrapper here. Two bare fundamentals both
// contribute zero, so only a wrapped payload can witness that the inner
// contribution reaches the outer fold.
static_assert(row_hash_contribution_v<safety::AppendOnly<safety::Linear<int>, FoundO49_ProbeA>>
                  != row_hash_contribution_v<safety::AppendOnly<safety::Stale<int>, FoundO49_ProbeA>>,
              "two append-only carriers over differently wrapped payloads share a row hash, so the inner "
              "contribution no longer reaches the outer fold");

static_assert(row_hash_contribution_v<safety::AppendOnly<int, FoundO49_ProbeA>> != 0);

static_assert(row_hash_contribution_v<safety::AppendOnly<safety::Linear<int>, FoundO49_ProbeA>>
              != row_hash_contribution_v<safety::AppendOnly<safety::Stale<int>, FoundO49_ProbeB>>);

// The toolchain tag is not a row hash. It only ever enters a key that
// spans toolchains, and folding it in leaves every published row hash
// and every pin above untouched.

static_assert(detail::FEDERATION_TOOLCHAIN_TAG != 0);
static_assert(federation_toolchain_tag() == detail::FEDERATION_TOOLCHAIN_TAG);

// A key carrying the tag is disjoint from the bare hash, which is what
// keeps the two kinds of peer from reading each other's slots.
static_assert(federation_key_with_toolchain_v<EmptyRow>.raw() != row_hash_contribution_v<EmptyRow>);
static_assert(federation_key_with_toolchain_v<Row<Effect::Alloc>>.raw() != row_hash_contribution_v<Row<Effect::Alloc>>);

// It keeps every property the bare hash has: row-discriminating,
// payload-blind, and clear of the empty-slot marker.
static_assert(federation_key_with_toolchain_v<Row<Effect::Alloc>> != federation_key_with_toolchain_v<Row<Effect::IO>>);
static_assert(federation_key_with_toolchain_v<Row<Effect::Alloc>> != federation_key_with_toolchain_v<EmptyRow>);

static_assert(federation_key_with_toolchain_v<effects::Computation<EmptyRow, int>>
              == federation_key_with_toolchain_v<effects::Computation<EmptyRow, double>>);

static_assert(federation_key_with_toolchain_v<EmptyRow>.raw() != static_cast<std::uint64_t>(-1));
static_assert(federation_key_with_toolchain_v<Row<Effect::Alloc>>.raw() != static_cast<std::uint64_t>(-1));

}  // namespace detail::row_hash_self_test

}  // namespace crucible::safety::diag
