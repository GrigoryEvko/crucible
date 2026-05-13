#pragma once

// ── crucible::safety::diag — RowHash recursive fmix64 fold ─────────
//
// FOUND-I02.  Computes a 64-bit `RowHash` (Types.h, FOUND-I01) over a
// type T's *row signature* — the Met(X) effect-row content folded into
// a single content-addressable identifier.
//
// Foundation for the FOUND-I federation cache key:
//
//   KernelCacheKey { ContentHash, RowHash } ── FOUND-I01
//                                  └──── computed by THIS header (FOUND-I02)
//
// Two computations with byte-identical `content_hash` but different
// `row_hash` represent the *same computation under different effect
// regimes* — they MUST cache to distinct slots (CRUCIBLE.md §10,
// FORGE.md §23.2).  A Pure-row kernel is trivially federatable; an
// IO-row kernel is not; sharing a slot between them silently breaks
// the federation contract.
//
// ── Design contract ─────────────────────────────────────────────────
//
// 1. **Permutation-invariance.**  `Row<A, B>` and `Row<B, A>` share
//    semantics under the Subrow relation — the type system treats
//    them as equivalent (set semantics over effect atoms).  The hash
//    therefore canonicalizes the effect pack via sort-on-underlying-
//    value before folding.  Without this, two semantically-equivalent
//    cache keys would address different slots and federation would
//    fragment.
//
// 2. **Cardinality discrimination.**  `Row<A>` and `Row<A, B>` have
//    different semantics — the second carries a strictly stronger
//    capability claim (`A ⊑ A∪B`).  The fold visits every element, so
//    cardinality automatically participates in the hash.
//
// 3. **Bare-type baseline.**  A bare type (e.g. `int`, `float`) has
//    no row contribution; its `row_hash_contribution<T>::value == 0`.
//    This is the cache-default sentinel meaning "no row" and matches
//    the NSDMI default of `KernelCacheKey::row_hash`.
//
// 4. **`EmptyRow` is NOT zero.**  `Row<>` is a *real* row with zero
//    effects — semantically distinct from "no row" (a bare `int`).  We
//    seed the EmptyRow hash with the FNV-1a offset basis (mixed by
//    fmix64 once) so its bit pattern is fixed and non-zero.  Without
//    this distinction, `Computation<EmptyRow, int>` and a bare `int`
//    would alias, which is wrong: the former carries a row-typed
//    Met(X) carrier; the latter is just a payload.
//
// 5. **Federation V1 stability.**  `Effect` is an enum class with
//    fixed underlying values (Capabilities.h).  Hashing the underlying
//    `uint8_t` values yields the same RowHash on every compiler / TU
//    that includes the same `effects/Capabilities.h` revision.  This
//    is the V1 federation guarantee.  A breaking change to the
//    `Effect` enum (renumbering, deletion) is a CDAG_VERSION bump and
//    a wire-format break, exactly as documented in the Family-A
//    persistent-hash taxonomy in Types.h.
//
// 6. **Recursive fold over wrapper stack.**  The trait
//    `row_hash_contribution<T>` is the open extension point.  Wrappers
//    that carry row-relevant state supply specializations that fold
//    *their* contribution with the inner type's row hash via
//    `combine_ids` — the same Boost-style combiner used by
//    StableName.h.
//
// ── Currently shipped specializations (FOUND-I02 + I02-AUDIT) ──────
//
// As of this header revision, the row-bearing core and every wrapper
// in the canonical wrapper-nesting order are wired:
//
//   row_hash_contribution<effects::Row<Es...>>
//       — sort-fold over Effect underlying values; cardinality-
//         seeded.  See spec block below.
//
//   row_hash_contribution<effects::Computation<R, T>>
//       — combine_ids(R-hash, T-hash); payload-blind for bare T,
//         row-discriminating, nested-non-collapsing.  See spec
//         block below.
//
//   row_hash_contribution<W<...Inner...>>
//       — combine_ids(wrapper-stable-tag, Inner-hash) for:
//         HotPath / DetSafe / NumericalTier / Vendor / ResidencyHeat /
//         CipherTier / AllocClass / Wait / MemOrder / Progress / Stale /
//         Tagged / Refined / Secret / Linear.
//
// CLAUDE.md §XVI / FOUND-I03 documents this scope at user-facing
// resolution.
//
// ── References ──────────────────────────────────────────────────────
//
//   28_04_2026_effects.md §7.3 + §8     — design rationale
//   safety/diag/StableName.h            — FNV-1a + fmix64 primitives,
//                                         combine_ids
//   effects/EffectRow.h                 — Row<Es...> shape
//   effects/Capabilities.h              — Effect enum + values
//   Types.h                             — RowHash strong type
//   MerkleDag.h §9 KernelCache          — open-addressing cache that
//                                         consumes row-keyed entries
//
// FOUND-I02 — RowHash recursive fmix64 fold over wrapper stack.

#include <crucible/Expr.h>                       // detail::fmix64
#include <crucible/Platform.h>
#include <crucible/Types.h>                      // RowHash strong type
#include <crucible/effects/Capabilities.h>       // Effect enum
#include <crucible/effects/EffectRow.h>          // Row<Es...>
#include <crucible/safety/diag/StableName.h>     // FNV1A_OFFSET_BASIS, combine_ids

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// Forward-declare effects::Computation rather than pull in
// `effects/Computation.h`'s full graded substrate (which would
// transitively drag Graded.h, the lattice family, and stringly-typed
// reflection paths into every TU that includes RowHashFold).  A
// specialization of row_hash_contribution<effects::Computation<R, T>>
// only needs to deduce its template parameters; the class definition
// is irrelevant.  Any TU that USES Computation<...> as the argument
// type must include effects/Computation.h itself — that's the standard
// IWYU rule, not a constraint we add here.
namespace crucible::effects {
template <typename R, typename T> class Computation;
}  // namespace crucible::effects

namespace crucible::algebra::lattices {
enum class AllocClassTag : std::uint8_t;
enum class CipherTierTag : std::uint8_t;
enum class DetSafeTier : std::uint8_t;
enum class HotPathTier : std::uint8_t;
enum class MemOrderTag : std::uint8_t;
enum class ProgressClass : std::uint8_t;
enum class ResidencyHeatTag : std::uint8_t;
enum class Tolerance : std::uint8_t;
enum class VendorBackend : std::uint8_t;
enum class WaitStrategy : std::uint8_t;
}  // namespace crucible::algebra::lattices

namespace crucible::safety {
template <algebra::lattices::AllocClassTag Tag, typename T> class AllocClass;
template <algebra::lattices::CipherTierTag Tier, typename T> class CipherTier;
template <algebra::lattices::DetSafeTier Tier, typename T> class DetSafe;
template <algebra::lattices::HotPathTier Tier, typename T> class HotPath;
template <algebra::lattices::MemOrderTag Tag, typename T> class MemOrder;
template <algebra::lattices::ProgressClass Class, typename T> class Progress;
template <algebra::lattices::ResidencyHeatTag Tier, typename T> class ResidencyHeat;
template <algebra::lattices::Tolerance Tier, typename T> class NumericalTier;
template <algebra::lattices::VendorBackend Backend, typename T> class Vendor;
template <algebra::lattices::WaitStrategy Strategy, typename T> class Wait;
template <typename T> class Linear;
template <auto Pred, typename T> class Refined;
template <typename T> class Secret;
template <typename T> class Stale;
template <typename T, typename Tag> class Tagged;
}  // namespace crucible::safety

namespace crucible::safety::diag {

// ═════════════════════════════════════════════════════════════════════
// ── Internal fold helpers ──────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail {

inline constexpr std::uint64_t WRAPPER_HOTPATH_TAG        = 0x0100'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_DETSAFE_TAG        = 0x0200'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_NUMERICAL_TIER_TAG = 0x0300'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_VENDOR_TAG         = 0x0400'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_RESIDENCY_HEAT_TAG = 0x0500'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_CIPHER_TIER_TAG    = 0x0600'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_ALLOC_CLASS_TAG    = 0x0700'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_WAIT_TAG           = 0x0800'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_MEM_ORDER_TAG      = 0x0900'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_PROGRESS_TAG       = 0x0A00'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_STALE_TAG          = 0x0B00'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_TAGGED_TAG         = 0x0C00'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_REFINED_TAG        = 0x0D00'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_SECRET_TAG         = 0x0E00'0000'0000'0000ULL;
inline constexpr std::uint64_t WRAPPER_LINEAR_TAG         = 0x0F00'0000'0000'0000ULL;

// Bubble-sort a fixed-size std::array<uint64_t, N> in place at
// consteval.  N is bounded by `effects::effect_count` (≤ 64 by
// axiom — EffectRowLattice's defensive cap, FOUND-H01-AUDIT-2), so
// O(N²) is fine and cheaper than introducing <algorithm> dependence
// for a bounded compile-time problem.
template <std::size_t N>
[[nodiscard]] consteval std::array<std::uint64_t, N>
sorted_uints(std::array<std::uint64_t, N> xs) noexcept {
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

// fmix64-fold over an already-canonical (sorted) array.  The seed
// MUST encode cardinality (caller's responsibility) — otherwise an
// effect with underlying value 0 (currently `Effect::Alloc`) silently
// collides with `EmptyRow`: `fmix64(seed ^ 0) == fmix64(seed)`.
template <std::size_t N>
[[nodiscard]] consteval std::uint64_t
fmix64_fold(std::array<std::uint64_t, N> const& xs,
            std::uint64_t seed) noexcept {
    std::uint64_t h = seed;
    for (std::size_t i = 0; i < N; ++i) {
        h = ::crucible::detail::fmix64(h ^ xs[i]);
    }
    return h;
}

// Cardinality-mixing seed factory.  Every row hash starts here:
// the seed FNV1A_OFFSET_BASIS is XOR'd with N (the row's
// cardinality) and then mixed.  This guarantees Row<X> with N
// effects and Row<Y> with M ≠ N effects cannot collide regardless of
// XOR-fold coincidences inside the body.
[[nodiscard]] consteval std::uint64_t
cardinality_seed(std::uint64_t cardinality) noexcept {
    return ::crucible::detail::fmix64(FNV1A_OFFSET_BASIS ^ cardinality);
}

// EmptyRow hash — the seed produced for cardinality 0.  Pulled out
// so the constant is shared between the EmptyRow specialization and
// the self-test, and so any future audit can compare against it.
inline constexpr std::uint64_t EMPTY_ROW_HASH = cardinality_seed(0);

}  // namespace detail

// ═════════════════════════════════════════════════════════════════════
// ── row_hash_contribution<T> — open extension point ────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Primary template: any T contributes 0 to its row hash.  Specialized
// per row-bearing type kind:
//   • effects::Row<Es...>   (THIS file — sort-fold over Effect values)
//   • effects::Computation<R, T>  (THIS file — delegates to R)
//   • HotPath<T, Tier> / DetSafe<T, Tier> / ...  (FOUND-I05/06/07)
//
// Specializations supplied OUTSIDE this file MUST live in the same
// `crucible::safety::diag` namespace and follow the recursive
// composition discipline:
//
//   row_hash_contribution<W<T, ...attrs...>>::value =
//       combine_ids(<W's tag bits>, row_hash_contribution<T>::value)
//
// where `combine_ids` is the Boost-style combiner from StableName.h.
// `combine_ids` is order-sensitive — wrapper position in the stack
// matters (HotPath<DetSafe<T>> ≢ DetSafe<HotPath<T>>).  This is the
// canonical wrapper-nesting order documented in FOUND-I03.

template <typename T>
struct row_hash_contribution {
    static constexpr std::uint64_t value = 0;
};

template <typename T>
inline constexpr std::uint64_t row_hash_contribution_v =
    row_hash_contribution<T>::value;

// ═════════════════════════════════════════════════════════════════════
// ── Row<Es...> specialization — sort-fold over Effect values ───────
// ═════════════════════════════════════════════════════════════════════
//
// Permutation-invariant by construction: the underlying Effect values
// are extracted into a fixed-size array, sorted, then fmix64-folded.
// Two rows with the same effect set hash identically regardless of
// declaration order.

template <effects::Effect... Es>
struct row_hash_contribution<effects::Row<Es...>> {
    static constexpr std::uint64_t value = []() consteval -> std::uint64_t {
        constexpr std::size_t N = sizeof...(Es);
        // Cardinality is mixed FIRST so different N values cannot
        // collide regardless of how the body's XOR-fold lands.  The
        // bug this guards against: Effect::Alloc has underlying
        // value 0, and `fmix64(seed ^ 0) == fmix64(seed)`, so
        // without cardinality-seeding `Row<Alloc>` would alias
        // `EmptyRow` whose seed is the same FNV-1a offset basis.
        constexpr std::uint64_t seed = detail::cardinality_seed(N);
        if constexpr (N == 0) {
            return seed;
        } else {
            std::array<std::uint64_t, N> const raw_vals{
                static_cast<std::uint64_t>(
                    static_cast<std::underlying_type_t<effects::Effect>>(Es))...
            };
            return detail::fmix64_fold(detail::sorted_uints(raw_vals), seed);
        }
    }();
};

// ═════════════════════════════════════════════════════════════════════
// ── Computation<R, T> specialization — the primary Met(X) consumer ─
// ═════════════════════════════════════════════════════════════════════
//
// `effects::Computation<R, T>` is the canonical row-typed Met(X)
// carrier (Computation.h, FOUND-B, Tang-Lindley POPL 2026).  It is
// THE primary consumer of row hashes: the federation cache key for
// any compiled kernel that lifts its result through a row carrier
// must reflect *both* the row R and the payload T's row contribution.
//
// Two design constraints any specialization here must satisfy:
//
//   (1) **Distinct from bare R.**  `row_hash_contribution_v<Row<A>>`
//       must not equal `row_hash_contribution_v<Computation<Row<A>,
//       int>>`.  A bare row is metadata; a Computation is a value.
//       The cache must distinguish them or two semantically-different
//       calls would collide at the same slot.
//
//   (2) **Payload-blind for bare T.**  `Computation<R, int>` and
//       `Computation<R, double>` MUST hash identically — the cache
//       key is row-shape, not payload-shape (payload identity is
//       carried by `ContentHash`, the other half of KernelCacheKey).
//
//   (3) **Non-collapsing for row-bearing T.**  When T is itself row-
//       bearing (e.g. nested `Computation<Row<IO>, int>` inside a
//       `Computation<EmptyRow, ...>`), the inner row participates in
//       the outer hash so that nested vs. flat carriers cannot alias.
//       This protects the cache against a pathological "monad-in-
//       monad" stash that semantically differs from the flattened
//       form even before `then` collapses it.
//
// `combine_ids(row_hash_v<R>, row_hash_v<T>)` satisfies all three:
//
//   - combine_ids is order-sensitive and Boost-style golden-ratio
//     mixed; for any non-trivial X, `combine_ids(X, 0) ≠ X` —
//     guarantees (1) when T is a bare type contributing 0.
//   - For T1, T2 with `row_hash_contribution_v<T1> ==
//     row_hash_contribution_v<T2> == 0` (bare types), the result is
//     identical — guarantees (2).
//   - When T is row-bearing, its non-zero contribution flows in —
//     guarantees (3).
//
// Order-sensitivity (combine_ids(R, T) ≠ combine_ids(T, R)) is
// deliberate.  It pins the canonical ROW-FIRST nesting order: the
// row R is "outer", the payload T is "inner".  This is the same
// nesting documented for FOUND-I03's wrapper-stack discipline.

template <typename R, typename T>
struct row_hash_contribution<effects::Computation<R, T>> {
    static constexpr std::uint64_t value =
        detail::combine_ids(
            row_hash_contribution_v<R>,
            row_hash_contribution_v<T>);
};

// ═════════════════════════════════════════════════════════════════════
// ── Canonical safety-wrapper stack specializations ─────────────────
// ═════════════════════════════════════════════════════════════════════
//
// These specializations keep the row hash sensitive to wrapper order.
// Wrapper attributes participate where they are part of the type-level
// semantics: enum tiers fold their underlying value, Tagged folds the
// source tag type, and Refined folds the predicate object's type.  The
// wrapper tag itself lives in the high byte so enum values cannot
// collide across wrapper families.

template <algebra::lattices::HotPathTier Tier, typename Inner>
struct row_hash_contribution<safety::HotPath<Tier, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_HOTPATH_TAG | static_cast<std::uint64_t>(Tier),
        row_hash_contribution_v<Inner>);
};

template <algebra::lattices::DetSafeTier Tier, typename Inner>
struct row_hash_contribution<safety::DetSafe<Tier, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_DETSAFE_TAG | static_cast<std::uint64_t>(Tier),
        row_hash_contribution_v<Inner>);
};

template <algebra::lattices::Tolerance Tier, typename Inner>
struct row_hash_contribution<safety::NumericalTier<Tier, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_NUMERICAL_TIER_TAG | static_cast<std::uint64_t>(Tier),
        row_hash_contribution_v<Inner>);
};

template <algebra::lattices::VendorBackend Backend, typename Inner>
struct row_hash_contribution<safety::Vendor<Backend, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_VENDOR_TAG | static_cast<std::uint64_t>(Backend),
        row_hash_contribution_v<Inner>);
};

template <algebra::lattices::ResidencyHeatTag Tier, typename Inner>
struct row_hash_contribution<safety::ResidencyHeat<Tier, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_RESIDENCY_HEAT_TAG | static_cast<std::uint64_t>(Tier),
        row_hash_contribution_v<Inner>);
};

template <algebra::lattices::CipherTierTag Tier, typename Inner>
struct row_hash_contribution<safety::CipherTier<Tier, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_CIPHER_TIER_TAG | static_cast<std::uint64_t>(Tier),
        row_hash_contribution_v<Inner>);
};

template <algebra::lattices::AllocClassTag Tag, typename Inner>
struct row_hash_contribution<safety::AllocClass<Tag, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_ALLOC_CLASS_TAG | static_cast<std::uint64_t>(Tag),
        row_hash_contribution_v<Inner>);
};

template <algebra::lattices::WaitStrategy Strategy, typename Inner>
struct row_hash_contribution<safety::Wait<Strategy, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_WAIT_TAG | static_cast<std::uint64_t>(Strategy),
        row_hash_contribution_v<Inner>);
};

template <algebra::lattices::MemOrderTag Tag, typename Inner>
struct row_hash_contribution<safety::MemOrder<Tag, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_MEM_ORDER_TAG | static_cast<std::uint64_t>(Tag),
        row_hash_contribution_v<Inner>);
};

template <algebra::lattices::ProgressClass Class, typename Inner>
struct row_hash_contribution<safety::Progress<Class, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_PROGRESS_TAG | static_cast<std::uint64_t>(Class),
        row_hash_contribution_v<Inner>);
};

template <typename Inner>
struct row_hash_contribution<safety::Stale<Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_STALE_TAG,
        row_hash_contribution_v<Inner>);
};

template <typename Inner, typename Source>
struct row_hash_contribution<safety::Tagged<Inner, Source>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::combine_ids(
            detail::WRAPPER_TAGGED_TAG,
            stable_type_id<Source>),
        row_hash_contribution_v<Inner>);
};

template <auto Pred, typename Inner>
struct row_hash_contribution<safety::Refined<Pred, Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::combine_ids(
            detail::WRAPPER_REFINED_TAG,
            stable_type_id<std::remove_cvref_t<decltype(Pred)>>),
        row_hash_contribution_v<Inner>);
};

template <typename Inner>
struct row_hash_contribution<safety::Secret<Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_SECRET_TAG,
        row_hash_contribution_v<Inner>);
};

template <typename Inner>
struct row_hash_contribution<safety::Linear<Inner>> {
    static constexpr std::uint64_t value = detail::combine_ids(
        detail::WRAPPER_LINEAR_TAG,
        row_hash_contribution_v<Inner>);
};

// ═════════════════════════════════════════════════════════════════════
// ── Top-level entry points ─────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// `row_hash_of<T>()` — consteval function returning RowHash.  Use
// this when you need to capture the result into a non-template local.
//
// `row_hash_of_v<T>` — variable template form for static_assert.

template <typename T>
[[nodiscard]] consteval RowHash row_hash_of() noexcept {
    return RowHash{row_hash_contribution_v<T>};
}

template <typename T>
inline constexpr RowHash row_hash_of_v = row_hash_of<T>();

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block — invariants asserted at header inclusion ──────
// ═════════════════════════════════════════════════════════════════════

namespace detail::row_hash_self_test {

using effects::Effect;
using effects::EmptyRow;
using effects::Row;

// ─── Bare types contribute 0 ───────────────────────────────────────

static_assert(row_hash_contribution_v<int>      == 0);
static_assert(row_hash_contribution_v<float>    == 0);
static_assert(row_hash_contribution_v<double>   == 0);
static_assert(row_hash_contribution_v<void>     == 0);
static_assert(row_hash_contribution_v<unsigned> == 0);

static_assert(row_hash_of_v<int>   == RowHash{0});
static_assert(row_hash_of_v<float> == RowHash{0});

// Bare-type RowHash is_zero() — flows through to KernelCacheKey
// behavior: a (ContentHash, RowHash{0}) is NOT a sentinel slot, just
// a row-defaulted entry.
static_assert(!row_hash_of_v<int>.is_sentinel());

// ─── Singleton rows produce non-zero, distinct hashes ──────────────

static_assert(row_hash_contribution_v<Row<Effect::Alloc>> != 0);
static_assert(row_hash_contribution_v<Row<Effect::IO>>    != 0);
static_assert(row_hash_contribution_v<Row<Effect::Block>> != 0);
static_assert(row_hash_contribution_v<Row<Effect::Bg>>    != 0);
static_assert(row_hash_contribution_v<Row<Effect::Init>>  != 0);
static_assert(row_hash_contribution_v<Row<Effect::Test>>  != 0);

static_assert(row_hash_contribution_v<Row<Effect::Alloc>>
           != row_hash_contribution_v<Row<Effect::IO>>);
static_assert(row_hash_contribution_v<Row<Effect::Alloc>>
           != row_hash_contribution_v<Row<Effect::Block>>);
static_assert(row_hash_contribution_v<Row<Effect::IO>>
           != row_hash_contribution_v<Row<Effect::Bg>>);

// ─── Permutation invariance — pair, triple, full sextuple ──────────

// 2-way: Row<A,B> ≡ Row<B,A>.
static_assert(row_hash_contribution_v<Row<Effect::Alloc, Effect::IO>>
           == row_hash_contribution_v<Row<Effect::IO, Effect::Alloc>>);

static_assert(row_hash_contribution_v<Row<Effect::Block, Effect::Bg>>
           == row_hash_contribution_v<Row<Effect::Bg, Effect::Block>>);

// 3-way: all 6 permutations of {Alloc, IO, Block} hash identically.
static_assert(
    row_hash_contribution_v<Row<Effect::Alloc, Effect::IO, Effect::Block>>
 == row_hash_contribution_v<Row<Effect::Alloc, Effect::Block, Effect::IO>>);
static_assert(
    row_hash_contribution_v<Row<Effect::Alloc, Effect::IO, Effect::Block>>
 == row_hash_contribution_v<Row<Effect::IO, Effect::Alloc, Effect::Block>>);
static_assert(
    row_hash_contribution_v<Row<Effect::Alloc, Effect::IO, Effect::Block>>
 == row_hash_contribution_v<Row<Effect::IO, Effect::Block, Effect::Alloc>>);
static_assert(
    row_hash_contribution_v<Row<Effect::Alloc, Effect::IO, Effect::Block>>
 == row_hash_contribution_v<Row<Effect::Block, Effect::Alloc, Effect::IO>>);
static_assert(
    row_hash_contribution_v<Row<Effect::Alloc, Effect::IO, Effect::Block>>
 == row_hash_contribution_v<Row<Effect::Block, Effect::IO, Effect::Alloc>>);

// 6-way (full universe): a permutation of all six atoms hashes the
// same as the canonical declaration order.
using FullRow_canonical =
    Row<Effect::Alloc, Effect::IO, Effect::Block,
        Effect::Bg,    Effect::Init, Effect::Test>;
using FullRow_reversed =
    Row<Effect::Test,  Effect::Init, Effect::Bg,
        Effect::Block, Effect::IO,   Effect::Alloc>;
using FullRow_shuffled =
    Row<Effect::Block, Effect::Alloc, Effect::Test,
        Effect::IO,    Effect::Init,  Effect::Bg>;

static_assert(row_hash_contribution_v<FullRow_canonical>
           == row_hash_contribution_v<FullRow_reversed>);
static_assert(row_hash_contribution_v<FullRow_canonical>
           == row_hash_contribution_v<FullRow_shuffled>);

// ─── Cardinality discriminates ─────────────────────────────────────
//
// Adding an effect changes the hash.  This is essential — the row
// algebra has Row<A> ⊊ Row<A, B>, and the cache must reflect the
// strictly stronger capability claim.

static_assert(row_hash_contribution_v<Row<Effect::Alloc>>
           != row_hash_contribution_v<Row<Effect::Alloc, Effect::IO>>);

static_assert(row_hash_contribution_v<Row<Effect::Alloc, Effect::IO>>
           != row_hash_contribution_v<Row<Effect::Alloc, Effect::IO, Effect::Block>>);

static_assert(row_hash_contribution_v<Row<Effect::Alloc>>
           != row_hash_contribution_v<Row<Effect::IO>>);

// Disjoint pairs hash differently.
static_assert(row_hash_contribution_v<Row<Effect::Alloc, Effect::IO>>
           != row_hash_contribution_v<Row<Effect::Block, Effect::Bg>>);

// ─── EmptyRow is non-zero, distinct from bare-type 0 ───────────────

static_assert(row_hash_contribution_v<EmptyRow> != 0);
static_assert(row_hash_contribution_v<EmptyRow> == detail::EMPTY_ROW_HASH);

// EmptyRow ≢ bare-type 0 — the row carrier must always discriminate
// from a non-row T.
static_assert(row_hash_contribution_v<EmptyRow>
           != row_hash_contribution_v<int>);

// EmptyRow ≢ any singleton row — the empty effect set is not the
// same as a single-effect set.  REGRESSION ANCHOR: this enumeration
// triggered the audit catch that motivated the cardinality-seeded
// fold.  Effect::Alloc has underlying value 0, and the XOR-fold's
// `seed ^ 0 == seed` identity made `Row<Alloc>` alias `EmptyRow`
// before the cardinality_seed() fix.  Every singleton over the full
// Effect universe is now pinned distinct from EmptyRow.
static_assert(row_hash_contribution_v<EmptyRow>
           != row_hash_contribution_v<Row<Effect::Alloc>>);
static_assert(row_hash_contribution_v<EmptyRow>
           != row_hash_contribution_v<Row<Effect::IO>>);
static_assert(row_hash_contribution_v<EmptyRow>
           != row_hash_contribution_v<Row<Effect::Block>>);
static_assert(row_hash_contribution_v<EmptyRow>
           != row_hash_contribution_v<Row<Effect::Bg>>);
static_assert(row_hash_contribution_v<EmptyRow>
           != row_hash_contribution_v<Row<Effect::Init>>);
static_assert(row_hash_contribution_v<EmptyRow>
           != row_hash_contribution_v<Row<Effect::Test>>);

// All 15 distinct singleton-pair comparisons — every pair of
// singleton rows must hash to different values.  Exhaustive over
// Effect × Effect (modulo symmetry).  Catches any future Effect
// renumbering that creates a value-collision.

static_assert(row_hash_contribution_v<Row<Effect::Alloc>>
           != row_hash_contribution_v<Row<Effect::IO>>);
static_assert(row_hash_contribution_v<Row<Effect::Alloc>>
           != row_hash_contribution_v<Row<Effect::Block>>);
static_assert(row_hash_contribution_v<Row<Effect::Alloc>>
           != row_hash_contribution_v<Row<Effect::Bg>>);
static_assert(row_hash_contribution_v<Row<Effect::Alloc>>
           != row_hash_contribution_v<Row<Effect::Init>>);
static_assert(row_hash_contribution_v<Row<Effect::Alloc>>
           != row_hash_contribution_v<Row<Effect::Test>>);
static_assert(row_hash_contribution_v<Row<Effect::IO>>
           != row_hash_contribution_v<Row<Effect::Block>>);
static_assert(row_hash_contribution_v<Row<Effect::IO>>
           != row_hash_contribution_v<Row<Effect::Bg>>);
static_assert(row_hash_contribution_v<Row<Effect::IO>>
           != row_hash_contribution_v<Row<Effect::Init>>);
static_assert(row_hash_contribution_v<Row<Effect::IO>>
           != row_hash_contribution_v<Row<Effect::Test>>);
static_assert(row_hash_contribution_v<Row<Effect::Block>>
           != row_hash_contribution_v<Row<Effect::Bg>>);
static_assert(row_hash_contribution_v<Row<Effect::Block>>
           != row_hash_contribution_v<Row<Effect::Init>>);
static_assert(row_hash_contribution_v<Row<Effect::Block>>
           != row_hash_contribution_v<Row<Effect::Test>>);
static_assert(row_hash_contribution_v<Row<Effect::Bg>>
           != row_hash_contribution_v<Row<Effect::Init>>);
static_assert(row_hash_contribution_v<Row<Effect::Bg>>
           != row_hash_contribution_v<Row<Effect::Test>>);
static_assert(row_hash_contribution_v<Row<Effect::Init>>
           != row_hash_contribution_v<Row<Effect::Test>>);

// ─── row_hash_of_v wraps the raw u64 in a RowHash strongly typed ───

static_assert(std::is_same_v<decltype(row_hash_of_v<int>), const RowHash>);
static_assert(row_hash_of_v<EmptyRow>.raw() == detail::EMPTY_ROW_HASH);

// Round-trip: row_hash_of equals raw RowHash construction.
static_assert(row_hash_of_v<Row<Effect::Alloc>>
           == RowHash{row_hash_contribution_v<Row<Effect::Alloc>>});

// ─── No accidental sentinel collision ──────────────────────────────
//
// Real row hashes must not collide with the cache EMPTY-slot marker
// (RowHash::sentinel() == UINT64_MAX).  fmix64 distributes uniformly
// over the full 64-bit range; the probability of any specific value
// is ~2^-64 per row.  Spot-check the rows we actually use.

static_assert(row_hash_contribution_v<EmptyRow>
           != static_cast<std::uint64_t>(-1));
static_assert(row_hash_contribution_v<Row<Effect::Alloc>>
           != static_cast<std::uint64_t>(-1));
static_assert(row_hash_contribution_v<FullRow_canonical>
           != static_cast<std::uint64_t>(-1));

// Same sentinel discipline for every singleton — any future Effect
// renumbering that lands a row hash on UINT64_MAX would silently
// poison the cache (real row claims an EMPTY slot).  Cheap to check,
// catastrophic to miss.
static_assert(row_hash_contribution_v<Row<Effect::IO>>
           != static_cast<std::uint64_t>(-1));
static_assert(row_hash_contribution_v<Row<Effect::Block>>
           != static_cast<std::uint64_t>(-1));
static_assert(row_hash_contribution_v<Row<Effect::Bg>>
           != static_cast<std::uint64_t>(-1));
static_assert(row_hash_contribution_v<Row<Effect::Init>>
           != static_cast<std::uint64_t>(-1));
static_assert(row_hash_contribution_v<Row<Effect::Test>>
           != static_cast<std::uint64_t>(-1));

// ─── Computation<R, T> — payload-blind, row-discriminating ─────────
//
// AUDIT REGRESSION ANCHORS (FOUND-I02-AUDIT, 2026-04-30): closing the
// gap that motivated the audit pass — without these, `Computation`
// (the primary Met(X) consumer of row hashes) had NO specialization
// and fell through to the primary template's `value = 0`, aliasing
// every bare type and every other Computation instantiation.

// (a) Distinct from bare types.  A Computation<EmptyRow, int> is a
// row-typed carrier; a bare int has no row.  Cache must distinguish.
static_assert(row_hash_contribution_v<effects::Computation<EmptyRow, int>>
           != row_hash_contribution_v<int>);
static_assert(row_hash_contribution_v<effects::Computation<EmptyRow, int>> != 0);

// (b) Distinct from the bare row.  Combining the row with the (zero-
// contribution) payload via combine_ids changes the value; this is
// exactly what (combine_ids(X, 0) ≠ X) buys us — without it,
// Computation<EmptyRow, int> would alias EmptyRow and the cache could
// not tell "the carrier" from "the row metadata".
static_assert(row_hash_contribution_v<effects::Computation<EmptyRow, int>>
           != row_hash_contribution_v<EmptyRow>);
static_assert(
    row_hash_contribution_v<effects::Computation<Row<Effect::Alloc>, int>>
 != row_hash_contribution_v<Row<Effect::Alloc>>);
static_assert(
    row_hash_contribution_v<effects::Computation<Row<Effect::IO>, int>>
 != row_hash_contribution_v<Row<Effect::IO>>);

// (c) Payload-blind for bare T.  ContentHash carries payload identity;
// row_hash MUST be payload-blind so a kernel that returns int and one
// that returns double share row signatures (different cache slots
// thanks to ContentHash, same row).
static_assert(row_hash_contribution_v<effects::Computation<EmptyRow, int>>
           == row_hash_contribution_v<effects::Computation<EmptyRow, double>>);
static_assert(row_hash_contribution_v<effects::Computation<EmptyRow, int>>
           == row_hash_contribution_v<effects::Computation<EmptyRow, float>>);
static_assert(
    row_hash_contribution_v<effects::Computation<Row<Effect::Alloc>, int>>
 == row_hash_contribution_v<effects::Computation<Row<Effect::Alloc>, char>>);

// (d) Row-discriminating.  Same payload, different row → different
// hash.  Federation correctness depends on this: an Alloc-row kernel
// and an IO-row kernel that compute the same int must NOT share a
// cache slot.
static_assert(
    row_hash_contribution_v<effects::Computation<Row<Effect::Alloc>, int>>
 != row_hash_contribution_v<effects::Computation<Row<Effect::IO>, int>>);
static_assert(
    row_hash_contribution_v<effects::Computation<Row<Effect::Alloc>, int>>
 != row_hash_contribution_v<effects::Computation<EmptyRow, int>>);

// (e) Permutation invariance lifts through Computation — Row<A,B> and
// Row<B,A> hash identically inside the carrier.  Direct corollary of
// the inner Row<Es...> specialization, but pin it explicitly so a
// future combine_ids implementation change can't silently break it.
static_assert(
    row_hash_contribution_v<
        effects::Computation<Row<Effect::Alloc, Effect::IO>, int>>
 == row_hash_contribution_v<
        effects::Computation<Row<Effect::IO, Effect::Alloc>, int>>);

// (f) Cardinality discrimination lifts through Computation — Row<A>
// strictly less than Row<A,B>, so wrapping each in Computation cannot
// alias.  Direct corollary, pinned explicitly.
static_assert(
    row_hash_contribution_v<effects::Computation<Row<Effect::Alloc>, int>>
 != row_hash_contribution_v<
        effects::Computation<Row<Effect::Alloc, Effect::IO>, int>>);

// (g) Nested Computation — the inner row participates.  This is the
// "monad-in-monad" non-collapsing guarantee: a `Computation<EmptyRow,
// Computation<Row<IO>, int>>` carries a non-zero T contribution that
// MUST flow up into the outer hash, distinguishing it from the flat
// `Computation<EmptyRow, int>`.  This protects the cache against
// pathological nested stashes that semantically differ even before
// `then` flattens them.
static_assert(
    row_hash_contribution_v<
        effects::Computation<EmptyRow,
            effects::Computation<Row<Effect::IO>, int>>>
 != row_hash_contribution_v<effects::Computation<EmptyRow, int>>);
static_assert(
    row_hash_contribution_v<
        effects::Computation<EmptyRow,
            effects::Computation<Row<Effect::IO>, int>>>
 != row_hash_contribution_v<effects::Computation<Row<Effect::IO>, int>>);

// (h) Computation<R, T> never sentinel-collides on common rows.
static_assert(row_hash_contribution_v<effects::Computation<EmptyRow, int>>
           != static_cast<std::uint64_t>(-1));
static_assert(
    row_hash_contribution_v<effects::Computation<FullRow_canonical, int>>
 != static_cast<std::uint64_t>(-1));

// ─── Federation hash stability — the wire-format pin (FOUND-I04) ───
//
// PINNED CANONICAL ROW HASHES.  These hex literals are the bit-for-
// bit federation contract: every L1 / L2 / L3 cache entry whose key
// includes one of these row hashes encodes that exact 64-bit value
// in its serialized form.  Any change here — to the underlying
// Effect values (Capabilities.h), to fmix64 / FNV1A_OFFSET_BASIS
// (Expr.h / StableName.h), or to the cardinality_seed / fmix64_fold
// algorithm (this header) — invalidates every published cache entry
// across every fleet on the network.
//
// **DO NOT EDIT THESE LITERALS** unless you are deliberately
// performing the federation-cache wire-format-break ceremony
// documented in `effects/Capabilities.h` § "Major-version event
// procedure".  A drift WITHOUT that ceremony is silent corruption
// of every federation peer's cache.
//
// The comprehensive set covers: EmptyRow + every singleton + a
// representative pair + the full-Universe row.  Adding new pins
// for additional rows is fine; CHANGING existing pins is the
// wire-format break.

static_assert(row_hash_contribution_v<EmptyRow>
           == 0xEFD01F60BA992926ULL,
    "EmptyRow row_hash drifted — federation wire-format break.  "
    "See FOUND-I04 ceremony in Capabilities.h.");
static_assert(row_hash_contribution_v<Row<Effect::Alloc>>
           == 0x436DAF9EDCB565C3ULL,
    "Row<Alloc> row_hash drifted — federation wire-format break.");
static_assert(row_hash_contribution_v<Row<Effect::IO>>
           == 0x6FBFD0F707B63BECULL,
    "Row<IO> row_hash drifted — federation wire-format break.");
static_assert(row_hash_contribution_v<Row<Effect::Block>>
           == 0x3117F06B828C9247ULL,
    "Row<Block> row_hash drifted — federation wire-format break.");
static_assert(row_hash_contribution_v<Row<Effect::Bg>>
           == 0x008A519814C8FC81ULL,
    "Row<Bg> row_hash drifted — federation wire-format break.");
static_assert(row_hash_contribution_v<Row<Effect::Init>>
           == 0x9E23FC5AC81DA675ULL,
    "Row<Init> row_hash drifted — federation wire-format break.");
static_assert(row_hash_contribution_v<Row<Effect::Test>>
           == 0x26A9EB08E748D58FULL,
    "Row<Test> row_hash drifted — federation wire-format break.");
static_assert(row_hash_contribution_v<Row<Effect::Alloc, Effect::IO>>
           == 0x6CC046F52E6D7663ULL,
    "Row<Alloc, IO> row_hash drifted — federation wire-format break.");
static_assert(row_hash_contribution_v<Row<Effect::Alloc, Effect::IO,
                Effect::Block, Effect::Bg, Effect::Init, Effect::Test>>
           == 0x1C9D0E4F548FAAD6ULL,
    "Full-Universe row row_hash drifted — federation wire-format break.");

// ─── Computation<R, T> hash pins (FOUND-I04 + FOUND-I02-AUDIT) ─────
//
// The Computation<R, T> specialization (FOUND-I02-AUDIT) folds the
// row R via combine_ids with the payload T's row contribution.  Pin
// canonical values to detect drift in EITHER:
//   • the combine_ids algorithm (StableName.h),
//   • the Computation<R, T> specialization itself (this file), or
//   • the inner Row<Es...> contributions (already pinned above).
//
// Coverage:
//   (1) Empty-row + bare payload  — combine_ids(EMPTY_ROW_HASH, 0)
//   (2) Singleton row + bare payload — Bg-context kernel return value
//   (3) Pair-row + bare payload — Alloc+IO carrier
//   (4) Nested Computation — outer empty, inner has IO row.  Pins
//       that nested non-collapsing flows through combine_ids.
//
// Same federation-wire-format-break severity as the Row pins above.

static_assert(row_hash_contribution_v<effects::Computation<EmptyRow, int>>
           == 0x49A55BE1CFC23FB0ULL,
    "Computation<EmptyRow, int> row_hash drifted — federation wire-"
    "format break.  Either combine_ids or Computation<R, T> "
    "specialization changed.");
static_assert(
    row_hash_contribution_v<effects::Computation<Row<Effect::Bg>, int>>
 == 0x3ACE35615F0F9243ULL,
    "Computation<Row<Bg>, int> row_hash drifted — wire-format break.");
static_assert(
    row_hash_contribution_v<effects::Computation<
        Row<Effect::Alloc, Effect::IO>, int>>
 == 0x83D432DE6CDEACA7ULL,
    "Computation<Row<Alloc, IO>, int> row_hash drifted — break.");
static_assert(
    row_hash_contribution_v<effects::Computation<EmptyRow,
        effects::Computation<Row<Effect::IO>, int>>>
 == 0x94EC56B861A6B8FDULL,
    "Nested Computation<EmptyRow, Computation<Row<IO>, int>> "
    "row_hash drifted — wire-format break.  Inner-row "
    "non-collapsing through combine_ids must remain bit-stable.");

// ─── Bubble-sort helper correctness ────────────────────────────────

static_assert(detail::sorted_uints(std::array<std::uint64_t, 0>{})
           == std::array<std::uint64_t, 0>{});
static_assert(detail::sorted_uints(std::array<std::uint64_t, 1>{42})
           == std::array<std::uint64_t, 1>{42});
static_assert(detail::sorted_uints(std::array<std::uint64_t, 3>{3, 1, 2})
           == std::array<std::uint64_t, 3>{1, 2, 3});
static_assert(detail::sorted_uints(std::array<std::uint64_t, 4>{4, 3, 2, 1})
           == std::array<std::uint64_t, 4>{1, 2, 3, 4});
static_assert(detail::sorted_uints(std::array<std::uint64_t, 4>{1, 1, 1, 1})
           == std::array<std::uint64_t, 4>{1, 1, 1, 1});

}  // namespace detail::row_hash_self_test

}  // namespace crucible::safety::diag
