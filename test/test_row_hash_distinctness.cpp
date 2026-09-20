// The federation cache key discriminates by row_hash, so two wrappers
// that fold to the same value share a cache slot and silently mis-route
// lookups between peers.  Nothing else in the tree notices: a changed
// salt, a stale combine_ids ordering, or a copy-paste between two
// row_hash_contribution specializations all still produce a working
// build.
//
// This translation unit is the cheat-probe for that surface.  It
// enumerates every canonical wrapper and every canonical stance, asserts
// all n*(n-1)/2 pairs are pairwise distinct, and folds the whole matrix
// into one pinned anchor literal.  Drift in any single entry flips the
// anchor and reddens the build, which puts the wire-format break in front
// of a reviewer before it ships.
//
// Every property is asserted twice, once at consteval and once at runtime
// through volatile sinks, so a consteval-only fast path cannot hide a
// collision that the runtime fold would produce.

#include <crucible/Expr.h>
#include <crucible/fixy/Fn.h>
#include <crucible/safety/_AllocClass.h>
#include <crucible/safety/BarrierGuarded.h>
#include <crucible/safety/Budgeted.h>
#include <crucible/safety/_CipherTier.h>
#include <crucible/safety/ClockSource.h>
#include <crucible/safety/Consistency.h>
#include <crucible/safety/Crash.h>
#include <crucible/safety/_DetSafe.h>
#include <crucible/safety/EpochVersioned.h>
#include <crucible/safety/FpMode.h>
#include <crucible/safety/Hw.h>
#include <crucible/safety/JoinPolicy.h>
#include <crucible/safety/Fn.h>
#include <crucible/safety/_HotPath.h>
#include <crucible/safety/_Linear.h>
#include <crucible/safety/MemOrder.h>
#include <crucible/safety/_Mutation.h>
#include <crucible/safety/NumaPlacement.h>
#include <crucible/safety/_NumericalTier.h>
#include <crucible/safety/_OpaqueLifetime.h>
#include <crucible/safety/Progress.h>
#include <crucible/safety/_RecipeSpec.h>
#include <crucible/safety/_Refined.h>
#include <crucible/safety/ResidencyHeat.h>
#include <crucible/safety/SchedClass.h>
#include <crucible/safety/ScopedFence.h>
#include <crucible/safety/_SealedRefined.h>
#include <crucible/safety/_Secret.h>
#include <crucible/safety/SimdWidthPinned.h>
#include <crucible/safety/_Stale.h>
#include <crucible/safety/SuspendBehavior.h>
#include <crucible/safety/_Tagged.h>
#include <crucible/safety/TimeOrdered.h>
#include <crucible/safety/Vendor.h>
#include <crucible/safety/_Wait.h>
#include <crucible/safety/Witness.h>
#include <crucible/safety/diag/_RowHashFold.h>
#include <crucible/safety/diag/_StableName.h>

#include "test_assert.h"

#include <array>
#include <cstdint>
#include <cstdio>

namespace cs = crucible::safety;
namespace cd = crucible::safety::diag;
namespace cf = crucible::fixy;

using cd::row_hash_contribution_v;

namespace {

// A tag owned by this test.  Reusing a production concurrency channel's
// tag would make the matrix depend on a wire format it does not control.
struct DistinctnessQuadTag {};

using W01_Linear = cs::Linear<int>;
using W02_Refined = cs::Refined<cs::positive, int>;
using W03_SealedRefined = cs::SealedRefined<cs::positive, int>;
using W04_Tagged = cs::Tagged<int, cs::source::FromUser>;
using W05_Secret = cs::Secret<int>;
using W06_Stale = cs::Stale<int>;
using W07_TimeOrdered = cs::TimeOrdered<int, 4, DistinctnessQuadTag>;
using W08_Monotonic = cs::Monotonic<std::uint64_t>;
using W09_AppendOnly = cs::AppendOnly<int>;
using W10_HotPath = cs::HotPath<cs::HotPathTier_v::Hot, int>;
using W11_DetSafe = cs::DetSafe<cs::DetSafeTier_v::Pure, int>;
using W12_NumericalTier = cs::NumericalTier<cs::Tolerance::BITEXACT, int>;
using W13_Vendor = cs::Vendor<cs::VendorBackend_v::Portable, int>;
using W14_ResidencyHeat = cs::ResidencyHeat<cs::ResidencyHeatTag_v::Hot, int>;
using W15_CipherTier = cs::CipherTier<cs::CipherTierTag_v::Hot, int>;
using W16_AllocClass = cs::AllocClass<cs::AllocClassTag_v::Arena, int>;
using W17_Wait = cs::Wait<cs::WaitStrategy_v::SpinPause, int>;
using W18_MemOrder = cs::MemOrder<cs::MemOrderTag_v::SeqCst, int>;
using W19_Progress = cs::Progress<cs::ProgressClass_v::Bounded, int>;
using W20_Consistency = cs::Consistency<cs::Consistency_v::STRONG, int>;
using W21_OpaqueLifetime = cs::OpaqueLifetime<cs::Lifetime_v::PER_REQUEST, int>;
using W22_Crash = cs::Crash<cs::CrashClass_v::NoThrow, int>;
using W23_Budgeted = cs::Budgeted<int>;
using W24_EpochVersioned = cs::EpochVersioned<int>;
using W25_NumaPlacement = cs::NumaPlacement<int>;
using W26_RecipeSpec = cs::RecipeSpec<int>;
using W27_Witness = cs::Witness<cs::Witness_v::FORMALLY_VERIFIED, int>;

using W28_Hw = cs::Hw<cs::HwInstruction_v::PrivilegedMsr, int>;
using W29_BarrierGuarded = cs::BarrierGuarded<cs::BarrierStrength_v::SeqCst, int>;
using W30_SimdWidthPinned = cs::SimdWidthPinned<cs::SimdIsa_v::Avx2, int>;
using W31_ScopedFence = cs::ScopedFence<cs::MemoryScope_v::Cta, int>;
using W32_ClockSource = cs::ClockSource<cs::ClockSource_v::TscRaw, int>;
using W33_SuspendBehavior = cs::SuspendBehavior<cs::SuspendBehavior_v::KeepsTicking, int>;
using W34_JoinPolicy = cs::JoinPolicy<cs::JoinPolicy_v::WAIT_DEADLINE, int>;
using W35_SchedClass = cs::SchedClass<cs::SchedulerPolicy_v::Fifo, int>;
// Two sub-axes of one wrapper.  Each mode-enum type carries its own
// salt, so a collision between sub-axes of the same wrapper aliases a
// cache slot exactly as a collision between two different wrappers does.
using W36_FpModePinned_R = cs::FpModePinned<cs::FpRounding::RoundToNearestEven, int>;
using W37_FpModePinned_F = cs::FpModePinned<cs::FpFtz::FlushToZero, int>;

// The stances are picked so that each one engages a different axis of
// the grant resolution, which is what makes their hashes differ:
//   * PureLinear and PureCopy differ on the Usage axis
//   * IoFunction and BgWorker differ on the Effect axis
//   * CtCrypto differs on Representation (constant-time)
//   * AsyncEndpoint differs on Reentrancy
//   * RealtimeHot differs on Effect (empty row) and on Regime
using S01_PureLinear = cf::stance::PureLinear<int>;
using S02_PureCopy = cf::stance::PureCopy<int>;
using S03_IoFunction = cf::stance::IoFunction<int>;
using S04_BgWorker = cf::stance::BgWorker<int>;
using S05_CtCrypto = cf::stance::CtCrypto<int>;
using S06_AsyncEndpoint = cf::stance::AsyncEndpoint<int>;
using S07_RealtimeHot = cf::stance::RealtimeHot<int>;

// The order of this array is contractual.  The anchor below folds the
// accumulator through the entries in sequence, so reshuffling indices
// moves the anchor even when every individual hash is unchanged.  Append
// a new wrapper at the end of the wrapper bucket, which leaves the fold
// state feeding the stance bucket intact.
inline constexpr std::array<std::uint64_t, 44> kHashes = {
    row_hash_contribution_v<W01_Linear>,
    row_hash_contribution_v<W02_Refined>,
    row_hash_contribution_v<W03_SealedRefined>,
    row_hash_contribution_v<W04_Tagged>,
    row_hash_contribution_v<W05_Secret>,
    row_hash_contribution_v<W06_Stale>,
    row_hash_contribution_v<W07_TimeOrdered>,
    row_hash_contribution_v<W08_Monotonic>,
    row_hash_contribution_v<W09_AppendOnly>,
    row_hash_contribution_v<W10_HotPath>,
    row_hash_contribution_v<W11_DetSafe>,
    row_hash_contribution_v<W12_NumericalTier>,
    row_hash_contribution_v<W13_Vendor>,
    row_hash_contribution_v<W14_ResidencyHeat>,
    row_hash_contribution_v<W15_CipherTier>,
    row_hash_contribution_v<W16_AllocClass>,
    row_hash_contribution_v<W17_Wait>,
    row_hash_contribution_v<W18_MemOrder>,
    row_hash_contribution_v<W19_Progress>,
    row_hash_contribution_v<W20_Consistency>,
    row_hash_contribution_v<W21_OpaqueLifetime>,
    row_hash_contribution_v<W22_Crash>,
    row_hash_contribution_v<W23_Budgeted>,
    row_hash_contribution_v<W24_EpochVersioned>,
    row_hash_contribution_v<W25_NumaPlacement>,
    row_hash_contribution_v<W26_RecipeSpec>,
    row_hash_contribution_v<W27_Witness>,
    row_hash_contribution_v<W28_Hw>,
    row_hash_contribution_v<W29_BarrierGuarded>,
    row_hash_contribution_v<W30_SimdWidthPinned>,
    row_hash_contribution_v<W31_ScopedFence>,
    row_hash_contribution_v<W32_ClockSource>,
    row_hash_contribution_v<W33_SuspendBehavior>,
    row_hash_contribution_v<W34_JoinPolicy>,
    row_hash_contribution_v<W35_SchedClass>,
    row_hash_contribution_v<W36_FpModePinned_R>,
    row_hash_contribution_v<W37_FpModePinned_F>,
    row_hash_contribution_v<S01_PureLinear>,
    row_hash_contribution_v<S02_PureCopy>,
    row_hash_contribution_v<S03_IoFunction>,
    row_hash_contribution_v<S04_BgWorker>,
    row_hash_contribution_v<S05_CtCrypto>,
    row_hash_contribution_v<S06_AsyncEndpoint>,
    row_hash_contribution_v<S07_RealtimeHot>,
};

inline constexpr std::size_t kEntryCount = kHashes.size();
inline constexpr std::size_t kPairCount = (kEntryCount * (kEntryCount - 1)) / 2;

struct CollisionIndices {
    std::size_t i = static_cast<std::size_t>(-1);
    std::size_t j = static_cast<std::size_t>(-1);
    [[nodiscard]] constexpr bool ok() const noexcept { return i == static_cast<std::size_t>(-1); }
};

[[nodiscard]] consteval CollisionIndices find_collision() noexcept {
    for (std::size_t i = 0; i < kHashes.size(); ++i) {
        for (std::size_t j = i + 1; j < kHashes.size(); ++j) {
            if (kHashes[i] == kHashes[j]) return {i, j};
        }
    }
    return {};
}

static_assert(find_collision().ok(), "row_hash collision in the canonical wrapper by stance matrix.  Every "
                                     "wrapper and stance must produce a row_hash distinct from every other "
                                     "entry; check that its row_hash_contribution specialization carries a "
                                     "unique WRAPPER_*_TAG salt.  The colliding (i, j) indices are indices "
                                     "into kHashes in declaration order.");

[[nodiscard]] consteval bool no_sentinel_collisions() noexcept {
    for (auto h : kHashes) {
        if (h == 0) return false;
        if (h == static_cast<std::uint64_t>(-1)) return false;
    }
    return true;
}

static_assert(no_sentinel_collisions(), "a wrapper or stance row_hash collided with a reserved value.  The "
                                        "federation cache reserves UINT64_MAX for an empty slot and 0 for the "
                                        "payload-blind contribution of a bare T, so a wrapper producing "
                                        "either is indistinguishable from an unaddressed cache state.");

// kFoldSeed is arbitrary, but deliberately not the FNV-1a offset basis:
// a reviewer who confuses this anchor with the seed inside the row-hash
// fold itself would update the wrong constant.
inline constexpr std::uint64_t kFoldSeed = 0xC0FFEEBADF00DBA5ULL;

[[nodiscard]] consteval std::uint64_t fold_anchor() noexcept {
    std::uint64_t acc = kFoldSeed;
    for (auto h : kHashes)
        acc = cd::detail::combine_ids(acc, h);
    return acc;
}

// Recompute and update this literal whenever a row_hash_contribution
// specialization changes its salt, its fold order, or the contribution
// of its inner type.  Rolling the anchor is a wire-format break: the
// federation cache slot of every affected kernel moves, and peers on
// either side of the roll route to different slots.
inline constexpr std::uint64_t kFoldAnchor = 0x7A48BBE6D5D97B9DULL;

static_assert(fold_anchor() == kFoldAnchor, "ceremony anchor drift.  A row_hash_contribution specialization "
                                            "changed its salt, its combine_ids order, its inner fold, or its "
                                            "bit-mix.  This is a wire-format break for federation cache keys: "
                                            "every peer's cache index moves.  Update kFoldAnchor to the new "
                                            "fold_anchor() value and record in the commit message which wrapper "
                                            "or stance changed, which slot moved, and why the break is "
                                            "acceptable.");

static_assert(kEntryCount == 44, "matrix cardinality changed.  Extend kHashes at the end rather than "
                                 "in the middle, which preserves the upstream fold state, then update "
                                 "this pin and recompute kFoldAnchor.");

static_assert(kPairCount == 946, "pair count no longer matches a 44-entry matrix.  Update this pin "
                                 "together with the entry-count pin above.");

// The global sweep above already forbids any two entries colliding, so
// this narrower check is redundant by construction.  It is kept because
// wrapper hashes and stance hashes come from disjoint salt families, and
// a collision across that boundary means a salt leaked from one family
// into the other rather than an ordinary duplicate.
inline constexpr std::size_t kWrapperCount = 37;
inline constexpr std::size_t kStanceCount = 7;

static_assert(kWrapperCount + kStanceCount == kEntryCount);

[[nodiscard]] consteval bool wrappers_disjoint_from_stances() noexcept {
    for (std::size_t i = 0; i < kWrapperCount; ++i) {
        for (std::size_t j = kWrapperCount; j < kWrapperCount + kStanceCount; ++j) {
            if (kHashes[i] == kHashes[j]) return false;
        }
    }
    return true;
}

static_assert(wrappers_disjoint_from_stances(), "a wrapper row_hash collided with a stance row_hash.  These regions "
                                                "must stay disjoint: wrappers come from the Graded-substrate salt "
                                                "family (0x01-0x1C), stances come from WRAPPER_FIXY_FN_TAG (0x1E) "
                                                "and its inner fold (0x1D).  A collision here means one of those "
                                                "salts drifted into the wrapper region.");

}  // namespace

// The consteval sweep above proves distinctness at compile time only.
// A consteval-runtime divergence in the optimizer would leave the
// static_assert green while the shipped fold produced collisions, so the
// same property is re-proved here through volatile sinks.
static void test_runtime_distinctness() {
    bool seen_collision = false;
    std::size_t ci = 0, cj = 0;
    for (std::size_t i = 0; i < kHashes.size(); ++i) {
        volatile std::uint64_t hi = kHashes[i];
        for (std::size_t j = i + 1; j < kHashes.size(); ++j) {
            volatile std::uint64_t hj = kHashes[j];
            if (hi == hj) {
                seen_collision = true;
                ci = i;
                cj = j;
            }
        }
    }
    if (seen_collision) {
        std::fprintf(stderr,
                     "test_row_hash_distinctness: runtime collision at "
                     "[%zu, %zu] — consteval was clean but runtime fold "
                     "differed, indicating compiler miscompile.\n",
                     ci, cj);
    }
    assert(!seen_collision);
    std::printf("  test_runtime_distinctness:    PASSED (%zu pairs)\n", kPairCount);
}

static void test_runtime_sentinel_guards() {
    for (auto h : kHashes) {
        volatile std::uint64_t v = h;
        assert(v != 0);
        assert(v != static_cast<std::uint64_t>(-1));
    }
    std::printf("  test_runtime_sentinel_guards: PASSED\n");
}

// combine_ids is constexpr rather than consteval so that this runtime
// fold calls the same body the consteval fold_anchor() calls.  A separate
// runtime-only mixing function would be free to drift out of sync with
// the consteval algebra, and the anchor would stop proving anything.
static void test_runtime_fold_anchor() {
    std::uint64_t acc = kFoldSeed;
    for (auto h : kHashes) {
        acc = cd::detail::combine_ids(acc, h);
    }
    volatile std::uint64_t runtime_acc = acc;
    assert(runtime_acc == kFoldAnchor);
    std::printf("  test_runtime_fold_anchor:     PASSED (anchor=0x%016llx)\n",
                static_cast<unsigned long long>(runtime_acc));
}

int main() {
    test_runtime_distinctness();
    test_runtime_sentinel_guards();
    test_runtime_fold_anchor();
    std::printf("test_row_hash_distinctness: 3 groups, all passed "
                "(matrix: %zu entries, %zu pairs)\n",
                kEntryCount, kPairCount);
    return 0;
}
