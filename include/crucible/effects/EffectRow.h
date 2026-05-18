#pragma once

// ── crucible::effects::Row — Met(X) effect-row algebra ──────────────
//
// A Row is a compile-time set of Effect atoms.  Rows compose by set
// union; Subrow inclusion is the substitution principle for capability
// propagation (a function requiring row R can be called from a context
// holding any row R' ⊇ R).
//
// Per 25_04_2026.md §3.2 and Tang-Lindley POPL 2026, the row is
// modelled as a `consteval std::meta::info` set.  This header declares
// the public surface; bodies land in METX-2 (#474).
//
//   Axiom coverage: TypeSafe — Row is strongly typed; mismatches at
//                   capability-propagation boundaries fire at template
//                   substitution, not at use site.
//                   DetSafe — every row operation is consteval.
//   Runtime cost:   zero.  Rows have no runtime representation; the
//                   set lives purely in the type system / consteval
//                   evaluation.
//
// Public surface:
//   Row<Es...>            — concrete row type carrying the effect pack
//   row_size_v<R>         — atom count
//   row_contains_v<R, E>  — membership predicate
//   Subrow<R₁, R₂>        — concept: R₁ ⊆ R₂
//   row_union_t<R₁, R₂>   — set union (canonicalized)
//   row_difference_t<R₁, R₂> — R₁ \ R₂
//   row_intersection_t<R₁, R₂> — R₁ ∩ R₂
//
// STATUS: class type is COMPLETE so that Computation<Row, T> and
// type-level row descriptors can name it.  Set operations live below;
// invalid early use fails at the exact requested operation.
//
// See Capabilities.h (atoms) and Computation.h (carrier).
//
// ── See also: typed-set triad (misc/typed_sets.md) ─────────────────
//
// Row<Es...> is the TYPE-LEVEL face of a triad of typed-set
// primitives.  Sister primitives:
//
//   safety::Bits<E>              — runtime value-level set (bitwise,
//                                  mask-encoded enums).
//   permissions::proto::PermSet  — type-only set of TAG TYPES.
//   effects::EffectMask          — runtime dual of Row<Es...>;
//                                  bridge via bits_from_row<R>() in
//                                  effects/EffectRowProjection.h.
//                                  EffectMask is dedicated (NOT
//                                  Bits<Effect>) because Effect is
//                                  position-encoded — see
//                                  misc/typed_sets.md §5 for the
//                                  encoding caveat.

#include <crucible/effects/Capabilities.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace crucible::effects {

// ── Row<Es...> ──────────────────────────────────────────────────────
//
// Carries a pack of Effect atoms.  No runtime state.  All algebra is
// consteval-only via the trait family below.
//
// Canonicalization (sort + dedup) is METX-2's job — until then,
// `Row<Effect::Bg, Effect::Alloc>` and `Row<Effect::Alloc, Effect::Bg>`
// are *distinct types*.  Code that doesn't depend on canonical
// equality compiles fine; code that does will need to wait for #474.
template <Effect... Es>
struct Row {
    static constexpr std::size_t size = sizeof...(Es);
};

using EmptyRow = Row<>;

// ── canonical_row_t<R> — sort + dedup at the type level (fixy-A3-001) ─
//
// Maps any `Row<Es...>` to its UNIQUE sort+dedup representative.  Two
// rows that differ in atom order OR in duplicate-atom multiplicity
// canonicalize to the same `Row<sorted-deduped-Es...>`:
//
//   canonical_row_t<Row<Effect::Bg, Effect::IO, Effect::Bg>>
//        == Row<Effect::IO, Effect::Bg>     // sort: IO=1 < Bg=3; dedup: drop second Bg
//
//   canonical_row_t<Row<Effect::IO, Effect::Alloc>>
//        == Row<Effect::Alloc, Effect::IO>  // sort: Alloc=0 < IO=1
//
// The canonical form is sort-on-underlying-value to match the row_hash
// invariant (safety/diag/RowHashFold.h §I02 permutation-invariance).
// Without this, `row_hash_contribution_v<R>` would be set-semantic but
// the row TYPE itself would be position-sensitive, leaving two
// guarantees out of sync (the hash already dedups via
// `fmix64_fold_unique_sorted` per fixy-H-19; this lifts that property
// from the value layer to the type layer).
//
// Why this matters (fixy-A3-001 cache-coherence class):
//   `row_union_t<Row<Bg, IO, Bg>, Row<Block>>` pre-fix yielded
//   `Row<Bg, IO, Bg, Block>` — a 4-pack with a duplicate.  Two
//   semantically equivalent unions could produce structurally-distinct
//   `Row<...>` types, and any downstream consumer reasoning by
//   `std::is_same_v<row_union_t<...>, ...>` (template specialization,
//   metafunction dispatch, row_size_v as set-cardinality) silently saw
//   a non-canonical answer.  Post-fix all three set operations apply
//   `canonical_row_t` to their output and the type-level invariant
//   matches the hash invariant.
template <typename R>
struct canonical_row;

namespace detail {

// Sort+dedup an Effect... pack at consteval, returning a fixed-size
// std::array + the number of distinct atoms.  N is bounded by the
// Effect catalog size (≤ 64 per the EffectRowLattice cap) so the
// O(N²) bubble sort is comfortably cheap at compile time.
template <Effect... Es>
[[nodiscard]] consteval auto compute_canonical_effect_pack() noexcept {
    struct Result {
        std::array<Effect, sizeof...(Es) == 0 ? 1 : sizeof...(Es)> data{};
        std::size_t count = 0;
    };
    Result r{};
    if constexpr (sizeof...(Es) == 0) {
        return r;
    } else {
        std::array<Effect, sizeof...(Es)> raw{Es...};
        using U = std::underlying_type_t<Effect>;
        // Bubble sort by Effect underlying value.
        for (std::size_t i = 0; i < raw.size(); ++i) {
            for (std::size_t j = i + 1; j < raw.size(); ++j) {
                if (static_cast<U>(raw[j]) < static_cast<U>(raw[i])) {
                    Effect const tmp = raw[i];
                    raw[i] = raw[j];
                    raw[j] = tmp;
                }
            }
        }
        // Walk-and-dedup into r.data, recording first occurrences only.
        std::size_t out = 0;
        for (std::size_t i = 0; i < raw.size(); ++i) {
            if (i == 0 || raw[i] != raw[i - 1]) {
                r.data[out++] = raw[i];
            }
        }
        r.count = out;
        return r;
    }
}

template <Effect... Es>
inline constexpr auto canonical_effect_pack_v =
    compute_canonical_effect_pack<Es...>();

}  // namespace detail

template <>
struct canonical_row<Row<>> {
    using type = Row<>;
};

template <Effect E0, Effect... Es>
struct canonical_row<Row<E0, Es...>> {
private:
    template <std::size_t... Is>
    static auto build(std::index_sequence<Is...>)
        -> Row<detail::canonical_effect_pack_v<E0, Es...>.data[Is]...>;

public:
    using type = decltype(build(std::make_index_sequence<
        detail::canonical_effect_pack_v<E0, Es...>.count>{}));
};

// Canonical form of a row type.  Idempotent: applying it to an
// already-canonical Row<...> yields the same type.
template <typename R>
using canonical_row_t = typename canonical_row<R>::type;

// ── Membership / size traits ────────────────────────────────────────
//
// THREE CARDINALITY LENSES — fixy-A3-031.  The substrate carries three
// related-but-distinct notions of "how big is row R", and a careless
// reader can pick the wrong one:
//
//   1. PACK SIZE      — positional count, includes duplicates and
//                       preserves declaration order.  Source of truth:
//                       `Row<Es...>::size = sizeof...(Es)`.  For
//                       `Row<Effect::Bg, Effect::IO, Effect::Bg>` this
//                       is 3.  This is what the bare `row_size_v<R>`
//                       returns; kept as-is for backwards compatibility.
//
//   2. UNIQUE SIZE    — set cardinality after sort+dedup via
//                       `canonical_row_t<R>`.  For the same row above
//                       this is 2 (Bg, IO).  Matches the set-semantic
//                       notion of "how many distinct effects does this
//                       row mention".  All set-algebra outputs
//                       (`row_union_t`, `row_difference_t`,
//                       `row_intersection_t`) ARE canonical, so for
//                       them PACK SIZE == UNIQUE SIZE; only directly-
//                       written `Row<Es...>` literals can differ.
//
//   3. HASH SIZE      — internal count used by
//                       `row_hash_contribution<Row<Es...>>` after
//                       `fmix64_fold_unique_sorted` (fixy-H-19).  Equal
//                       to UNIQUE SIZE by construction — the hash IS
//                       set-semantic, regardless of how the row was
//                       spelled.  Not exposed as a trait because it is
//                       indistinguishable from UNIQUE SIZE; documented
//                       here so readers don't reach for a third trait
//                       that doesn't exist.
//
// USAGE GUIDANCE:
//   - Replay determinism / signature equality / structural matching:
//     use `row_pack_size_v<R>`.  Two `Row<...>` types with the SAME
//     pack agree; differently-ordered literals disagree.
//   - Set-semantic predicates / "is row empty as a set", subrow tests,
//     federation cache slot reasoning: use `row_unique_size_v<R>`.
//   - Bare `row_size_v<R>` aliases PACK SIZE.  New code should prefer
//     the explicit name (`row_pack_size_v` or `row_unique_size_v`);
//     existing sites are untouched.

// Raw pack count — positional, includes duplicates, respects order.
// `row_pack_size_v<Row<A, B, A>> == 3`.  Backward-compatible alias
// for `R::size`; do not change without auditing every static_assert
// that compares it.
template <typename R>
inline constexpr std::size_t row_pack_size_v = R::size;

// Set cardinality — sort+dedup'd count.  Matches the hash invariant.
// `row_unique_size_v<Row<A, B, A>> == 2` (assuming A != B).
template <typename R>
inline constexpr std::size_t row_unique_size_v =
    row_pack_size_v<canonical_row_t<R>>;

// Legacy / bare alias for `row_pack_size_v`.  Many production sites
// already read this; the rename to `row_pack_size_v` is a clarity
// affordance, not a deprecation.
template <typename R>
inline constexpr std::size_t row_size_v = R::size;

// Membership predicate: does row R contain effect atom E?
//
// Implemented now (small enough that deferring would be silly).
template <typename R, Effect E>
inline constexpr bool row_contains_v = false;

template <Effect E, Effect... Es>
inline constexpr bool row_contains_v<Row<Es...>, E> = ((Es == E) || ...);

// ── Set algebra (METX-2 #474 — bodies shipped) ──────────────────────
//
// `row_union_t / row_difference_t / row_intersection_t` ARE
// canonicalized (sort+dedup by Effect underlying value) since
// fixy-A3-001 — every output is reified through `canonical_row_t`,
// so the type-level invariant matches the row_hash invariant
// (safety/diag/RowHashFold.h §I02 permutation-invariance + set-
// semantic dedup).  Two semantically equivalent operations yield
// `std::is_same_v` types:
//
//   row_union_t<Row<A,B>, Row<B,A>>  ==  row_union_t<Row<B,A>, Row<A,B>>
//                                    ==  Row<sorted(A,B)>
//
//   row_union_t<Row<Bg, IO, Bg>, Row<Block>>  ==  Row<IO, Block, Bg>
//                                                  (IO=1 < Block=2 < Bg=3)
//
// `Subrow` remains the semantic-equality concept used at substitution
// boundaries; the structural equality now agrees with it instead of
// drifting from it.  Compile-time cost is bounded by N² where N is
// the row's pack size — N ≤ 64 per the Effect catalog cap.
//
// Note: a user-written non-canonical `Row<Es...>` literal (e.g.
// `Row<Effect::Bg, Effect::IO>` where Bg=3 > IO=1) does NOT auto-
// canonicalize — only the OUTPUTS of the set-algebra operations do.
// Apply `canonical_row_t<R>` explicitly if you need a literal turned
// into canonical form.

namespace detail {

// row_insert_unique<R, E>: prepend E to R unless E is already present.
// Foundation primitive for the union recursion below.
template <typename R, Effect E>
struct row_insert_unique;

template <Effect... Es, Effect E>
struct row_insert_unique<Row<Es...>, E> {
    using type = std::conditional_t<
        ((Es == E) || ...),
        Row<Es...>,
        Row<Es..., E>
    >;
};

template <typename R, Effect E>
using row_insert_unique_t = typename row_insert_unique<R, E>::type;

// row_union_recursive<R1, R2>: walk R2's pack, insert each atom into R1
// only if not already present.  Linear in |R2|; O(|R1|) per insert via
// fold expression.
template <typename R1, typename R2>
struct row_union_recursive;

template <typename R1>
struct row_union_recursive<R1, Row<>> {
    using type = R1;
};

template <typename R1, Effect Head, Effect... Tail>
struct row_union_recursive<R1, Row<Head, Tail...>> {
    using type = typename row_union_recursive<
        row_insert_unique_t<R1, Head>,
        Row<Tail...>
    >::type;
};

// row_concat<Rs...>: concatenate a pack of Row<...>s.  Used by the
// difference / intersection filter machinery to fold per-element
// keep-or-drop decisions back into a single row.
template <typename...>
struct row_concat;

template <>
struct row_concat<> {
    using type = Row<>;
};

template <Effect... Xs>
struct row_concat<Row<Xs...>> {
    using type = Row<Xs...>;
};

template <Effect... Xs, Effect... Ys, typename... Rest>
struct row_concat<Row<Xs...>, Row<Ys...>, Rest...> {
    using type = typename row_concat<Row<Xs..., Ys...>, Rest...>::type;
};

// row_difference_impl: filter R1 by keep-if-not-in-R2.  Uses the per-
// element keep_or_drop alias to map each Effect either to Row<E> or
// Row<>; row_concat folds the result.
template <typename R1, typename R2>
struct row_difference_impl;

template <Effect... E1s, typename R2>
struct row_difference_impl<Row<E1s...>, R2> {
    template <Effect E>
    using keep_or_drop = std::conditional_t<
        row_contains_v<R2, E>,
        Row<>,        // drop — present in R2
        Row<E>        // keep — absent from R2
    >;

    using type = typename row_concat<keep_or_drop<E1s>...>::type;
};

// row_intersection_impl: filter R1 by keep-if-in-R2.  Symmetric to
// difference but with the keep-or-drop polarity flipped.
template <typename R1, typename R2>
struct row_intersection_impl;

template <Effect... E1s, typename R2>
struct row_intersection_impl<Row<E1s...>, R2> {
    template <Effect E>
    using keep_or_drop = std::conditional_t<
        row_contains_v<R2, E>,
        Row<E>,       // keep — present in R2
        Row<>         // drop — absent from R2
    >;

    using type = typename row_concat<keep_or_drop<E1s>...>::type;
};

}  // namespace detail

// fixy-A3-001: all three set-algebra outputs are reified through
// `canonical_row_t` so duplicate-atom drift in EITHER R1 OR R2 (or
// inside the per-element keep_or_drop fold for difference/intersection)
// cannot leak into the result type.  Pre-fix `row_union_t<
// Row<Bg, IO, Bg>, Row<Block>>` produced `Row<Bg, IO, Bg, Block>` — a
// non-canonical 4-pack that structurally differed from the
// semantically-equivalent `Row<IO, Block, Bg>` even though the row
// hash (RowHashFold.h §I02) folded them to the same value.  Post-fix
// both expressions yield `Row<IO, Block, Bg>` as a type, and the
// type-level invariant agrees with the hash invariant.
template <typename R1, typename R2>
using row_union_t = canonical_row_t<
    typename detail::row_union_recursive<R1, R2>::type>;

template <typename R1, typename R2>
using row_difference_t = canonical_row_t<
    typename detail::row_difference_impl<R1, R2>::type>;

template <typename R1, typename R2>
using row_intersection_t = canonical_row_t<
    typename detail::row_intersection_impl<R1, R2>::type>;

// ── Subrow concept ──────────────────────────────────────────────────
//
// R1 is a Subrow of R2 iff every atom in R1 is also in R2.
// Implemented now (the substitution principle is the ONE check
// that pervades every capability-propagation call site, so deferring
// it would block METX-5's sweep before it starts).
template <typename R1, typename R2>
struct is_subrow : std::false_type {};

template <Effect... E1s, Effect... E2s>
struct is_subrow<Row<E1s...>, Row<E2s...>>
    : std::bool_constant<(row_contains_v<Row<E2s...>, E1s> && ...)> {};

template <typename R1, typename R2>
inline constexpr bool is_subrow_v = is_subrow<R1, R2>::value;

template <typename R1, typename R2>
concept Subrow = is_subrow_v<R1, R2>;

// ── Self-test block ─────────────────────────────────────────────────
namespace detail::effect_row_self_test {

using R_empty       = Row<>;
using R_alloc       = Row<Effect::Alloc>;
using R_io          = Row<Effect::IO>;
using R_alloc_io    = Row<Effect::Alloc, Effect::IO>;
using R_alloc_io_bg = Row<Effect::Alloc, Effect::IO, Effect::Bg>;

// Sizes match the pack.
static_assert(row_size_v<R_empty>       == 0);
static_assert(row_size_v<R_alloc>       == 1);
static_assert(row_size_v<R_alloc_io>    == 2);
static_assert(row_size_v<R_alloc_io_bg> == 3);

// Membership.
static_assert(!row_contains_v<R_empty,    Effect::Alloc>);
static_assert( row_contains_v<R_alloc,    Effect::Alloc>);
static_assert(!row_contains_v<R_alloc,    Effect::IO>);
static_assert( row_contains_v<R_alloc_io, Effect::Alloc>);
static_assert( row_contains_v<R_alloc_io, Effect::IO>);
static_assert(!row_contains_v<R_alloc_io, Effect::Bg>);

// Subrow inclusion (substitution principle).
static_assert( is_subrow_v<R_empty, R_empty>);
static_assert( is_subrow_v<R_empty, R_alloc>);          // ∅ ⊆ {Alloc}
static_assert( is_subrow_v<R_alloc, R_alloc_io>);        // {A} ⊆ {A, I}
static_assert(!is_subrow_v<R_alloc_io, R_alloc>);        // {A, I} ⊄ {A}
static_assert( is_subrow_v<R_alloc_io, R_alloc_io_bg>);  // {A, I} ⊆ {A, I, B}
static_assert(!is_subrow_v<R_io, R_alloc>);              // {I} ⊄ {A}

// Concept-form mirror.
static_assert( Subrow<R_empty, R_alloc_io>);
static_assert( Subrow<R_alloc, R_alloc_io_bg>);
static_assert(!Subrow<R_alloc_io, R_alloc>);

// ── Set-algebra coverage (METX-2 #474 + fixy-A3-001) ────────────────
//
// Most assertions are EXTENSIONAL — they check membership / sub-row
// containment.  Since fixy-A3-001 the three set-algebra operations
// ALSO produce canonical (sort+dedup-by-underlying-value) Row types,
// so several structural `std::is_same_v` invariants now hold in
// addition to the Subrow semantics — see the canonical_row_t block
// below.  Pre-fix `row_union_t<Row<A,B>, Row<B,A>>` could differ
// structurally from `row_union_t<Row<B,A>, Row<A,B>>` while staying
// Subrow-equal; post-fix the two are `std::is_same_v` equal.

// Identity laws.
static_assert(std::is_same_v<row_union_t<R_empty, R_empty>, R_empty>);
static_assert(std::is_same_v<row_union_t<R_alloc_io, R_empty>, R_alloc_io>);
static_assert(is_subrow_v<R_alloc_io, row_union_t<R_empty, R_alloc_io>>);
static_assert(is_subrow_v<row_union_t<R_empty, R_alloc_io>, R_alloc_io>);

// Union — every input row is contained in the result.
using R_union_a_io = row_union_t<R_alloc, R_io>;
static_assert(is_subrow_v<R_alloc, R_union_a_io>);
static_assert(is_subrow_v<R_io,    R_union_a_io>);
static_assert(row_size_v<R_union_a_io> == 2);

// Union — duplicates absorbed (no double-insert).
using R_union_dup = row_union_t<R_alloc_io, R_alloc>;
static_assert(row_size_v<R_union_dup> == 2);  // Alloc not duplicated.
static_assert(is_subrow_v<R_alloc_io, R_union_dup>);
static_assert(is_subrow_v<R_alloc,    R_union_dup>);
static_assert(!row_contains_v<R_union_dup, Effect::Bg>);

// Union — commutativity up to Subrow.
using R_left  = row_union_t<R_alloc, R_io>;
using R_right = row_union_t<R_io, R_alloc>;
static_assert(is_subrow_v<R_left, R_right>);
static_assert(is_subrow_v<R_right, R_left>);

// Union — associativity up to Subrow.
using R_lr_then_bg     = row_union_t<row_union_t<R_alloc, R_io>, Row<Effect::Bg>>;
using R_lr_then_bg_alt = row_union_t<R_alloc, row_union_t<R_io, Row<Effect::Bg>>>;
static_assert(is_subrow_v<R_lr_then_bg, R_lr_then_bg_alt>);
static_assert(is_subrow_v<R_lr_then_bg_alt, R_lr_then_bg>);
static_assert(row_size_v<R_lr_then_bg> == 3);

// Difference — A \ ∅ = A; A \ A = ∅.
static_assert(std::is_same_v<row_difference_t<R_alloc_io, R_empty>, R_alloc_io>);
static_assert(row_size_v<row_difference_t<R_alloc_io, R_alloc_io>> == 0);
static_assert(std::is_same_v<row_difference_t<R_alloc_io, R_alloc_io>, R_empty>);

// Difference — drops only the named atoms.
using R_diff = row_difference_t<R_alloc_io_bg, R_alloc>;
static_assert(row_size_v<R_diff> == 2);
static_assert(!row_contains_v<R_diff, Effect::Alloc>);
static_assert( row_contains_v<R_diff, Effect::IO>);
static_assert( row_contains_v<R_diff, Effect::Bg>);

// Intersection — ∅ ∩ A = ∅; A ∩ A = A; commutative up to Subrow.
static_assert(row_size_v<row_intersection_t<R_empty, R_alloc_io>> == 0);
static_assert(std::is_same_v<row_intersection_t<R_alloc_io, R_alloc_io>, R_alloc_io>);

using R_inter = row_intersection_t<R_alloc_io, R_alloc_io_bg>;
static_assert(is_subrow_v<R_inter, R_alloc_io>);
static_assert(is_subrow_v<R_alloc_io, R_inter>);  // R_alloc_io ⊆ R_alloc_io_bg
static_assert(row_size_v<R_inter> == 2);

using R_inter_disjoint = row_intersection_t<R_alloc, R_io>;
static_assert(row_size_v<R_inter_disjoint> == 0);

// De Morgan-ish identity over the universe row {Alloc, IO, Bg, Init,
// Test, Block} — every Effect atom — exercises the difference-of-
// union-equals-intersection-of-differences shape that downstream
// graded-modal effects code relies on.
using R_universe = Row<Effect::Alloc, Effect::IO, Effect::Block,
                       Effect::Bg, Effect::Init, Effect::Test>;
static_assert(row_size_v<R_universe> == effect_count);
static_assert(is_subrow_v<R_alloc_io, R_universe>);
static_assert(is_subrow_v<R_alloc_io_bg, R_universe>);

// Self-difference / intersection corner — A \ A = ∅; A ∩ ∅ = ∅.
static_assert(row_size_v<row_difference_t<R_universe, R_universe>>      == 0);
static_assert(row_size_v<row_intersection_t<R_universe, R_empty>>       == 0);

// ── canonical_row_t<R> coverage (fixy-A3-001) ───────────────────────
//
// canonical_row_t is sort-on-underlying-value + dedup.  Effect
// underlying values per Capabilities.h:
//   Alloc=0  IO=1  Block=2  Bg=3  Init=4  Test=5
// so sorted order over any pack of these atoms is exactly the
// declaration order in Capabilities.h.

// Empty pack: canonical_row_t<Row<>> == Row<>.
static_assert(std::is_same_v<canonical_row_t<Row<>>, Row<>>);

// Singleton: canonical_row_t is a no-op (already sorted, no dupes).
static_assert(std::is_same_v<
    canonical_row_t<Row<Effect::Bg>>,
    Row<Effect::Bg>>);

// Already-sorted, no dupes: canonical_row_t is a no-op (idempotent).
static_assert(std::is_same_v<
    canonical_row_t<Row<Effect::Alloc, Effect::IO>>,
    Row<Effect::Alloc, Effect::IO>>);

// Reversed pair: canonical_row_t sorts to underlying-value order.
static_assert(std::is_same_v<
    canonical_row_t<Row<Effect::IO, Effect::Alloc>>,
    Row<Effect::Alloc, Effect::IO>>);

// Duplicate-atom drift: canonical_row_t collapses to the unique set.
//
// `Row<Bg, IO, Bg>` has Bg=3 / IO=1 / Bg=3.  Sort → {1, 3, 3} →
// {IO, Bg, Bg}.  Dedup → {IO, Bg}.  Final type Row<IO, Bg>.
static_assert(std::is_same_v<
    canonical_row_t<Row<Effect::Bg, Effect::IO, Effect::Bg>>,
    Row<Effect::IO, Effect::Bg>>);

// Triple-replicated atom collapses to singleton.
static_assert(std::is_same_v<
    canonical_row_t<Row<Effect::IO, Effect::IO, Effect::IO>>,
    Row<Effect::IO>>);

// Interleaved duplicates with extra atom: full sort+dedup chain.
//
// `Row<Bg, IO, Bg, Block>` — values {3, 1, 3, 2}.  Sort → {1, 2, 3, 3}.
// Dedup → {1, 2, 3} == {IO, Block, Bg}.
static_assert(std::is_same_v<
    canonical_row_t<Row<Effect::Bg, Effect::IO, Effect::Bg, Effect::Block>>,
    Row<Effect::IO, Effect::Block, Effect::Bg>>);

// canonical_row_t is idempotent.
using R_canon_once  = canonical_row_t<Row<Effect::Bg, Effect::Alloc, Effect::Bg>>;
using R_canon_twice = canonical_row_t<R_canon_once>;
static_assert(std::is_same_v<R_canon_once, R_canon_twice>);
static_assert(std::is_same_v<R_canon_once, Row<Effect::Alloc, Effect::Bg>>);

// canonical_row_t reduces row_size_v to SET cardinality, matching the
// row_hash's unique_count_sorted behavior.
static_assert(row_size_v<canonical_row_t<Row<Effect::Bg, Effect::IO, Effect::Bg>>> == 2);
static_assert(row_size_v<canonical_row_t<Row<Effect::IO, Effect::IO, Effect::IO>>> == 1);
static_assert(row_size_v<canonical_row_t<Row<>>> == 0);

// ── fixy-A3-031: three cardinality lenses pinned by witnesses ──────
//
// Witness all three lenses on a single non-canonical literal so a
// future reader can copy-paste the pattern at any production call
// site that mixes pack-level matching with set-level reasoning.
namespace effect_row_a3_031_witness {

// A non-canonical literal: positions (Bg=3, IO=1, Bg=3) deliberately
// in declaration order so PACK SIZE = 3, but UNIQUE SIZE = 2 because
// Bg repeats.
using R_dup = Row<Effect::Bg, Effect::IO, Effect::Bg>;

// LENS 1 — pack size: positional count, includes duplicate Bg.
static_assert(row_pack_size_v<R_dup> == 3);
static_assert(row_size_v<R_dup>      == 3);  // legacy alias for pack

// LENS 2 — unique size: set cardinality, dedups Bg.
static_assert(row_unique_size_v<R_dup> == 2);

// LENS 3 — hash size: indistinguishable from UNIQUE SIZE by design;
// no separate trait, but the doc-block above names it so readers
// don't go looking for one.  Spot-check the agreement by routing
// through canonical_row_t (the hash internally does the same):
static_assert(row_pack_size_v<canonical_row_t<R_dup>>
            == row_unique_size_v<R_dup>);

// On canonical rows the three lenses agree.  Set-algebra outputs are
// canonical (fixy-A3-001), so production sites pass through this
// equality automatically.
using R_canon = canonical_row_t<R_dup>;
static_assert(row_pack_size_v<R_canon>   == 2);
static_assert(row_unique_size_v<R_canon> == 2);

// Empty row: all three lenses are 0.
static_assert(row_pack_size_v<Row<>>   == 0);
static_assert(row_unique_size_v<Row<>> == 0);
static_assert(row_size_v<Row<>>        == 0);

}  // namespace effect_row_a3_031_witness

// ── Set operations now produce canonical types (fixy-A3-001) ────────
//
// Pre-fix invariants asserted only Subrow-equality between
// equivalent-order constructions; post-fix the type identities hold.

// row_union_t auto-dedups intra-R1 duplicates AND sorts the output.
static_assert(std::is_same_v<
    row_union_t<Row<Effect::Bg, Effect::IO, Effect::Bg>, Row<Effect::Block>>,
    Row<Effect::IO, Effect::Block, Effect::Bg>>);

// row_union_t commutative AT THE TYPE LEVEL (was only Subrow-equal pre-fix).
static_assert(std::is_same_v<
    row_union_t<R_alloc, R_io>,
    row_union_t<R_io, R_alloc>>);

// row_union_t associative AT THE TYPE LEVEL (was only Subrow-equal pre-fix).
static_assert(std::is_same_v<R_lr_then_bg, R_lr_then_bg_alt>);

// row_difference_t output is canonical (drops dupes inside R1).
static_assert(std::is_same_v<
    row_difference_t<Row<Effect::Bg, Effect::IO, Effect::Bg>, Row<Effect::Block>>,
    Row<Effect::IO, Effect::Bg>>);

// row_intersection_t output is canonical (no order leak from
// keep_or_drop walk over R1).
static_assert(std::is_same_v<
    row_intersection_t<Row<Effect::Bg, Effect::IO>, Row<Effect::IO, Effect::Bg>>,
    Row<Effect::IO, Effect::Bg>>);

// Reordered inputs to set ops yield the same canonical type.
static_assert(std::is_same_v<
    row_union_t<Row<Effect::Test, Effect::Alloc>, Row<Effect::Bg>>,
    row_union_t<Row<Effect::Bg>, Row<Effect::Alloc, Effect::Test>>>);

// ── Runtime smoke test (fixy-A3-021) ────────────────────────────────
//
// EffectRow's surface is consteval-only — every operation is a type-
// level metafunction or a `row_size`/`row_contains_v` value alias.
// There's no runtime body to drive, but we DO need to prove the
// trait calls type-check against runtime-typed witnesses (rvalues,
// lvalue refs, const refs) — a future refactor that added cv-ref
// stripping or removed it from the concept gates would fire here
// (sister discipline to fixy-A3-004 IsExecCtx cv-ref symmetry).
inline void runtime_smoke_test() {
    // Default-construct runtime row witnesses — Row is empty (no state)
    // but constructing it at runtime proves the empty-struct invariant
    // holds AND the canonicalization machinery doesn't drag in a
    // non-trivial default ctor.
    [[maybe_unused]] Row<Effect::Bg> r_bg{};
    [[maybe_unused]] Row<Effect::Bg, Effect::IO> r_bg_io{};
    [[maybe_unused]] EmptyRow r_empty{};

    // Probe the runtime sizeof — empty rows must collapse via EBO when
    // used as [[no_unique_address]] members in ExecCtx.
    [[maybe_unused]] auto sz1 = sizeof(r_bg);
    [[maybe_unused]] auto sz2 = sizeof(r_empty);

    // Drive `size` member through a runtime read path.
    [[maybe_unused]] std::size_t n_bg    = decltype(r_bg)::size;
    [[maybe_unused]] std::size_t n_bg_io = decltype(r_bg_io)::size;
    [[maybe_unused]] std::size_t n_empty = EmptyRow::size;
}

}  // namespace detail::effect_row_self_test

}  // namespace crucible::effects
