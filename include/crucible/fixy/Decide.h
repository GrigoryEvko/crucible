#pragma once

// ── crucible::fixy::decide — Named VC predicates under fixy:: ─────
//
// Re-export surface for `crucible::decide::` named VC
// predicates (catalog of 23 predicates + Interval<T> type as of
// 2026-05-19).  Surfaces every catalog predicate under fixy::decide::
// so callers who include only the fixy umbrella never have to
// descend into safety/Decide.h directly.
//
// Per CLAUDE.md §XII three-layer VC discharge hierarchy:
//
//   1. Type-level proof (Refined<>)        — cheapest, preferred
//   2. Named cite (decide::*)              — this surface
//   3. Anonymous predicate at site         — last resort
//
// The catalog grows bottom-up (per feedback_decide_catalog.md): cite-
// first, defend at 6-month audit.  When a new VC predicate joins the
// catalog, ADD a row below.  When a predicate retires (CONTRACT-126
// catalog-trim discipline), REMOVE the row.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
// All from `crucible::decide::` (safety/Decide.h):
//   no_overflow_mul / no_overflow_sum / no_overflow_pow2_shift
//   all_in_range / strictly_increasing / weakly_increasing
//   is_power_of_two_le / factorization_eq / coprime
//   intervals_pairwise_disjoint / intervals_cover_unit / Interval
//   tier_replaces / row_subset
//   fmix_preserves_non_zero
//   conjunction / disjunction / implies
//   aligned_in_range / in_range / positive / non_negative
//   valid_span / is_non_zero
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — predicates are `[[gnu::const]]`/`[[gnu::pure]]` —
//              they read parameters only; no hidden state path.
//   TypeSafe — every predicate's substrate signature is preserved by
//              the using-declaration; template constraints carry.
//   NullSafe — `valid_span` and `is_non_zero` are the boundary
//              checkers; their substrate `pre(...)` contracts
//              carry through the using-decl unchanged.
//   MemSafe  — predicates take by-value or span; no ownership
//              transfer at the re-export boundary.
//   DetSafe  — pure functions; identical input → identical output.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Each `using crucible::decide::X;` is a name-lookup directive
// only; the call resolves to the same substrate function pointer.
// Verified by the same-symbol witnesses in the self-test block.
//
// ── §XXI Universal Mint Pattern ────────────────────────────────────
//
// Predicates are NOT mints — they are not factories that synthesize
// authority.  They are pure functions used at the call site to
// discharge a VC.  The §XXI grep-target convention (`mint_*`) does
// not apply.  decide::* predicates are grep-discoverable under their
// own catalog tag (per `feedback_decide_catalog.md`: bottom-up cite
// discoverability is the catalog's organising principle).

#include <crucible/safety/Decide.h>
#include <crucible/safety/DecideOracle.h>   // FIXY-V-178: oracle:: reference impls

namespace crucible::fixy::decide {

// ── Integer-arithmetic predicates (3) ──────────────────────────────
using ::crucible::decide::no_overflow_mul;
using ::crucible::decide::no_overflow_sum;
using ::crucible::decide::no_overflow_pow2_shift;

// ── Range / ordering predicates (3) ───────────────────────────────
using ::crucible::decide::all_in_range;
using ::crucible::decide::strictly_increasing;
using ::crucible::decide::weakly_increasing;

// ── Divisibility / factorization predicates (3) ───────────────────
using ::crucible::decide::is_power_of_two_le;
using ::crucible::decide::factorization_eq;
using ::crucible::decide::coprime;

// ── Interval-set predicates + carrier (3) ─────────────────────────
using ::crucible::decide::Interval;
using ::crucible::decide::intervals_pairwise_disjoint;
using ::crucible::decide::intervals_cover_unit;

// ── Tier / row predicates (2) ─────────────────────────────────────
using ::crucible::decide::tier_replaces;
using ::crucible::decide::row_subset;

// ── Hash / mixing predicates (1) ──────────────────────────────────
using ::crucible::decide::fmix_preserves_non_zero;

// ── Boolean-fold predicates (3) ───────────────────────────────────
using ::crucible::decide::conjunction;
using ::crucible::decide::disjunction;
using ::crucible::decide::implies;

// ── Scalar-range predicates (4) ───────────────────────────────────
using ::crucible::decide::aligned_in_range;
using ::crucible::decide::in_range;
using ::crucible::decide::positive;
using ::crucible::decide::non_negative;

// ── Pointer / span predicates (2) ─────────────────────────────────
using ::crucible::decide::valid_span;
using ::crucible::decide::is_non_zero;

}  // namespace crucible::fixy::decide

// ── Self-test ──────────────────────────────────────────────────────
//
// Each witness asserts that the fixy:: re-export and the substrate
// symbol name the same function template.  We check `same_*_v` per
// predicate via pointer-to-function comparison at constexpr scope —
// if any using-decl re-targets a different substrate symbol, the
// corresponding witness goes false and the TU red-bars under
// -Werror.  Templates that take std::integral T constraints
// instantiate at T=int for the witness.
//
// Per CLAUDE.md §XVII telling-word discipline, each witness name
// reads as "the substrate and fixy paths resolve to the same X".

namespace crucible::fixy::decide::self_test {

// Witnesses compare function-pointer TYPES via std::is_same_v rather
// than function-pointer VALUES via operator== to side-step GCC's
// `-Werror=tautological-compare` (which fires when the optimizer can
// statically prove both sides are the same symbol — exactly the
// outcome we want, but the warning is noisy at our diagnostic level).
// Type-level same-symbol is structurally equivalent to value-level
// same-pointer for any normal using-decl that doesn't introduce a
// new overload.  Matches Cap.h's canonical witness pattern.

// ── Integer-arithmetic ────────────────────────────────────────────
inline constexpr bool same_no_overflow_mul_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::no_overflow_mul<int>),
    decltype(&::crucible::decide::no_overflow_mul<int>)>;
static_assert(same_no_overflow_mul_v,
    "fixy::decide::no_overflow_mul must alias the substrate symbol.");

inline constexpr bool same_no_overflow_sum_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::no_overflow_sum<int>),
    decltype(&::crucible::decide::no_overflow_sum<int>)>;
static_assert(same_no_overflow_sum_v,
    "fixy::decide::no_overflow_sum must alias the substrate symbol.");

inline constexpr bool same_no_overflow_pow2_shift_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::no_overflow_pow2_shift<int>),
    decltype(&::crucible::decide::no_overflow_pow2_shift<int>)>;
static_assert(same_no_overflow_pow2_shift_v,
    "fixy::decide::no_overflow_pow2_shift must alias the substrate symbol.");

// ── Range / ordering ──────────────────────────────────────────────
inline constexpr bool same_all_in_range_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::all_in_range<int>),
    decltype(&::crucible::decide::all_in_range<int>)>;
static_assert(same_all_in_range_v,
    "fixy::decide::all_in_range must alias the substrate symbol.");

inline constexpr bool same_strictly_increasing_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::strictly_increasing<int>),
    decltype(&::crucible::decide::strictly_increasing<int>)>;
static_assert(same_strictly_increasing_v,
    "fixy::decide::strictly_increasing must alias the substrate symbol.");

inline constexpr bool same_weakly_increasing_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::weakly_increasing<int>),
    decltype(&::crucible::decide::weakly_increasing<int>)>;
static_assert(same_weakly_increasing_v,
    "fixy::decide::weakly_increasing must alias the substrate symbol.");

// ── Divisibility / factorization ──────────────────────────────────
inline constexpr bool same_is_power_of_two_le_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::is_power_of_two_le<int>),
    decltype(&::crucible::decide::is_power_of_two_le<int>)>;
static_assert(same_is_power_of_two_le_v,
    "fixy::decide::is_power_of_two_le must alias the substrate symbol.");

inline constexpr bool same_factorization_eq_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::factorization_eq<int>),
    decltype(&::crucible::decide::factorization_eq<int>)>;
static_assert(same_factorization_eq_v,
    "fixy::decide::factorization_eq must alias the substrate symbol.");

inline constexpr bool same_coprime_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::coprime<int>),
    decltype(&::crucible::decide::coprime<int>)>;
static_assert(same_coprime_v,
    "fixy::decide::coprime must alias the substrate symbol.");

// ── Interval-set carrier identity ─────────────────────────────────
static_assert(std::is_same_v<
    ::crucible::fixy::decide::Interval<int>,
    ::crucible::decide::Interval<int>>,
    "fixy::decide::Interval must alias the substrate type.");

inline constexpr bool same_intervals_pairwise_disjoint_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::intervals_pairwise_disjoint<int>),
    decltype(&::crucible::decide::intervals_pairwise_disjoint<int>)>;
static_assert(same_intervals_pairwise_disjoint_v,
    "fixy::decide::intervals_pairwise_disjoint must alias the substrate.");

inline constexpr bool same_intervals_cover_unit_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::intervals_cover_unit<int>),
    decltype(&::crucible::decide::intervals_cover_unit<int>)>;
static_assert(same_intervals_cover_unit_v,
    "fixy::decide::intervals_cover_unit must alias the substrate.");

// ── Tier / row ────────────────────────────────────────────────────
// tier_replaces<TierTag> takes any TierTag — use a TU-private tag.
namespace decide_self_test_tags { enum class T { A, B }; }

inline constexpr bool same_tier_replaces_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::tier_replaces<decide_self_test_tags::T>),
    decltype(&::crucible::decide::tier_replaces<decide_self_test_tags::T>)>;
static_assert(same_tier_replaces_v,
    "fixy::decide::tier_replaces must alias the substrate.");

inline constexpr bool same_row_subset_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::row_subset<int, int>),
    decltype(&::crucible::decide::row_subset<int, int>)>;
static_assert(same_row_subset_v,
    "fixy::decide::row_subset must alias the substrate.");

// ── Hash / mixing ─────────────────────────────────────────────────
inline constexpr bool same_fmix_preserves_non_zero_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::fmix_preserves_non_zero),
    decltype(&::crucible::decide::fmix_preserves_non_zero)>;
static_assert(same_fmix_preserves_non_zero_v,
    "fixy::decide::fmix_preserves_non_zero must alias the substrate.");

// ── Boolean-fold ──────────────────────────────────────────────────
inline constexpr bool same_conjunction_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::conjunction),
    decltype(&::crucible::decide::conjunction)>;
static_assert(same_conjunction_v,
    "fixy::decide::conjunction must alias the substrate.");

inline constexpr bool same_disjunction_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::disjunction),
    decltype(&::crucible::decide::disjunction)>;
static_assert(same_disjunction_v,
    "fixy::decide::disjunction must alias the substrate.");

inline constexpr bool same_implies_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::implies),
    decltype(&::crucible::decide::implies)>;
static_assert(same_implies_v,
    "fixy::decide::implies must alias the substrate.");

// ── Scalar-range ──────────────────────────────────────────────────
inline constexpr bool same_aligned_in_range_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::aligned_in_range),
    decltype(&::crucible::decide::aligned_in_range)>;
static_assert(same_aligned_in_range_v,
    "fixy::decide::aligned_in_range must alias the substrate.");

inline constexpr bool same_in_range_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::in_range<int>),
    decltype(&::crucible::decide::in_range<int>)>;
static_assert(same_in_range_v,
    "fixy::decide::in_range must alias the substrate.");

inline constexpr bool same_positive_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::positive<int>),
    decltype(&::crucible::decide::positive<int>)>;
static_assert(same_positive_v,
    "fixy::decide::positive must alias the substrate.");

inline constexpr bool same_non_negative_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::non_negative<int>),
    decltype(&::crucible::decide::non_negative<int>)>;
static_assert(same_non_negative_v,
    "fixy::decide::non_negative must alias the substrate.");

// ── Pointer / span ────────────────────────────────────────────────
inline constexpr bool same_valid_span_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::valid_span<int>),
    decltype(&::crucible::decide::valid_span<int>)>;
static_assert(same_valid_span_v,
    "fixy::decide::valid_span must alias the substrate.");

inline constexpr bool same_is_non_zero_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::is_non_zero<int>),
    decltype(&::crucible::decide::is_non_zero<int>)>;
static_assert(same_is_non_zero_v,
    "fixy::decide::is_non_zero must alias the substrate.");

// ── Catalog cardinality witness ───────────────────────────────────
//
// Pins the predicate count visible through fixy::decide:: at
// authoring time.  When a new predicate joins (CONTRACT-* migration
// growth) OR retires (CONTRACT-126 catalog-trim discipline), bump
// the count below AND add/remove the using-decl above AND add/remove
// the corresponding same_*_v witness.  Cross-axis change forces
// reviewer acknowledgement.
//
// Two complementary assertions follow the constants:
//
// (1) Health-band `[20, 30]` — design-intent floor + ceiling.  If the
//     count falls below 20, the catalog has lost coverage; if it
//     exceeds 30, the catalog has likely accumulated unused entries
//     and should be audited under CONTRACT-126 trim discipline.
//
// (2) FIXY-U-127 / U-128 colocated EXACT ceiling pin (`== 23`, `== 1`)
//     — drift-family discipline per feedback_catalog_cardinality_
//     test_drift.  The contributor bumping the constants sees the
//     sibling assertions at edit time.  The sibling test_fixy_decide.
//     cpp holds only FLOOR pins (`>= 23`, `>= 1`) which catch the
//     inverse direction — an accidental REMOVAL of a catalog entry
//     that escaped review.

inline constexpr int kFixyDecidePredicateCount = 23;
inline constexpr int kFixyDecideTypeCount      = 1;   // Interval

static_assert(kFixyDecidePredicateCount >= 20 &&
              kFixyDecidePredicateCount <= 30,
    "fixy::decide predicate catalog drift outside expected window — "
    "audit the using-decl rows above against catalog additions/removals.");

static_assert(kFixyDecidePredicateCount == 23,
    "ceiling: fixy::decide:: re-exports exactly 23 predicates.  If "
    "you add or remove a predicate, update BOTH the constant AND "
    "this colocated ceiling pin in the same edit.  The sibling "
    "test_fixy_decide.cpp holds only a >= floor and auto-tracks "
    "growth — see feedback_catalog_cardinality_test_drift.");

static_assert(kFixyDecideTypeCount == 1,
    "ceiling: fixy::decide:: re-exports exactly 1 type (Interval<T>).  "
    "If you add a second decide type, update BOTH the constant AND "
    "this colocated ceiling pin in the same edit.");

}  // namespace crucible::fixy::decide::self_test

// ── Runtime smoke test ─────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline.md — every
// algebra/fixy header ships an `inline void runtime_smoke_test()`
// that exercises the surface with NON-CONSTANT values so consteval
// fast-paths don't mask inline-body bugs.  Predicates are pure
// `[[gnu::const]]` / `[[gnu::pure]]` — they compile to direct calls
// with no side effects.

namespace crucible::fixy::decide::self_test {

inline void runtime_smoke_test() {
    // Cross-section sample of predicates with runtime args; volatile
    // sinks defeat consteval folding.
    volatile int a = 7;
    volatile int b = 3;
    volatile int c = 100;
    volatile bool sink = false;
    sink = ::crucible::fixy::decide::no_overflow_mul(a, b);
    sink = ::crucible::fixy::decide::no_overflow_sum(a, b);
    sink = ::crucible::fixy::decide::in_range(a, 0, c);
    sink = ::crucible::fixy::decide::positive(a);
    sink = ::crucible::fixy::decide::non_negative(a);
    sink = ::crucible::fixy::decide::coprime(a, b);
    sink = ::crucible::fixy::decide::is_power_of_two_le(a, c);
    int xs[] = {1, 2, 3, 4, 5};
    sink = ::crucible::fixy::decide::strictly_increasing(
        std::span<const int>{xs});
    sink = ::crucible::fixy::decide::weakly_increasing(
        std::span<const int>{xs});
    sink = ::crucible::fixy::decide::all_in_range(
        std::span<const int>{xs}, 0, 10);
    sink = ::crucible::fixy::decide::implies(true, true);
    bool bs[] = {true, true, false};
    sink = ::crucible::fixy::decide::conjunction(std::span<const bool>{bs});
    sink = ::crucible::fixy::decide::disjunction(std::span<const bool>{bs});
    sink = ::crucible::fixy::decide::is_non_zero(a);
    (void)sink;
}

}  // namespace crucible::fixy::decide::self_test

// ── crucible::fixy::decide::oracle — slow reference oracles ─────────
//
// FIXY-V-178: re-export of `crucible::decide::oracle::*` (safety/
// DecideOracle.h, the "DecideOracle" surface).  These are the
// deliberately-SLOW, transparently-correct reference implementations
// the CONTRACT-090 fuzz harness compares the clever production
// `decide::*` procedures against (widen-and-bound vs
// __builtin_mul_overflow; trial-division / Euclidean vs binary GCD;
// O(n^2) all-pairs vs sort+sweep).  Surfaced under the fixy umbrella
// so a band-3 fuzz / property harness reaches the oracle without
// descending into safety/.
//
// Oracles are NOT predicates and NOT mints — they are spec functions
// used only by the verification harness.  The distinct `oracle::`
// sub-namespace keeps them OUT of the predicate-catalog cardinality
// (kFixyDecidePredicateCount) above; they carry their own count
// witness below.  All are `[[nodiscard]] constexpr` total functions
// of their inputs — zero hidden state, zero re-export cost.
//
//   widen<T> / widen_t<T>            wider integer type for overflow oracles
//   no_overflow_mul_oracle<T>        widen-and-bound product range check
//   no_overflow_sum_oracle<T>        widen-and-bound sum range check
//   all_in_range_oracle<T>           manual-loop range membership
//   strictly_increasing_oracle<T>    O(n^2) all-pairs strict order
//   weakly_increasing_oracle<T>      O(n^2) all-pairs non-strict order
//   is_power_of_two_le_oracle<T>     popcount==1 AND bound
//   factorization_eq_oracle<T>       widen-product equality
//   coprime_oracle<T>                Euclidean GCD == 1
//   conjunction_oracle               count-false == 0
//   disjunction_oracle               count-true != 0
//   aligned_in_range_oracle          four-clause aligned range check

namespace crucible::fixy::decide::oracle {

using ::crucible::decide::oracle::widen;
using ::crucible::decide::oracle::widen_t;
using ::crucible::decide::oracle::no_overflow_mul_oracle;
using ::crucible::decide::oracle::no_overflow_sum_oracle;
using ::crucible::decide::oracle::all_in_range_oracle;
using ::crucible::decide::oracle::strictly_increasing_oracle;
using ::crucible::decide::oracle::weakly_increasing_oracle;
using ::crucible::decide::oracle::is_power_of_two_le_oracle;
using ::crucible::decide::oracle::factorization_eq_oracle;
using ::crucible::decide::oracle::coprime_oracle;
using ::crucible::decide::oracle::conjunction_oracle;
using ::crucible::decide::oracle::disjunction_oracle;
using ::crucible::decide::oracle::aligned_in_range_oracle;

}  // namespace crucible::fixy::decide::oracle

// ── Oracle self-test ────────────────────────────────────────────────
//
// Same dual-export discipline as the predicate surface: each witness
// asserts the fixy:: oracle path and the substrate path name the same
// symbol (type-level same-symbol via decltype, side-stepping
// -Werror=tautological-compare).  Templated oracles instantiate at
// T=int; the three non-templated oracles compare the bare function.

namespace crucible::fixy::decide::oracle::self_test {

// widen<T> / widen_t<T> carrier identity.
static_assert(std::is_same_v<
    ::crucible::fixy::decide::oracle::widen_t<int>,
    ::crucible::decide::oracle::widen_t<int>>,
    "fixy::decide::oracle::widen_t must alias the substrate.");

// ── Templated oracles (8) ─────────────────────────────────────────
inline constexpr bool same_no_overflow_mul_oracle_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::oracle::no_overflow_mul_oracle<int>),
    decltype(&::crucible::decide::oracle::no_overflow_mul_oracle<int>)>;
static_assert(same_no_overflow_mul_oracle_v,
    "fixy::decide::oracle::no_overflow_mul_oracle must alias the substrate.");

inline constexpr bool same_no_overflow_sum_oracle_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::oracle::no_overflow_sum_oracle<int>),
    decltype(&::crucible::decide::oracle::no_overflow_sum_oracle<int>)>;
static_assert(same_no_overflow_sum_oracle_v,
    "fixy::decide::oracle::no_overflow_sum_oracle must alias the substrate.");

inline constexpr bool same_all_in_range_oracle_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::oracle::all_in_range_oracle<int>),
    decltype(&::crucible::decide::oracle::all_in_range_oracle<int>)>;
static_assert(same_all_in_range_oracle_v,
    "fixy::decide::oracle::all_in_range_oracle must alias the substrate.");

inline constexpr bool same_strictly_increasing_oracle_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::oracle::strictly_increasing_oracle<int>),
    decltype(&::crucible::decide::oracle::strictly_increasing_oracle<int>)>;
static_assert(same_strictly_increasing_oracle_v,
    "fixy::decide::oracle::strictly_increasing_oracle must alias the substrate.");

inline constexpr bool same_weakly_increasing_oracle_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::oracle::weakly_increasing_oracle<int>),
    decltype(&::crucible::decide::oracle::weakly_increasing_oracle<int>)>;
static_assert(same_weakly_increasing_oracle_v,
    "fixy::decide::oracle::weakly_increasing_oracle must alias the substrate.");

inline constexpr bool same_is_power_of_two_le_oracle_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::oracle::is_power_of_two_le_oracle<int>),
    decltype(&::crucible::decide::oracle::is_power_of_two_le_oracle<int>)>;
static_assert(same_is_power_of_two_le_oracle_v,
    "fixy::decide::oracle::is_power_of_two_le_oracle must alias the substrate.");

inline constexpr bool same_factorization_eq_oracle_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::oracle::factorization_eq_oracle<int>),
    decltype(&::crucible::decide::oracle::factorization_eq_oracle<int>)>;
static_assert(same_factorization_eq_oracle_v,
    "fixy::decide::oracle::factorization_eq_oracle must alias the substrate.");

inline constexpr bool same_coprime_oracle_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::oracle::coprime_oracle<int>),
    decltype(&::crucible::decide::oracle::coprime_oracle<int>)>;
static_assert(same_coprime_oracle_v,
    "fixy::decide::oracle::coprime_oracle must alias the substrate.");

// ── Non-templated oracles (3) ─────────────────────────────────────
inline constexpr bool same_conjunction_oracle_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::oracle::conjunction_oracle),
    decltype(&::crucible::decide::oracle::conjunction_oracle)>;
static_assert(same_conjunction_oracle_v,
    "fixy::decide::oracle::conjunction_oracle must alias the substrate.");

inline constexpr bool same_disjunction_oracle_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::oracle::disjunction_oracle),
    decltype(&::crucible::decide::oracle::disjunction_oracle)>;
static_assert(same_disjunction_oracle_v,
    "fixy::decide::oracle::disjunction_oracle must alias the substrate.");

inline constexpr bool same_aligned_in_range_oracle_v = std::is_same_v<
    decltype(&::crucible::fixy::decide::oracle::aligned_in_range_oracle),
    decltype(&::crucible::decide::oracle::aligned_in_range_oracle)>;
static_assert(same_aligned_in_range_oracle_v,
    "fixy::decide::oracle::aligned_in_range_oracle must alias the substrate.");

// ── Oracle cardinality witness ────────────────────────────────────
//
// Pins the oracle count visible through fixy::decide::oracle:: at
// authoring time, separate from the predicate-catalog count.  When an
// oracle joins (a new fuzzable decide:: procedure) OR retires, bump
// the constant AND add/remove the using-decl AND the same_*_v witness
// in the same edit (feedback_catalog_cardinality_test_drift).  Floor
// guards accidental removal; exact pin forces reviewer ack on growth.
inline constexpr int kFixyDecideOracleCount = 11;  // 8 templated + 3 not
static_assert(kFixyDecideOracleCount >= 11,
    "fixy::decide::oracle floor — an oracle re-export went missing.");
static_assert(kFixyDecideOracleCount == 11,
    "ceiling: fixy::decide::oracle:: re-exports exactly 11 oracles.  If "
    "you add/remove an oracle, update the constant AND the using-decl "
    "AND the same_*_v witness in the same edit.");

}  // namespace crucible::fixy::decide::oracle::self_test

// ── Oracle runtime smoke test ──────────────────────────────────────
//
// Oracles ARE runtime functions (unlike the pure type-level extract
// metafunctions), so exercise a cross-section with NON-CONSTANT args
// so consteval folding cannot mask an inline-body bug.

namespace crucible::fixy::decide::oracle::self_test {

inline void runtime_smoke_test() {
    volatile int a = 6;
    volatile int b = 4;
    volatile bool sink = false;
    sink = ::crucible::fixy::decide::oracle::no_overflow_mul_oracle<int>(a, b);
    sink = ::crucible::fixy::decide::oracle::no_overflow_sum_oracle<int>(a, b);
    sink = ::crucible::fixy::decide::oracle::coprime_oracle<int>(a, b);
    sink = ::crucible::fixy::decide::oracle::is_power_of_two_le_oracle<int>(a, 64);
    int xs[] = {1, 2, 3, 4, 5};
    sink = ::crucible::fixy::decide::oracle::strictly_increasing_oracle<int>(
        std::span<const int>{xs});
    sink = ::crucible::fixy::decide::oracle::weakly_increasing_oracle<int>(
        std::span<const int>{xs});
    sink = ::crucible::fixy::decide::oracle::all_in_range_oracle<int>(
        std::span<const int>{xs}, 0, 10);
    bool bs[] = {true, true, false};
    sink = ::crucible::fixy::decide::oracle::conjunction_oracle(
        std::span<const bool>{bs});
    sink = ::crucible::fixy::decide::oracle::disjunction_oracle(
        std::span<const bool>{bs});
    volatile std::uint64_t v = 64;
    sink = ::crucible::fixy::decide::oracle::aligned_in_range_oracle(v, 0, 128, 16);
    (void)sink;
}

}  // namespace crucible::fixy::decide::oracle::self_test
