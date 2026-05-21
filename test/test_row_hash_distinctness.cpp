// FIXY-V-008: row_hash distinctness × wire-format-break ceremony.
//
// Closes the federation-cache regression surface opened by FIXY-V-001
// + FIXY-V-002.  Once `safety::fn::Fn` + `fixy::fn` started carrying a
// real row_hash, the KernelCacheKey gained capability-divergent
// discrimination — but discrimination is only as strong as the
// pairwise distinctness of every `row_hash_contribution` specialization
// it covers.  A subtle change to a salt, a stale combine_ids ordering,
// or a copy-paste between two `row_hash_contribution<>` specializations
// could silently collapse two wrappers' hashes and nobody would notice
// until cross-org cache misses spiked.  This TU is the cheat-probe of
// the federation cache key: it enumerates every canonical wrapper ×
// every canonical stance, asserts all `n*(n-1)/2` pairs are pairwise
// distinct, and pins a single rolling-fold anchor that captures the
// whole matrix.  Drift in ANY entry's hash flips the anchor; the
// CI build reddens; review notices the wire-format-break.
//
// Entries: 26 canonical wrappers (the CLAUDE.md §XVI canonical 16
// outer-nesting plus the 10 off-tree extensions documented in
// DimensionTraits.h §822-§847) × 8 single-parameter fixy stances
// (`fixy::stance::*`) = 34 distinct hashes.  Pair count is
// 34 * 33 / 2 = 561 ≥ 240 (the original task description's lower bound
// gross-counted the "rough" matrix size; the actual exercised
// surface is ~2.3× larger by construction).
//
// Discipline aligned with `test_row_hash_fold.cpp` (FOUND-I02 peer):
//   * static_assert distinctness via consteval O(n²) sweep
//   * static_assert sentinel guards (≠ 0, ≠ UINT64_MAX)
//   * static_assert fold anchor pinned to a single literal
//   * runtime peers via volatile sinks defeat consteval-only fast-path
//     miscompiles per feedback_algebra_runtime_smoke_test_discipline
//   * wire-format-break ceremony: any commit that changes a
//     `row_hash_contribution<>` specialization MUST update kFoldAnchor
//     and document the cache-slot change in commit history.

#include <crucible/Expr.h>                       // detail::fmix64
#include <crucible/fixy/Fn.h>                    // fixy::fn + stance::*
#include <crucible/safety/AllocClass.h>
#include <crucible/safety/Budgeted.h>
#include <crucible/safety/CipherTier.h>
#include <crucible/safety/Consistency.h>
#include <crucible/safety/Crash.h>
#include <crucible/safety/DetSafe.h>
#include <crucible/safety/EpochVersioned.h>
#include <crucible/safety/Fn.h>                  // safety::fn::Fn (V-002 peer)
#include <crucible/safety/HotPath.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/MemOrder.h>
#include <crucible/safety/Mutation.h>            // Monotonic
#include <crucible/safety/NumaPlacement.h>
#include <crucible/safety/NumericalTier.h>
#include <crucible/safety/OpaqueLifetime.h>
#include <crucible/safety/Progress.h>
#include <crucible/safety/RecipeSpec.h>
#include <crucible/safety/Refined.h>             // positive predicate
#include <crucible/safety/ResidencyHeat.h>
#include <crucible/safety/SealedRefined.h>
#include <crucible/safety/Secret.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/Tagged.h>              // Tagged + source::FromUser
#include <crucible/safety/TimeOrdered.h>
#include <crucible/safety/Vendor.h>
#include <crucible/safety/Wait.h>
#include <crucible/safety/Witness.h>             // FIXY-V-054 / V-055
#include <crucible/safety/diag/RowHashFold.h>
#include <crucible/safety/diag/StableName.h>     // detail::combine_ids

#include "test_assert.h"

#include <array>
#include <cstdint>
#include <cstdio>

namespace cs = crucible::safety;
namespace cd = crucible::safety::diag;
namespace cf = crucible::fixy;

using cd::row_hash_contribution_v;

namespace {

// Local 4-channel TimeOrdered tag — independent of any production
// concurrency channel so this test never tugs at a wire format.
struct DistinctnessQuadTag {};

// ── 26 canonical wrappers (mirrors DimensionTraits.h §822-§847) ────
//
// The W* aliases below match the `wrapper_dimension<W>` quadruples
// pinned in DimensionTraits.h.  Adding a 27th canonical wrapper
// requires (a) extending this list and (b) updating kEntryCount AND
// kFoldAnchor below — the ceremony anchor catches forgotten entries.
using W01_Linear         = cs::Linear<int>;
using W02_Refined        = cs::Refined<cs::positive, int>;
using W03_SealedRefined  = cs::SealedRefined<cs::positive, int>;
using W04_Tagged         = cs::Tagged<int, cs::source::FromUser>;
using W05_Secret         = cs::Secret<int>;
using W06_Stale          = cs::Stale<int>;
using W07_TimeOrdered    = cs::TimeOrdered<int, 4, DistinctnessQuadTag>;
using W08_Monotonic      = cs::Monotonic<std::uint64_t>;
using W09_AppendOnly     = cs::AppendOnly<int>;
using W10_HotPath        = cs::HotPath<cs::HotPathTier_v::Hot, int>;
using W11_DetSafe        = cs::DetSafe<cs::DetSafeTier_v::Pure, int>;
using W12_NumericalTier  = cs::NumericalTier<cs::Tolerance::BITEXACT, int>;
using W13_Vendor         = cs::Vendor<cs::VendorBackend_v::Portable, int>;
using W14_ResidencyHeat  = cs::ResidencyHeat<cs::ResidencyHeatTag_v::Hot, int>;
using W15_CipherTier     = cs::CipherTier<cs::CipherTierTag_v::Hot, int>;
using W16_AllocClass     = cs::AllocClass<cs::AllocClassTag_v::Arena, int>;
using W17_Wait           = cs::Wait<cs::WaitStrategy_v::SpinPause, int>;
using W18_MemOrder       = cs::MemOrder<cs::MemOrderTag_v::SeqCst, int>;
using W19_Progress       = cs::Progress<cs::ProgressClass_v::Bounded, int>;
using W20_Consistency    = cs::Consistency<cs::Consistency_v::STRONG, int>;
using W21_OpaqueLifetime = cs::OpaqueLifetime<cs::Lifetime_v::PER_REQUEST, int>;
using W22_Crash          = cs::Crash<cs::CrashClass_v::NoThrow, int>;
using W23_Budgeted       = cs::Budgeted<int>;
using W24_EpochVersioned = cs::EpochVersioned<int>;
using W25_NumaPlacement  = cs::NumaPlacement<int>;
using W26_RecipeSpec     = cs::RecipeSpec<int>;
using W27_Witness        = cs::Witness<cs::Witness_v::FORMALLY_VERIFIED, int>;

// ── 8 canonical single-parameter fixy stances ─────────────────────
//
// `fixy::fn<T, Grants...>` resolves to `safety::fn::Fn<T, ...>` via
// 19-axis per-axis grant resolution; FIXY-V-002 specialized
// `row_hash_contribution<safety::fn::Fn<...>>` to fold all 19 axes.
// These 8 stances exercise the axis-engagement combinatorics:
//   * PureLinear/PureCopy differ on Usage axis
//   * IoFunction/BgWorker differ on Effect axis
//   * CtCrypto differs on Representation axis (constant-time)
//   * AsyncEndpoint differs on Reentrancy axis
//   * CooperativeBg/RealtimeHot differ on Synchronization + HotPath
using S01_PureLinear     = cf::stance::PureLinear<int>;
using S02_PureCopy       = cf::stance::PureCopy<int>;
using S03_IoFunction     = cf::stance::IoFunction<int>;
using S04_BgWorker       = cf::stance::BgWorker<int>;
using S05_CtCrypto       = cf::stance::CtCrypto<int>;
using S06_AsyncEndpoint  = cf::stance::AsyncEndpoint<int>;
using S07_CooperativeBg  = cf::stance::CooperativeBg<int>;
using S08_RealtimeHot    = cf::stance::RealtimeHot<int>;

// ── 35-entry hash matrix ──────────────────────────────────────────
//
// Order is contractual — the fold ceremony pins acc-after-each-entry,
// so reshuffling indices changes the anchor literal even if every
// individual hash stays the same.  When adding an entry, append at
// the END of the wrapper bucket (preserves all upstream fold state for
// the stance bucket).
inline constexpr std::array<std::uint64_t, 35> kHashes = {
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
    row_hash_contribution_v<S01_PureLinear>,
    row_hash_contribution_v<S02_PureCopy>,
    row_hash_contribution_v<S03_IoFunction>,
    row_hash_contribution_v<S04_BgWorker>,
    row_hash_contribution_v<S05_CtCrypto>,
    row_hash_contribution_v<S06_AsyncEndpoint>,
    row_hash_contribution_v<S07_CooperativeBg>,
    row_hash_contribution_v<S08_RealtimeHot>,
};

inline constexpr std::size_t kEntryCount = kHashes.size();
inline constexpr std::size_t kPairCount  = (kEntryCount * (kEntryCount - 1)) / 2;

// ── Pairwise distinctness at consteval ────────────────────────────
//
// O(n²) sweep; for n=34 that's 561 comparisons.  Returns the index
// of the first collision (or sentinel sentinel-pair on success) so
// the diagnostic message in `static_assert` can point at the
// colliding row.
struct CollisionIndices {
    std::size_t i = static_cast<std::size_t>(-1);
    std::size_t j = static_cast<std::size_t>(-1);
    [[nodiscard]] constexpr bool ok() const noexcept {
        return i == static_cast<std::size_t>(-1);
    }
};

[[nodiscard]] consteval CollisionIndices find_collision() noexcept {
    for (std::size_t i = 0; i < kHashes.size(); ++i) {
        for (std::size_t j = i + 1; j < kHashes.size(); ++j) {
            if (kHashes[i] == kHashes[j]) return {i, j};
        }
    }
    return {};
}

static_assert(find_collision().ok(),
    "FIXY-V-008: row_hash collision in the canonical wrapper × stance "
    "matrix.  A new wrapper or stance MUST produce a row_hash distinct "
    "from every existing entry; check that its row_hash_contribution<> "
    "specialization includes a unique WRAPPER_*_TAG salt (RowHashFold.h "
    ":~190-235).  The colliding (i, j) indices map to the kHashes "
    "array order documented above the array literal.");

// ── Sentinel guards ───────────────────────────────────────────────
//
// The federation cache uses two reserved values: UINT64_MAX is the
// EMPTY-slot sentinel (FoundI04 + RowHash::sentinel()), and 0 is the
// bare-T payload-blind contribution.  Neither value may be produced
// by ANY wrapper or stance, or its slot would be indistinguishable
// from an empty / unaddressed cache state.
[[nodiscard]] consteval bool no_sentinel_collisions() noexcept {
    for (auto h : kHashes) {
        if (h == 0) return false;
        if (h == static_cast<std::uint64_t>(-1)) return false;
    }
    return true;
}

static_assert(no_sentinel_collisions(),
    "FIXY-V-008: a wrapper or stance row_hash collided with the "
    "EMPTY-slot sentinel (UINT64_MAX) or the bare-T payload-blind "
    "default (0).  This collapses federation cache slot assignment "
    "and would silently mis-route lookups.");

// ── Wire-format-break ceremony anchor ─────────────────────────────
//
// Fold every entry into one literal via the SAME `combine_ids` used
// by RowHashFold.h, seeded with a TU-stable constant.  Drift in ANY
// entry's hash flips this value; reviewers see the static_assert
// failure, must add a commit-message ceremony note documenting which
// federation cache slot moved, and update the literal in lockstep.
//
// `kFoldSeed` is arbitrary — pick something memorable but not
// FNV1A_OFFSET_BASIS so a confused reviewer can't paste-confuse this
// anchor with the row-hash internal seed.
inline constexpr std::uint64_t kFoldSeed = 0xC0FFEEBADF00DBA5ULL;

[[nodiscard]] consteval std::uint64_t fold_anchor() noexcept {
    std::uint64_t acc = kFoldSeed;
    for (auto h : kHashes) acc = cd::detail::combine_ids(acc, h);
    return acc;
}

// PINNED ANCHOR — recompute and update when a row_hash specialization
// changes salt / order / Inner contribution.  Document the change in
// the commit message via "FIXY-V-008: anchor rolled OLD → NEW because
// <reason>" — the OLD value tells reviewers which prior cache slots
// were affected.
//
// V-055 (2026-05-22): rolled OLD=0x6C5E81D4DA13027B →
// NEW=0xCF47CF6C1D6D6AAA after appending W27_Witness at wrapper-bucket
// position 27.  Inserting mid-array DOES re-fold the trailing 8 stance
// entries, but the entries themselves did not change; this is a
// fold-position drift, not a hash drift, and is the expected
// federation-key wire-format-break for an Observability-axis carrier
// joining the universe.  See V-055 commit.
inline constexpr std::uint64_t kFoldAnchor = 0xCF47CF6C1D6D6AAAULL;

static_assert(fold_anchor() == kFoldAnchor,
    "FIXY-V-008: ceremony anchor drift.  A row_hash_contribution<> "
    "specialization changed (different salt, different combine_ids "
    "order, different Inner fold, or different bit-mix).  This is a "
    "wire-format break for federation cache keys — every peer's "
    "KernelCacheKey index moves.  Update kFoldAnchor to the new "
    "fold_anchor() value AND document the cause in the commit "
    "message: which wrapper/stance changed, what slot moved, and "
    "why the break is acceptable.  See FIXY-V-001/002 for the "
    "original mint of the salt vocabulary.");

// Cardinality pin — adding a new entry without updating kEntryCount
// would compile silently otherwise.  This anchors the size to
// review.
static_assert(kEntryCount == 35,
    "FIXY-V-008: matrix cardinality changed.  Update kEntryCount, "
    "extend kHashes at the end (NOT the middle — preserves upstream "
    "fold state), and recompute kFoldAnchor.");

static_assert(kPairCount == 595,
    "FIXY-V-008: 35 * 34 / 2 = 595.  If you see this fire, "
    "kEntryCount changed without updating kPairCount.");

// ── Cross-bucket distinctness — wrappers vs stances ───────────────
//
// Belt-and-suspenders: even though the global pairwise sweep above
// already enforces no two entries collide, this narrower check
// documents the load-bearing property that NO wrapper hashes to ANY
// stance — they live in disjoint cache regions by construction.
inline constexpr std::size_t kWrapperCount = 27;
inline constexpr std::size_t kStanceCount  = 8;

static_assert(kWrapperCount + kStanceCount == kEntryCount);

[[nodiscard]] consteval bool wrappers_disjoint_from_stances() noexcept {
    for (std::size_t i = 0; i < kWrapperCount; ++i) {
        for (std::size_t j = kWrapperCount;
             j < kWrapperCount + kStanceCount; ++j)
        {
            if (kHashes[i] == kHashes[j]) return false;
        }
    }
    return true;
}

static_assert(wrappers_disjoint_from_stances(),
    "FIXY-V-008: a wrapper row_hash collided with a stance row_hash.  "
    "These hash regions must stay disjoint: wrappers come from the "
    "Graded-substrate salt family (0x01-0x1C), stances come from "
    "WRAPPER_FIXY_FN_TAG (0x1E) and its inner safety::fn::Fn fold "
    "(0x1D).  A collision here means the WRAPPER_SAFETY_FN_TAG or "
    "WRAPPER_FIXY_FN_TAG salt drifted into a wrapper region.");

}  // namespace

// ── Runtime peer — defeats consteval-only fast-path miscompile ────
//
// `feedback_algebra_runtime_smoke_test_discipline` requires every
// algebra/* + effects/* header that asserts identities at consteval
// to also assert them at runtime via volatile sinks — otherwise a
// rare consteval-runtime divergence in the optimizer would be
// invisible.  We extend the same discipline to the federation cache
// key surface.
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
    std::printf("  test_runtime_distinctness:    PASSED (%zu pairs)\n",
                kPairCount);
}

static void test_runtime_sentinel_guards() {
    for (auto h : kHashes) {
        volatile std::uint64_t v = h;
        assert(v != 0);
        assert(v != static_cast<std::uint64_t>(-1));
    }
    std::printf("  test_runtime_sentinel_guards: PASSED\n");
}

// Re-derive the ceremony anchor at runtime using the published
// `combine_ids` algebra.  combine_ids is `consteval` and can't be
// invoked from runtime, but `crucible::detail::fmix64` is constexpr
// and the Boost mix is a 4-op straight line — we replicate it
// verbatim and assert equality with the consteval value.
[[nodiscard]] static std::uint64_t combine_ids_runtime(
    std::uint64_t a, std::uint64_t b) noexcept
{
    a ^= b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2);
    return ::crucible::detail::fmix64(a);
}

static void test_runtime_fold_anchor() {
    std::uint64_t acc = kFoldSeed;
    for (auto h : kHashes) acc = combine_ids_runtime(acc, h);
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
