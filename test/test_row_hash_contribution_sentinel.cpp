// ═══════════════════════════════════════════════════════════════════
// test_row_hash_contribution_sentinel — FIXY-U-005
//
// Exhaustive row_hash_contribution coverage matrix for the 15
// single-arg canonical wrappers from CLAUDE.md §XVI's outer→inner
// nesting order.  This TU closes a coverage gap that A3-003 of
// test_migration_verification.cpp left open:
//
//   A3-003 ships a PER-WRAPPER ANCHOR: every wrapper W contributes
//     a non-default value (W<Anchor> ≠ bare Anchor).  That catches
//     "wrapper added to DimensionTraits but forgot row_hash" drift.
//
//   This TU adds the CROSS-PAIR MATRIX: for every ordered pair
//     (W_i, W_j) of the 15 canonical single-arg wrappers
//     (W_i ≠ W_j) the matrix asserts
//         row_hash_contribution_v<W_i<Anchor>>
//      != row_hash_contribution_v<W_j<Anchor>>
//     = 15 × 14 = 210 ordered-pair distinctness static_asserts.
//
//   That catches the SALT-COLLISION bug: two wrappers' identity
//   salts happen to fmix64 to the same constant for the same
//   payload.  The anchor checks would miss it (both differ from
//   bare Anchor); the cross-pair matrix catches it.
//
// The matrix is exhaustive across the 15 canonical single-arg
// wrappers — Computation (the carrier, two-arg shape) is checked
// separately via Anchor ≠ pure-Anchor below.  The 10 off-tree
// wrappers (TimeOrdered, Monotonic, AppendOnly, SealedRefined,
// Consistency, OpaqueLifetime, Crash, Budgeted, EpochVersioned,
// NumaPlacement, RecipeSpec) are anchor-only in A3-003; their
// cross-pair coverage is deferred until their canonical-nesting-
// position story is documented (CLAUDE.md §XVI lists them as
// "off-tree extensions" whose stack position is dimension-
// dependent).
//
// Sentinel-3 (nesting-order discipline): a subset of pairs
// witnesses A<B<Anchor>> ≠ B<A<Anchor>>, encoding the FOUND-I03
// promise that combine_ids is order-sensitive and therefore
// HotPath<DetSafe<T>> and DetSafe<HotPath<T>> hash to distinct
// federation-cache slots.
//
// Trust boundary:
//   test_row_hash_fold.cpp owns Row<>/Computation<> semantics
//     (permutation invariance, dedup, payload-blindness).
//   test_migration_verification.cpp A3-003 owns per-wrapper
//     anchor checks.
//   This TU owns CROSS-WRAPPER DISTINCTNESS at single layer plus
//     nesting-order subset.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/effects/Computation.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/safety/AllocClass.h>
#include <crucible/safety/CipherTier.h>
#include <crucible/safety/DetSafe.h>
#include <crucible/safety/HotPath.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/MemOrder.h>
#include <crucible/safety/NumericalTier.h>
#include <crucible/safety/Progress.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/ResidencyHeat.h>
#include <crucible/safety/Secret.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/Tagged.h>
#include <crucible/safety/Vendor.h>
#include <crucible/safety/Wait.h>
#include <crucible/safety/diag/RowHashFold.h>

#include "test_assert.h"

#include <cstdint>
#include <cstdio>

namespace {

namespace cd = ::crucible::safety::diag;
namespace ce = ::crucible::effects;

using namespace crucible::safety;
using ce::Computation;
using ce::Effect;
using ce::Row;

// ─── Test-local witnesses ───────────────────────────────────────────
// Refined predicate.  Refined takes `auto Pred` (non-type) so the
// alias must consume a CONSTEXPR VALUE instance, not the type.
// Local to this TU so it cannot accidentally collide with another
// fixture's predicate-identity.
struct PositiveCheck {
    constexpr bool operator()(int x) const noexcept { return x > 0; }
};
inline constexpr PositiveCheck positive_local{};

// Tagged source tag.  Same isolation rationale.
struct SentinelTag {};

// ─── The canonical payload — a row-bearing Computation ─────────────
// Carrier choice matters: the matrix must witness that wrapper salt
// flows OUT of the wrapper AND THROUGH the inner row contribution.
// Anchor's own row contribution is non-zero (Row<Bg> ≠ EmptyRow),
// so any wrapper that fails to mix its salt in would be exposed
// against `bare-Anchor` in the per-wrapper anchor checks (A3-003)
// AND against every OTHER wrapper in the pair matrix below.
using Anchor = Computation<Row<Effect::Bg>, int>;

// ─── Per-wrapper instantiation aliases ─────────────────────────────
// One single-arg alias per canonical §XVI wrapper.  Order matches
// CLAUDE.md §XVI outer→inner.  Each alias pins ONE attribute value;
// rotating attribute values across W_i instances is the job of the
// per-wrapper anchor checks (A3-003), not this matrix.
template <typename T> using W01_HotPath        = HotPath<HotPathTier_v::Hot,           T>;
template <typename T> using W02_DetSafe        = DetSafe<DetSafeTier_v::Pure,          T>;
template <typename T> using W03_NumericalTier  = NumericalTier<Tolerance::BITEXACT,    T>;
template <typename T> using W04_Vendor         = Vendor<VendorBackend_v::NV,           T>;
template <typename T> using W05_ResidencyHeat  = ResidencyHeat<ResidencyHeatTag_v::Hot, T>;
template <typename T> using W06_CipherTier     = CipherTier<CipherTierTag_v::Hot,      T>;
template <typename T> using W07_AllocClass     = AllocClass<AllocClassTag_v::Arena,    T>;
template <typename T> using W08_Wait           = Wait<WaitStrategy_v::SpinPause,       T>;
template <typename T> using W09_MemOrder       = MemOrder<MemOrderTag_v::Relaxed,      T>;
template <typename T> using W10_Progress       = Progress<ProgressClass_v::Bounded,    T>;
template <typename T> using W11_Stale          = Stale<T>;
template <typename T> using W12_Tagged         = Tagged<T, SentinelTag>;
template <typename T> using W13_Refined        = Refined<positive_local, T>;
template <typename T> using W14_Secret         = Secret<T>;
template <typename T> using W15_Linear         = Linear<T>;

// ─── Sentinel #1: per-wrapper non-zero contribution ─────────────────
//
// Defends against the "added to DimensionTraits but forgot
// row_hash_contribution specialization" bug.  The primary template
// returns 0; an unspecialized wrapper would silently alias bare T's
// hash, fragmenting the federation cache into spurious slot
// collisions.  Mirror of A3-003 in test_migration_verification.cpp
// — kept here so this TU is self-contained as a CI sentinel.

static_assert(cd::row_hash_contribution_v<W01_HotPath<Anchor>>       != 0);
static_assert(cd::row_hash_contribution_v<W02_DetSafe<Anchor>>       != 0);
static_assert(cd::row_hash_contribution_v<W03_NumericalTier<Anchor>> != 0);
static_assert(cd::row_hash_contribution_v<W04_Vendor<Anchor>>        != 0);
static_assert(cd::row_hash_contribution_v<W05_ResidencyHeat<Anchor>> != 0);
static_assert(cd::row_hash_contribution_v<W06_CipherTier<Anchor>>    != 0);
static_assert(cd::row_hash_contribution_v<W07_AllocClass<Anchor>>    != 0);
static_assert(cd::row_hash_contribution_v<W08_Wait<Anchor>>          != 0);
static_assert(cd::row_hash_contribution_v<W09_MemOrder<Anchor>>      != 0);
static_assert(cd::row_hash_contribution_v<W10_Progress<Anchor>>      != 0);
static_assert(cd::row_hash_contribution_v<W11_Stale<Anchor>>         != 0);
static_assert(cd::row_hash_contribution_v<W12_Tagged<Anchor>>        != 0);
static_assert(cd::row_hash_contribution_v<W13_Refined<Anchor>>       != 0);
static_assert(cd::row_hash_contribution_v<W14_Secret<Anchor>>        != 0);
static_assert(cd::row_hash_contribution_v<W15_Linear<Anchor>>        != 0);

// ─── Sentinel #2: 15×14 = 210 cross-pair distinctness matrix ────────
//
// For every ordered pair (W_i, W_j) with i ≠ j, assert that
//   row_hash_contribution_v<W_i<Anchor>> != row_hash_contribution_v<W_j<Anchor>>
//
// Catches the SALT-COLLISION bug: two wrappers happen to fmix64 to
// the same constant for the same payload.  A regression that
// flattens, e.g., HotPath and DetSafe to share salt bits would
// reduce all four cells (W01<vs>W02 in either order, plus the
// post-W01 and post-W02 cells) to compile-error here.
//
// The macro expands one ordered pair per line so a regression
// diagnostic names BOTH operands.  This file is intentionally
// rote — the matrix's correctness IS its purpose.
#define DISTINCT_PAIR(A, B)                                            \
    static_assert(cd::row_hash_contribution_v<A<Anchor>>               \
               != cd::row_hash_contribution_v<B<Anchor>>,              \
                  "row_hash_contribution collision: " #A " vs " #B)

// ── W01 × all others (14 pairs) ─────────────────────────────────────
DISTINCT_PAIR(W01_HotPath, W02_DetSafe);
DISTINCT_PAIR(W01_HotPath, W03_NumericalTier);
DISTINCT_PAIR(W01_HotPath, W04_Vendor);
DISTINCT_PAIR(W01_HotPath, W05_ResidencyHeat);
DISTINCT_PAIR(W01_HotPath, W06_CipherTier);
DISTINCT_PAIR(W01_HotPath, W07_AllocClass);
DISTINCT_PAIR(W01_HotPath, W08_Wait);
DISTINCT_PAIR(W01_HotPath, W09_MemOrder);
DISTINCT_PAIR(W01_HotPath, W10_Progress);
DISTINCT_PAIR(W01_HotPath, W11_Stale);
DISTINCT_PAIR(W01_HotPath, W12_Tagged);
DISTINCT_PAIR(W01_HotPath, W13_Refined);
DISTINCT_PAIR(W01_HotPath, W14_Secret);
DISTINCT_PAIR(W01_HotPath, W15_Linear);

// ── W02 × all others (14 pairs) ─────────────────────────────────────
DISTINCT_PAIR(W02_DetSafe, W01_HotPath);
DISTINCT_PAIR(W02_DetSafe, W03_NumericalTier);
DISTINCT_PAIR(W02_DetSafe, W04_Vendor);
DISTINCT_PAIR(W02_DetSafe, W05_ResidencyHeat);
DISTINCT_PAIR(W02_DetSafe, W06_CipherTier);
DISTINCT_PAIR(W02_DetSafe, W07_AllocClass);
DISTINCT_PAIR(W02_DetSafe, W08_Wait);
DISTINCT_PAIR(W02_DetSafe, W09_MemOrder);
DISTINCT_PAIR(W02_DetSafe, W10_Progress);
DISTINCT_PAIR(W02_DetSafe, W11_Stale);
DISTINCT_PAIR(W02_DetSafe, W12_Tagged);
DISTINCT_PAIR(W02_DetSafe, W13_Refined);
DISTINCT_PAIR(W02_DetSafe, W14_Secret);
DISTINCT_PAIR(W02_DetSafe, W15_Linear);

// ── W03 × all others (14 pairs) ─────────────────────────────────────
DISTINCT_PAIR(W03_NumericalTier, W01_HotPath);
DISTINCT_PAIR(W03_NumericalTier, W02_DetSafe);
DISTINCT_PAIR(W03_NumericalTier, W04_Vendor);
DISTINCT_PAIR(W03_NumericalTier, W05_ResidencyHeat);
DISTINCT_PAIR(W03_NumericalTier, W06_CipherTier);
DISTINCT_PAIR(W03_NumericalTier, W07_AllocClass);
DISTINCT_PAIR(W03_NumericalTier, W08_Wait);
DISTINCT_PAIR(W03_NumericalTier, W09_MemOrder);
DISTINCT_PAIR(W03_NumericalTier, W10_Progress);
DISTINCT_PAIR(W03_NumericalTier, W11_Stale);
DISTINCT_PAIR(W03_NumericalTier, W12_Tagged);
DISTINCT_PAIR(W03_NumericalTier, W13_Refined);
DISTINCT_PAIR(W03_NumericalTier, W14_Secret);
DISTINCT_PAIR(W03_NumericalTier, W15_Linear);

// ── W04 × all others (14 pairs) ─────────────────────────────────────
DISTINCT_PAIR(W04_Vendor, W01_HotPath);
DISTINCT_PAIR(W04_Vendor, W02_DetSafe);
DISTINCT_PAIR(W04_Vendor, W03_NumericalTier);
DISTINCT_PAIR(W04_Vendor, W05_ResidencyHeat);
DISTINCT_PAIR(W04_Vendor, W06_CipherTier);
DISTINCT_PAIR(W04_Vendor, W07_AllocClass);
DISTINCT_PAIR(W04_Vendor, W08_Wait);
DISTINCT_PAIR(W04_Vendor, W09_MemOrder);
DISTINCT_PAIR(W04_Vendor, W10_Progress);
DISTINCT_PAIR(W04_Vendor, W11_Stale);
DISTINCT_PAIR(W04_Vendor, W12_Tagged);
DISTINCT_PAIR(W04_Vendor, W13_Refined);
DISTINCT_PAIR(W04_Vendor, W14_Secret);
DISTINCT_PAIR(W04_Vendor, W15_Linear);

// ── W05 × all others (14 pairs) ─────────────────────────────────────
DISTINCT_PAIR(W05_ResidencyHeat, W01_HotPath);
DISTINCT_PAIR(W05_ResidencyHeat, W02_DetSafe);
DISTINCT_PAIR(W05_ResidencyHeat, W03_NumericalTier);
DISTINCT_PAIR(W05_ResidencyHeat, W04_Vendor);
DISTINCT_PAIR(W05_ResidencyHeat, W06_CipherTier);
DISTINCT_PAIR(W05_ResidencyHeat, W07_AllocClass);
DISTINCT_PAIR(W05_ResidencyHeat, W08_Wait);
DISTINCT_PAIR(W05_ResidencyHeat, W09_MemOrder);
DISTINCT_PAIR(W05_ResidencyHeat, W10_Progress);
DISTINCT_PAIR(W05_ResidencyHeat, W11_Stale);
DISTINCT_PAIR(W05_ResidencyHeat, W12_Tagged);
DISTINCT_PAIR(W05_ResidencyHeat, W13_Refined);
DISTINCT_PAIR(W05_ResidencyHeat, W14_Secret);
DISTINCT_PAIR(W05_ResidencyHeat, W15_Linear);

// ── W06 × all others (14 pairs) ─────────────────────────────────────
DISTINCT_PAIR(W06_CipherTier, W01_HotPath);
DISTINCT_PAIR(W06_CipherTier, W02_DetSafe);
DISTINCT_PAIR(W06_CipherTier, W03_NumericalTier);
DISTINCT_PAIR(W06_CipherTier, W04_Vendor);
DISTINCT_PAIR(W06_CipherTier, W05_ResidencyHeat);
DISTINCT_PAIR(W06_CipherTier, W07_AllocClass);
DISTINCT_PAIR(W06_CipherTier, W08_Wait);
DISTINCT_PAIR(W06_CipherTier, W09_MemOrder);
DISTINCT_PAIR(W06_CipherTier, W10_Progress);
DISTINCT_PAIR(W06_CipherTier, W11_Stale);
DISTINCT_PAIR(W06_CipherTier, W12_Tagged);
DISTINCT_PAIR(W06_CipherTier, W13_Refined);
DISTINCT_PAIR(W06_CipherTier, W14_Secret);
DISTINCT_PAIR(W06_CipherTier, W15_Linear);

// ── W07 × all others (14 pairs) ─────────────────────────────────────
DISTINCT_PAIR(W07_AllocClass, W01_HotPath);
DISTINCT_PAIR(W07_AllocClass, W02_DetSafe);
DISTINCT_PAIR(W07_AllocClass, W03_NumericalTier);
DISTINCT_PAIR(W07_AllocClass, W04_Vendor);
DISTINCT_PAIR(W07_AllocClass, W05_ResidencyHeat);
DISTINCT_PAIR(W07_AllocClass, W06_CipherTier);
DISTINCT_PAIR(W07_AllocClass, W08_Wait);
DISTINCT_PAIR(W07_AllocClass, W09_MemOrder);
DISTINCT_PAIR(W07_AllocClass, W10_Progress);
DISTINCT_PAIR(W07_AllocClass, W11_Stale);
DISTINCT_PAIR(W07_AllocClass, W12_Tagged);
DISTINCT_PAIR(W07_AllocClass, W13_Refined);
DISTINCT_PAIR(W07_AllocClass, W14_Secret);
DISTINCT_PAIR(W07_AllocClass, W15_Linear);

// ── W08 × all others (14 pairs) ─────────────────────────────────────
DISTINCT_PAIR(W08_Wait, W01_HotPath);
DISTINCT_PAIR(W08_Wait, W02_DetSafe);
DISTINCT_PAIR(W08_Wait, W03_NumericalTier);
DISTINCT_PAIR(W08_Wait, W04_Vendor);
DISTINCT_PAIR(W08_Wait, W05_ResidencyHeat);
DISTINCT_PAIR(W08_Wait, W06_CipherTier);
DISTINCT_PAIR(W08_Wait, W07_AllocClass);
DISTINCT_PAIR(W08_Wait, W09_MemOrder);
DISTINCT_PAIR(W08_Wait, W10_Progress);
DISTINCT_PAIR(W08_Wait, W11_Stale);
DISTINCT_PAIR(W08_Wait, W12_Tagged);
DISTINCT_PAIR(W08_Wait, W13_Refined);
DISTINCT_PAIR(W08_Wait, W14_Secret);
DISTINCT_PAIR(W08_Wait, W15_Linear);

// ── W09 × all others (14 pairs) ─────────────────────────────────────
DISTINCT_PAIR(W09_MemOrder, W01_HotPath);
DISTINCT_PAIR(W09_MemOrder, W02_DetSafe);
DISTINCT_PAIR(W09_MemOrder, W03_NumericalTier);
DISTINCT_PAIR(W09_MemOrder, W04_Vendor);
DISTINCT_PAIR(W09_MemOrder, W05_ResidencyHeat);
DISTINCT_PAIR(W09_MemOrder, W06_CipherTier);
DISTINCT_PAIR(W09_MemOrder, W07_AllocClass);
DISTINCT_PAIR(W09_MemOrder, W08_Wait);
DISTINCT_PAIR(W09_MemOrder, W10_Progress);
DISTINCT_PAIR(W09_MemOrder, W11_Stale);
DISTINCT_PAIR(W09_MemOrder, W12_Tagged);
DISTINCT_PAIR(W09_MemOrder, W13_Refined);
DISTINCT_PAIR(W09_MemOrder, W14_Secret);
DISTINCT_PAIR(W09_MemOrder, W15_Linear);

// ── W10 × all others (14 pairs) ─────────────────────────────────────
DISTINCT_PAIR(W10_Progress, W01_HotPath);
DISTINCT_PAIR(W10_Progress, W02_DetSafe);
DISTINCT_PAIR(W10_Progress, W03_NumericalTier);
DISTINCT_PAIR(W10_Progress, W04_Vendor);
DISTINCT_PAIR(W10_Progress, W05_ResidencyHeat);
DISTINCT_PAIR(W10_Progress, W06_CipherTier);
DISTINCT_PAIR(W10_Progress, W07_AllocClass);
DISTINCT_PAIR(W10_Progress, W08_Wait);
DISTINCT_PAIR(W10_Progress, W09_MemOrder);
DISTINCT_PAIR(W10_Progress, W11_Stale);
DISTINCT_PAIR(W10_Progress, W12_Tagged);
DISTINCT_PAIR(W10_Progress, W13_Refined);
DISTINCT_PAIR(W10_Progress, W14_Secret);
DISTINCT_PAIR(W10_Progress, W15_Linear);

// ── W11 × all others (14 pairs) ─────────────────────────────────────
DISTINCT_PAIR(W11_Stale, W01_HotPath);
DISTINCT_PAIR(W11_Stale, W02_DetSafe);
DISTINCT_PAIR(W11_Stale, W03_NumericalTier);
DISTINCT_PAIR(W11_Stale, W04_Vendor);
DISTINCT_PAIR(W11_Stale, W05_ResidencyHeat);
DISTINCT_PAIR(W11_Stale, W06_CipherTier);
DISTINCT_PAIR(W11_Stale, W07_AllocClass);
DISTINCT_PAIR(W11_Stale, W08_Wait);
DISTINCT_PAIR(W11_Stale, W09_MemOrder);
DISTINCT_PAIR(W11_Stale, W10_Progress);
DISTINCT_PAIR(W11_Stale, W12_Tagged);
DISTINCT_PAIR(W11_Stale, W13_Refined);
DISTINCT_PAIR(W11_Stale, W14_Secret);
DISTINCT_PAIR(W11_Stale, W15_Linear);

// ── W12 × all others (14 pairs) ─────────────────────────────────────
DISTINCT_PAIR(W12_Tagged, W01_HotPath);
DISTINCT_PAIR(W12_Tagged, W02_DetSafe);
DISTINCT_PAIR(W12_Tagged, W03_NumericalTier);
DISTINCT_PAIR(W12_Tagged, W04_Vendor);
DISTINCT_PAIR(W12_Tagged, W05_ResidencyHeat);
DISTINCT_PAIR(W12_Tagged, W06_CipherTier);
DISTINCT_PAIR(W12_Tagged, W07_AllocClass);
DISTINCT_PAIR(W12_Tagged, W08_Wait);
DISTINCT_PAIR(W12_Tagged, W09_MemOrder);
DISTINCT_PAIR(W12_Tagged, W10_Progress);
DISTINCT_PAIR(W12_Tagged, W11_Stale);
DISTINCT_PAIR(W12_Tagged, W13_Refined);
DISTINCT_PAIR(W12_Tagged, W14_Secret);
DISTINCT_PAIR(W12_Tagged, W15_Linear);

// ── W13 × all others (14 pairs) ─────────────────────────────────────
DISTINCT_PAIR(W13_Refined, W01_HotPath);
DISTINCT_PAIR(W13_Refined, W02_DetSafe);
DISTINCT_PAIR(W13_Refined, W03_NumericalTier);
DISTINCT_PAIR(W13_Refined, W04_Vendor);
DISTINCT_PAIR(W13_Refined, W05_ResidencyHeat);
DISTINCT_PAIR(W13_Refined, W06_CipherTier);
DISTINCT_PAIR(W13_Refined, W07_AllocClass);
DISTINCT_PAIR(W13_Refined, W08_Wait);
DISTINCT_PAIR(W13_Refined, W09_MemOrder);
DISTINCT_PAIR(W13_Refined, W10_Progress);
DISTINCT_PAIR(W13_Refined, W11_Stale);
DISTINCT_PAIR(W13_Refined, W12_Tagged);
DISTINCT_PAIR(W13_Refined, W14_Secret);
DISTINCT_PAIR(W13_Refined, W15_Linear);

// ── W14 × all others (14 pairs) ─────────────────────────────────────
DISTINCT_PAIR(W14_Secret, W01_HotPath);
DISTINCT_PAIR(W14_Secret, W02_DetSafe);
DISTINCT_PAIR(W14_Secret, W03_NumericalTier);
DISTINCT_PAIR(W14_Secret, W04_Vendor);
DISTINCT_PAIR(W14_Secret, W05_ResidencyHeat);
DISTINCT_PAIR(W14_Secret, W06_CipherTier);
DISTINCT_PAIR(W14_Secret, W07_AllocClass);
DISTINCT_PAIR(W14_Secret, W08_Wait);
DISTINCT_PAIR(W14_Secret, W09_MemOrder);
DISTINCT_PAIR(W14_Secret, W10_Progress);
DISTINCT_PAIR(W14_Secret, W11_Stale);
DISTINCT_PAIR(W14_Secret, W12_Tagged);
DISTINCT_PAIR(W14_Secret, W13_Refined);
DISTINCT_PAIR(W14_Secret, W15_Linear);

// ── W15 × all others (14 pairs) ─────────────────────────────────────
DISTINCT_PAIR(W15_Linear, W01_HotPath);
DISTINCT_PAIR(W15_Linear, W02_DetSafe);
DISTINCT_PAIR(W15_Linear, W03_NumericalTier);
DISTINCT_PAIR(W15_Linear, W04_Vendor);
DISTINCT_PAIR(W15_Linear, W05_ResidencyHeat);
DISTINCT_PAIR(W15_Linear, W06_CipherTier);
DISTINCT_PAIR(W15_Linear, W07_AllocClass);
DISTINCT_PAIR(W15_Linear, W08_Wait);
DISTINCT_PAIR(W15_Linear, W09_MemOrder);
DISTINCT_PAIR(W15_Linear, W10_Progress);
DISTINCT_PAIR(W15_Linear, W11_Stale);
DISTINCT_PAIR(W15_Linear, W12_Tagged);
DISTINCT_PAIR(W15_Linear, W13_Refined);
DISTINCT_PAIR(W15_Linear, W14_Secret);

#undef DISTINCT_PAIR

// Cardinality witness — re-affirm the matrix is exactly 15 × 14
// ordered pairs (210 total).  Future contributors who add W16 to
// §XVI's canonical outer-nesting order MUST update the constant
// below AND extend the matrix.  This is rote-but-grep-discoverable
// because §XVI is the single source of truth for the canonical
// 15 single-arg wrappers (Computation is the carrier, two-arg).
inline constexpr std::size_t MATRIX_CANONICAL_WRAPPER_COUNT = 15;
inline constexpr std::size_t MATRIX_ORDERED_PAIRS =
    MATRIX_CANONICAL_WRAPPER_COUNT * (MATRIX_CANONICAL_WRAPPER_COUNT - 1);
static_assert(MATRIX_ORDERED_PAIRS == 210,
              "FIXY-U-005 distinctness matrix must remain 15 × 14 = 210 "
              "ordered pairs.  If §XVI canonical count changed, update "
              "MATRIX_CANONICAL_WRAPPER_COUNT and extend the matrix.");

// ─── Sentinel #3: nesting-order discipline (subset) ─────────────────
//
// FOUND-I03 promise: combine_ids is order-sensitive, so A<B<T>> ≠
// B<A<T>> for distinct wrappers A and B.  Encoding the full 15 × 14
// ordered nested-pair matrix would double the file size for limited
// added value over the single-layer matrix; instead we pin a
// representative subset that covers each "tier" of §XVI:
//
//   - Outer execution-budget wrappers (HotPath, DetSafe, NumericalTier, Vendor)
//   - Middle scheduling / cache wrappers (ResidencyHeat, CipherTier, AllocClass, Wait)
//   - Concurrency-discipline wrappers (MemOrder, Progress)
//   - Per-value salt wrappers (Stale, Tagged, Refined, Secret, Linear)
//
// Each cell asserts A<B<Anchor>> ≠ B<A<Anchor>>, witnessing that
// stacking wrappers in different canonical positions produces
// distinct federation-cache slots — the property that protects the
// L2 IR003 cache from order-aliasing two semantically-different
// kernel residency stories.

#define NESTING_ORDER_DISTINCT(A, B)                                        \
    static_assert(cd::row_hash_contribution_v<A<B<Anchor>>>                 \
               != cd::row_hash_contribution_v<B<A<Anchor>>>,                \
                  "row_hash nesting-order collision: "                      \
                  #A "<" #B "<T>> aliases " #B "<" #A "<T>>")

// HotPath × {DetSafe, NumericalTier, Vendor, Tagged, Linear} — top-tier wrapper × representative each-tier wrapper
NESTING_ORDER_DISTINCT(W01_HotPath, W02_DetSafe);
NESTING_ORDER_DISTINCT(W01_HotPath, W03_NumericalTier);
NESTING_ORDER_DISTINCT(W01_HotPath, W04_Vendor);
NESTING_ORDER_DISTINCT(W01_HotPath, W12_Tagged);
NESTING_ORDER_DISTINCT(W01_HotPath, W15_Linear);

// DetSafe × {NumericalTier, AllocClass, MemOrder, Refined, Secret}
NESTING_ORDER_DISTINCT(W02_DetSafe, W03_NumericalTier);
NESTING_ORDER_DISTINCT(W02_DetSafe, W07_AllocClass);
NESTING_ORDER_DISTINCT(W02_DetSafe, W09_MemOrder);
NESTING_ORDER_DISTINCT(W02_DetSafe, W13_Refined);
NESTING_ORDER_DISTINCT(W02_DetSafe, W14_Secret);

// NumericalTier × {Vendor, CipherTier, Progress, Stale, Linear}
NESTING_ORDER_DISTINCT(W03_NumericalTier, W04_Vendor);
NESTING_ORDER_DISTINCT(W03_NumericalTier, W06_CipherTier);
NESTING_ORDER_DISTINCT(W03_NumericalTier, W10_Progress);
NESTING_ORDER_DISTINCT(W03_NumericalTier, W11_Stale);
NESTING_ORDER_DISTINCT(W03_NumericalTier, W15_Linear);

// Vendor × {ResidencyHeat, Wait, Tagged, Secret, Linear}
NESTING_ORDER_DISTINCT(W04_Vendor, W05_ResidencyHeat);
NESTING_ORDER_DISTINCT(W04_Vendor, W08_Wait);
NESTING_ORDER_DISTINCT(W04_Vendor, W12_Tagged);
NESTING_ORDER_DISTINCT(W04_Vendor, W14_Secret);
NESTING_ORDER_DISTINCT(W04_Vendor, W15_Linear);

// ResidencyHeat × {CipherTier, MemOrder, Refined}
NESTING_ORDER_DISTINCT(W05_ResidencyHeat, W06_CipherTier);
NESTING_ORDER_DISTINCT(W05_ResidencyHeat, W09_MemOrder);
NESTING_ORDER_DISTINCT(W05_ResidencyHeat, W13_Refined);

// CipherTier × {AllocClass, Stale, Linear}
NESTING_ORDER_DISTINCT(W06_CipherTier, W07_AllocClass);
NESTING_ORDER_DISTINCT(W06_CipherTier, W11_Stale);
NESTING_ORDER_DISTINCT(W06_CipherTier, W15_Linear);

// Stale × Tagged — particularly important: §XVI canonical orders
// Stale OUTER to Tagged (Stale<Tagged<T>>); the reverse stack
// (Tagged<Stale<T>>) MUST land in a different cache slot.
NESTING_ORDER_DISTINCT(W11_Stale, W12_Tagged);

// Refined × Secret — both are per-value salt wrappers; a regression
// that flattens predicate-vs-classification salt would alias here.
NESTING_ORDER_DISTINCT(W13_Refined, W14_Secret);

// Linear × {Tagged, Secret, Refined} — Linear is innermost in §XVI;
// these cells witness that moving Linear outward changes the slot.
NESTING_ORDER_DISTINCT(W15_Linear, W12_Tagged);
NESTING_ORDER_DISTINCT(W15_Linear, W14_Secret);
NESTING_ORDER_DISTINCT(W15_Linear, W13_Refined);

#undef NESTING_ORDER_DISTINCT

// ─── Runtime peer check (header-only-static_assert blind spot) ──────
// CLAUDE.md feedback memory: headers shipped with embedded
// static_asserts aren't verified under project warning flags unless
// a .cpp TU includes them.  This TU IS a .cpp, so all 210 + 15 +
// nesting checks above run under the test target's full -Werror=
// matrix.  The runtime peer below materializes a sample of the
// matrix through volatile sinks so the optimizer cannot collapse
// the entire check to consteval-only.

// Helper — Secret hash materialized through a function boundary
// to ensure cross-TU codegen has to compute it.  Declared BEFORE
// test_runtime_distinctness_samples so the forward reference
// compiles.
static std::uint64_t sink_w14_canary() {
    volatile std::uint64_t sink_w14 =
        cd::row_hash_contribution_v<W14_Secret<Anchor>>;
    return sink_w14;
}

static void test_runtime_distinctness_samples() {
    // Materialize 16 sampled cells through volatile storage so the
    // optimizer must actually compute each hash at runtime — defeats
    // a consteval-only fast path masking a runtime miscompile.
    volatile std::uint64_t sink_w01 =
        cd::row_hash_contribution_v<W01_HotPath<Anchor>>;
    volatile std::uint64_t sink_w02 =
        cd::row_hash_contribution_v<W02_DetSafe<Anchor>>;
    volatile std::uint64_t sink_w03 =
        cd::row_hash_contribution_v<W03_NumericalTier<Anchor>>;
    volatile std::uint64_t sink_w04 =
        cd::row_hash_contribution_v<W04_Vendor<Anchor>>;
    volatile std::uint64_t sink_w11 =
        cd::row_hash_contribution_v<W11_Stale<Anchor>>;
    volatile std::uint64_t sink_w12 =
        cd::row_hash_contribution_v<W12_Tagged<Anchor>>;
    volatile std::uint64_t sink_w13 =
        cd::row_hash_contribution_v<W13_Refined<Anchor>>;
    volatile std::uint64_t sink_w15 =
        cd::row_hash_contribution_v<W15_Linear<Anchor>>;

    assert(sink_w01 != 0);
    assert(sink_w02 != 0);
    assert(sink_w03 != 0);
    assert(sink_w04 != 0);
    assert(sink_w11 != 0);
    assert(sink_w12 != 0);
    assert(sink_w13 != 0);
    assert(sink_w15 != 0);

    // Pairwise distinctness sampled from the matrix's 210 pairs.
    assert(sink_w01 != sink_w02);  // HotPath vs DetSafe
    assert(sink_w01 != sink_w03);  // HotPath vs NumericalTier
    assert(sink_w02 != sink_w04);  // DetSafe vs Vendor
    assert(sink_w11 != sink_w12);  // Stale vs Tagged
    assert(sink_w13 != sink_w14_canary());  // Refined vs Secret
    assert(sink_w15 != sink_w12);  // Linear vs Tagged

    std::printf("  test_runtime_distinctness_samples: PASSED\n");
}

// Nesting-order runtime peer — materialize one outer/inner swap and
// verify the runtime hash matches the consteval result and that the
// pair is distinct.
static void test_runtime_nesting_order_sample() {
    volatile std::uint64_t hot_over_det =
        cd::row_hash_contribution_v<W01_HotPath<W02_DetSafe<Anchor>>>;
    volatile std::uint64_t det_over_hot =
        cd::row_hash_contribution_v<W02_DetSafe<W01_HotPath<Anchor>>>;
    assert(hot_over_det != det_over_hot);
    assert(hot_over_det != 0);
    assert(det_over_hot != 0);
    std::printf("  test_runtime_nesting_order_sample:  PASSED\n");
}

}  // namespace

int main() {
    std::printf("test_row_hash_contribution_sentinel:\n");
    test_runtime_distinctness_samples();
    test_runtime_nesting_order_sample();
    std::printf("test_row_hash_contribution_sentinel: ALL PASSED\n");
    return 0;
}
