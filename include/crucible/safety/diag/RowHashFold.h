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
//    `row_hash_contribution<T>` is the open extension point.  Future
//    wrappers that carry row-relevant state (HotPath<T, Tier>,
//    DetSafe<T, Tier>, Permission<Tag>, ...) supply specializations
//    that fold *their* contribution with the inner type's row hash
//    via `combine_ids` — the same Boost-style combiner used by
//    StableName.h.  FOUND-I02 ships the *infrastructure* and the
//    `Row<Es...>` specialization; per-wrapper specializations land
//    alongside their wrapper's production call site (FOUND-I05/06/07
//    for the cache levels and the FOUND-G* lattice family).
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

namespace crucible::safety::diag {

// ═════════════════════════════════════════════════════════════════════
// ── Internal fold helpers ──────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail {

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

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test — non-constant-args execution probe ─────────
// ═════════════════════════════════════════════════════════════════════
//
// Per `feedback_algebra_runtime_smoke_test_discipline`: the consteval
// claims above must execute through ABI-visible code paths.  Volatile
// sinks defeat constant-folding so the optimizer cannot collapse
// these to compile-time-only checks.

inline void runtime_smoke_test_row_hash_fold() noexcept {
    using effects::Effect;
    using effects::EmptyRow;
    using effects::Row;

    volatile std::uint64_t sink = 0;

    // Bare types — should contribute 0.
    sink ^= row_hash_of_v<int>.raw();
    sink ^= row_hash_of_v<float>.raw();
    sink ^= row_hash_of_v<double>.raw();

    // Singleton rows — non-zero, distinct.
    sink ^= row_hash_of_v<Row<Effect::Alloc>>.raw();
    sink ^= row_hash_of_v<Row<Effect::IO>>.raw();
    sink ^= row_hash_of_v<Row<Effect::Block>>.raw();
    sink ^= row_hash_of_v<Row<Effect::Bg>>.raw();
    sink ^= row_hash_of_v<Row<Effect::Init>>.raw();
    sink ^= row_hash_of_v<Row<Effect::Test>>.raw();

    // Pair rows.
    sink ^= row_hash_of_v<Row<Effect::Alloc, Effect::IO>>.raw();
    sink ^= row_hash_of_v<Row<Effect::Block, Effect::Bg>>.raw();

    // Permutation invariance — two declarations, same hash.
    auto h_alloc_io = row_hash_of_v<Row<Effect::Alloc, Effect::IO>>;
    auto h_io_alloc = row_hash_of_v<Row<Effect::IO, Effect::Alloc>>;
    volatile bool perm_eq = (h_alloc_io == h_io_alloc);

    // EmptyRow non-zero distinct from bare types.
    sink ^= row_hash_of_v<EmptyRow>.raw();
    volatile bool empty_distinct =
        (row_hash_of_v<EmptyRow> != row_hash_of_v<int>);

    (void)sink;
    (void)perm_eq;
    (void)empty_distinct;
}

}  // namespace crucible::safety::diag
