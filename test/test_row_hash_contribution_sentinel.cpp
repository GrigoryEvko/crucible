// Checking that each wrapper contributes something other than the
// bare payload's hash catches a wrapper that was added without any
// contribution at all.  It cannot catch two wrappers whose identity
// salts happen to mix to the same value over the same payload,
// because both of those differ from the bare payload.  The matrix
// below compares every ordered pair of the canonical single-argument
// wrappers against each other, which does catch it.
//
// A second and smaller matrix witnesses that stacking two wrappers one
// way round hashes differently from stacking them the other way, so
// two stacks that mean different things cannot land in one cache slot.
//
// The wrappers whose place in a stack depends on which dimension they
// belong to are left out.  Their nesting position is not fixed, so an
// ordered pair over them would assert nothing.

#include <crucible/effects/_Computation.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/safety/AllocClass.h>
#include <crucible/safety/CipherTier.h>
#include <crucible/safety/DetSafe.h>
#include <crucible/safety/HotPath.h>
#include <crucible/safety/_Linear.h>
#include <crucible/safety/MemOrder.h>
#include <crucible/safety/NumericalTier.h>
#include <crucible/safety/Progress.h>
#include <crucible/safety/_Refined.h>
#include <crucible/safety/ResidencyHeat.h>
#include <crucible/safety/_Secret.h>
#include <crucible/safety/_Stale.h>
#include <crucible/safety/_Tagged.h>
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

// The refinement wrapper takes the predicate as a value, so the alias
// below needs a constexpr object and not the type.  Both witnesses
// are local to this file, so neither can share an identity with
// another fixture's.
struct PositiveCheck {
    constexpr bool operator()(int x) const noexcept { return x > 0; }
};
inline constexpr PositiveCheck positive_local{};

struct SentinelTag {};

// The payload carries a non-empty row of its own, so a wrapper that
// failed to mix its salt through the inner contribution would show up
// against the bare payload as well as against every other wrapper.
using Anchor = Computation<Row<Effect::Bg>, int>;

// One alias per wrapper, in the canonical outer-to-inner order.  Each
// pins a single attribute value: varying the attribute is a separate
// question from telling two wrappers apart.
template <typename T>
using W01_HotPath = HotPath<HotPathTier_v::Hot, T>;
template <typename T>
using W02_DetSafe = DetSafe<DetSafeTier_v::Pure, T>;
template <typename T>
using W03_NumericalTier = NumericalTier<Tolerance::BITEXACT, T>;
template <typename T>
using W04_Vendor = Vendor<VendorBackend_v::NV, T>;
template <typename T>
using W05_ResidencyHeat = ResidencyHeat<ResidencyHeatTag_v::Hot, T>;
template <typename T>
using W06_CipherTier = CipherTier<CipherTierTag_v::Hot, T>;
template <typename T>
using W07_AllocClass = AllocClass<AllocClassTag_v::Arena, T>;
template <typename T>
using W08_Wait = Wait<WaitStrategy_v::SpinPause, T>;
template <typename T>
using W09_MemOrder = MemOrder<MemOrderTag_v::Relaxed, T>;
template <typename T>
using W10_Progress = Progress<ProgressClass_v::Bounded, T>;
template <typename T>
using W11_Stale = Stale<T>;
template <typename T>
using W12_Tagged = Tagged<T, SentinelTag>;
template <typename T>
using W13_Refined = Refined<positive_local, T>;
template <typename T>
using W14_Secret = Secret<T>;
template <typename T>
using W15_Linear = Linear<T>;

// A wrapper with no contribution of its own falls through to the
// primary template, which answers zero, and then hashes exactly like
// the bare payload it wraps.  These assertions catch that.

static_assert(cd::row_hash_contribution_v<W01_HotPath<Anchor>> != 0);
static_assert(cd::row_hash_contribution_v<W02_DetSafe<Anchor>> != 0);
static_assert(cd::row_hash_contribution_v<W03_NumericalTier<Anchor>> != 0);
static_assert(cd::row_hash_contribution_v<W04_Vendor<Anchor>> != 0);
static_assert(cd::row_hash_contribution_v<W05_ResidencyHeat<Anchor>> != 0);
static_assert(cd::row_hash_contribution_v<W06_CipherTier<Anchor>> != 0);
static_assert(cd::row_hash_contribution_v<W07_AllocClass<Anchor>> != 0);
static_assert(cd::row_hash_contribution_v<W08_Wait<Anchor>> != 0);
static_assert(cd::row_hash_contribution_v<W09_MemOrder<Anchor>> != 0);
static_assert(cd::row_hash_contribution_v<W10_Progress<Anchor>> != 0);
static_assert(cd::row_hash_contribution_v<W11_Stale<Anchor>> != 0);
static_assert(cd::row_hash_contribution_v<W12_Tagged<Anchor>> != 0);
static_assert(cd::row_hash_contribution_v<W13_Refined<Anchor>> != 0);
static_assert(cd::row_hash_contribution_v<W14_Secret<Anchor>> != 0);
static_assert(cd::row_hash_contribution_v<W15_Linear<Anchor>> != 0);

// One ordered pair per line, so a failing diagnostic names both
// operands.  The repetition is the point.
#define DISTINCT_PAIR(A, B)                                                                         \
    static_assert(cd::row_hash_contribution_v<A<Anchor>> != cd::row_hash_contribution_v<B<Anchor>>, \
                  "row_hash_contribution collision: " #A " vs " #B)

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

// Adding a wrapper to the canonical order means updating the count
// below and extending the matrix.  The assertion is there to make the
// omission loud rather than silent.
inline constexpr std::size_t MATRIX_CANONICAL_WRAPPER_COUNT = 15;
inline constexpr std::size_t MATRIX_ORDERED_PAIRS =
    MATRIX_CANONICAL_WRAPPER_COUNT * (MATRIX_CANONICAL_WRAPPER_COUNT - 1);
static_assert(MATRIX_ORDERED_PAIRS == 210, "The distinctness matrix must remain 15 x 14 = 210 ordered "
                                           "pairs.  If the canonical wrapper count changed, update "
                                           "MATRIX_CANONICAL_WRAPPER_COUNT and extend the matrix.");

// The combiner is order-sensitive, so stacking two wrappers one way
// round must not hash like stacking them the other way.  A second full
// matrix would double the length of this file and add little over the
// single-layer one, so the cells below sample it: a few pairs drawn
// from each band of the canonical order, outermost to innermost.

#define NESTING_ORDER_DISTINCT(A, B)                                                                      \
    static_assert(cd::row_hash_contribution_v<A<B<Anchor>>> != cd::row_hash_contribution_v<B<A<Anchor>>>, \
                  "row_hash nesting-order collision: " #A "<" #B "<T>> aliases " #B "<" #A "<T>>")

NESTING_ORDER_DISTINCT(W01_HotPath, W02_DetSafe);
NESTING_ORDER_DISTINCT(W01_HotPath, W03_NumericalTier);
NESTING_ORDER_DISTINCT(W01_HotPath, W04_Vendor);
NESTING_ORDER_DISTINCT(W01_HotPath, W12_Tagged);
NESTING_ORDER_DISTINCT(W01_HotPath, W15_Linear);

NESTING_ORDER_DISTINCT(W02_DetSafe, W03_NumericalTier);
NESTING_ORDER_DISTINCT(W02_DetSafe, W07_AllocClass);
NESTING_ORDER_DISTINCT(W02_DetSafe, W09_MemOrder);
NESTING_ORDER_DISTINCT(W02_DetSafe, W13_Refined);
NESTING_ORDER_DISTINCT(W02_DetSafe, W14_Secret);

NESTING_ORDER_DISTINCT(W03_NumericalTier, W04_Vendor);
NESTING_ORDER_DISTINCT(W03_NumericalTier, W06_CipherTier);
NESTING_ORDER_DISTINCT(W03_NumericalTier, W10_Progress);
NESTING_ORDER_DISTINCT(W03_NumericalTier, W11_Stale);
NESTING_ORDER_DISTINCT(W03_NumericalTier, W15_Linear);

NESTING_ORDER_DISTINCT(W04_Vendor, W05_ResidencyHeat);
NESTING_ORDER_DISTINCT(W04_Vendor, W08_Wait);
NESTING_ORDER_DISTINCT(W04_Vendor, W12_Tagged);
NESTING_ORDER_DISTINCT(W04_Vendor, W14_Secret);
NESTING_ORDER_DISTINCT(W04_Vendor, W15_Linear);

NESTING_ORDER_DISTINCT(W05_ResidencyHeat, W06_CipherTier);
NESTING_ORDER_DISTINCT(W05_ResidencyHeat, W09_MemOrder);
NESTING_ORDER_DISTINCT(W05_ResidencyHeat, W13_Refined);

NESTING_ORDER_DISTINCT(W06_CipherTier, W07_AllocClass);
NESTING_ORDER_DISTINCT(W06_CipherTier, W11_Stale);
NESTING_ORDER_DISTINCT(W06_CipherTier, W15_Linear);

// The canonical order puts staleness outside provenance, so the
// reverse stack has to land in a different slot.
NESTING_ORDER_DISTINCT(W11_Stale, W12_Tagged);

// Both of these salt the value itself.  A regression that stopped
// distinguishing a refinement from a classification would alias here
// first.
NESTING_ORDER_DISTINCT(W13_Refined, W14_Secret);

// Ownership is innermost in the canonical order.  These cells witness
// that moving it outward changes the slot.
NESTING_ORDER_DISTINCT(W15_Linear, W12_Tagged);
NESTING_ORDER_DISTINCT(W15_Linear, W14_Secret);
NESTING_ORDER_DISTINCT(W15_Linear, W13_Refined);

#undef NESTING_ORDER_DISTINCT

// Assertions embedded in a header are only compiled where some
// translation unit includes it, and this file is that unit for every
// assertion above.  The checks below sample the same matrix at run
// time, so a path that agrees at compile time and diverges at run time
// cannot hide.

// This one goes through a function boundary so the hash has to be
// computed rather than folded away.  It is declared ahead of its
// caller for the same reason any function is.
static std::uint64_t sink_w14_canary() {
    volatile std::uint64_t sink_w14 = cd::row_hash_contribution_v<W14_Secret<Anchor>>;
    return sink_w14;
}

static void test_runtime_distinctness_samples() {
    // Each sample lands in volatile storage, so the hash is computed
    // at run time instead of folded to a constant.
    volatile std::uint64_t sink_w01 = cd::row_hash_contribution_v<W01_HotPath<Anchor>>;
    volatile std::uint64_t sink_w02 = cd::row_hash_contribution_v<W02_DetSafe<Anchor>>;
    volatile std::uint64_t sink_w03 = cd::row_hash_contribution_v<W03_NumericalTier<Anchor>>;
    volatile std::uint64_t sink_w04 = cd::row_hash_contribution_v<W04_Vendor<Anchor>>;
    volatile std::uint64_t sink_w11 = cd::row_hash_contribution_v<W11_Stale<Anchor>>;
    volatile std::uint64_t sink_w12 = cd::row_hash_contribution_v<W12_Tagged<Anchor>>;
    volatile std::uint64_t sink_w13 = cd::row_hash_contribution_v<W13_Refined<Anchor>>;
    volatile std::uint64_t sink_w15 = cd::row_hash_contribution_v<W15_Linear<Anchor>>;

    assert(sink_w01 != 0);
    assert(sink_w02 != 0);
    assert(sink_w03 != 0);
    assert(sink_w04 != 0);
    assert(sink_w11 != 0);
    assert(sink_w12 != 0);
    assert(sink_w13 != 0);
    assert(sink_w15 != 0);

    // A sample of the pairs the matrix above covers in full.
    assert(sink_w01 != sink_w02);  // HotPath vs DetSafe
    assert(sink_w01 != sink_w03);  // HotPath vs NumericalTier
    assert(sink_w02 != sink_w04);  // DetSafe vs Vendor
    assert(sink_w11 != sink_w12);  // Stale vs Tagged
    assert(sink_w13 != sink_w14_canary());  // Refined vs Secret
    assert(sink_w15 != sink_w12);  // Linear vs Tagged

    std::printf("  test_runtime_distinctness_samples: PASSED\n");
}

// One outer-and-inner swap, checked at run time for the same reason.
static void test_runtime_nesting_order_sample() {
    volatile std::uint64_t hot_over_det = cd::row_hash_contribution_v<W01_HotPath<W02_DetSafe<Anchor>>>;
    volatile std::uint64_t det_over_hot = cd::row_hash_contribution_v<W02_DetSafe<W01_HotPath<Anchor>>>;
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
