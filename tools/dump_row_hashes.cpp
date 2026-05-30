// FIXY-FOUND-052 — Cross-build row_hash determinism witness.
//
// Prints every `row_hash_contribution<W>::value` in the canonical §XVI
// matrix + 10 Tier-L off-tree extensions + 8 stance aliases, one entry
// per line as `<label> 0x%016lx`.  CI diffs the output against
// tools/row_hash_golden.txt; any mismatch reddens the build with a
// wire-format ceremony note required.
//
// ── Why this exists ────────────────────────────────────────────────
//
// Within one build, every row_hash_contribution<W>::value is
// deterministic AND pinned by the FIXY-V-008 ceremony anchor at
// test/test_row_hash_distinctness.cpp.  But that anchor's static_assert
// fires in the SAME TU that computed the hashes — so a TU-context-
// fragility regression in display_string_of (the upstream of
// stable_type_id) could silently move hashes without tripping any
// in-TU static_assert.  The cure is a SEPARATE binary that emits the
// hashes verbatim → captured snapshot under VCS → CI re-runs the
// binary → bit-identical or fail.
//
// The 45-entry enumeration MUST stay in lockstep with
// test/test_row_hash_distinctness.cpp's kHashes array.  Drift is
// caught by the static_assert below: the dump tool computes the SAME
// kFoldAnchor as the test, so a divergence in count, ordering, or
// any individual hash flips the anchor and fails the build BEFORE the
// golden-file diff would notice.
//
// ── Ceremony discipline ────────────────────────────────────────────
//
// When a 46th wrapper is added to test_row_hash_distinctness.cpp:
//   1. Update the test's kHashes (extend), kEntryCount (45→46), and
//      kFoldAnchor (re-roll, document in commit message).
//   2. Update THIS tool's kEntries (extend), kEntryCount (45→46), and
//      kFoldAnchor (use the SAME re-rolled value as the test).
//   3. Re-run the binary, capture stdout, replace
//      tools/row_hash_golden.txt with the new output.
//   4. Commit all three (test + tool + golden) atomically.
//
// CI catches partial migrations:
//   * test+tool not in lockstep → tool's static_assert fires (anchor mismatch).
//   * tool+golden not in lockstep → CTest entry `row_hash_golden_diff`
//     fires (textual diff fails).

#include <crucible/Expr.h>                       // detail::fmix64
#include <crucible/fixy/Fn.h>                    // fixy::fn + stance::*
#include <crucible/safety/AllocClass.h>
#include <crucible/safety/BarrierGuarded.h>
#include <crucible/safety/Budgeted.h>
#include <crucible/safety/CipherTier.h>
#include <crucible/safety/ClockSource.h>
#include <crucible/safety/Consistency.h>
#include <crucible/safety/Crash.h>
#include <crucible/safety/DetSafe.h>
#include <crucible/safety/EpochVersioned.h>
#include <crucible/safety/Fn.h>
#include <crucible/safety/FpMode.h>
#include <crucible/safety/HotPath.h>
#include <crucible/safety/Hw.h>
#include <crucible/safety/JoinPolicy.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/MemOrder.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/NumaPlacement.h>
#include <crucible/safety/NumericalTier.h>
#include <crucible/safety/OpaqueLifetime.h>
#include <crucible/safety/Progress.h>
#include <crucible/safety/RecipeSpec.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/ResidencyHeat.h>
#include <crucible/safety/SchedClass.h>
#include <crucible/safety/ScopedFence.h>
#include <crucible/safety/SealedRefined.h>
#include <crucible/safety/Secret.h>
#include <crucible/safety/SimdWidthPinned.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/SuspendBehavior.h>
#include <crucible/safety/Tagged.h>
#include <crucible/safety/TimeOrdered.h>
#include <crucible/safety/Vendor.h>
#include <crucible/safety/Wait.h>
#include <crucible/safety/Witness.h>
#include <crucible/safety/diag/RowHashFold.h>
#include <crucible/safety/diag/StableName.h>

#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace cs = crucible::safety;
namespace cd = crucible::safety::diag;
namespace cf = crucible::fixy;

using cd::row_hash_contribution_v;

namespace {

// Local 4-channel TimeOrdered tag — independent of any production
// channel.  Matches test_row_hash_distinctness.cpp's DistinctnessQuadTag
// in shape AND in name suffix; the latter matters because
// stable_type_id<T> consumes display_string_of, so the suffix is
// reflected in the W07 hash.  Using a DIFFERENT name here would emit a
// different hash for W07, breaking the lockstep with the test.  Keep
// the suffix identical.
struct DistinctnessQuadTag {};

// ── Wrapper enumeration — MUST mirror test_row_hash_distinctness.cpp ──
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
using W28_Hw             = cs::Hw<cs::HwInstruction_v::PrivilegedMsr, int>;
using W29_BarrierGuarded = cs::BarrierGuarded<cs::BarrierStrength_v::SeqCst, int>;
using W30_SimdWidthPinned = cs::SimdWidthPinned<cs::SimdIsa_v::Avx2, int>;
using W31_ScopedFence    = cs::ScopedFence<cs::MemoryScope_v::Cta, int>;
using W32_ClockSource    = cs::ClockSource<cs::ClockSource_v::TscRaw, int>;
using W33_SuspendBehavior = cs::SuspendBehavior<cs::SuspendBehavior_v::KeepsTicking, int>;
using W34_JoinPolicy     = cs::JoinPolicy<cs::JoinPolicy_v::WAIT_DEADLINE, int>;
using W35_SchedClass     = cs::SchedClass<cs::SchedulerPolicy_v::Fifo, int>;
using W36_FpModePinned_R = cs::FpModePinned<cs::FpRounding::RoundToNearestEven, int>;
using W37_FpModePinned_F = cs::FpModePinned<cs::FpFtz::FlushToZero, int>;

using S01_PureLinear     = cf::stance::PureLinear<int>;
using S02_PureCopy       = cf::stance::PureCopy<int>;
using S03_IoFunction     = cf::stance::IoFunction<int>;
using S04_BgWorker       = cf::stance::BgWorker<int>;
using S05_CtCrypto       = cf::stance::CtCrypto<int>;
using S06_AsyncEndpoint  = cf::stance::AsyncEndpoint<int>;
using S07_RealtimeHot    = cf::stance::RealtimeHot<int>;

struct LabeledEntry {
    const char*   label;
    std::uint64_t value;
};

inline constexpr std::array<LabeledEntry, 44> kEntries = {{
    {"W01_Linear",          row_hash_contribution_v<W01_Linear>},
    {"W02_Refined",         row_hash_contribution_v<W02_Refined>},
    {"W03_SealedRefined",   row_hash_contribution_v<W03_SealedRefined>},
    {"W04_Tagged",          row_hash_contribution_v<W04_Tagged>},
    {"W05_Secret",          row_hash_contribution_v<W05_Secret>},
    {"W06_Stale",           row_hash_contribution_v<W06_Stale>},
    {"W07_TimeOrdered",     row_hash_contribution_v<W07_TimeOrdered>},
    {"W08_Monotonic",       row_hash_contribution_v<W08_Monotonic>},
    {"W09_AppendOnly",      row_hash_contribution_v<W09_AppendOnly>},
    {"W10_HotPath",         row_hash_contribution_v<W10_HotPath>},
    {"W11_DetSafe",         row_hash_contribution_v<W11_DetSafe>},
    {"W12_NumericalTier",   row_hash_contribution_v<W12_NumericalTier>},
    {"W13_Vendor",          row_hash_contribution_v<W13_Vendor>},
    {"W14_ResidencyHeat",   row_hash_contribution_v<W14_ResidencyHeat>},
    {"W15_CipherTier",      row_hash_contribution_v<W15_CipherTier>},
    {"W16_AllocClass",      row_hash_contribution_v<W16_AllocClass>},
    {"W17_Wait",            row_hash_contribution_v<W17_Wait>},
    {"W18_MemOrder",        row_hash_contribution_v<W18_MemOrder>},
    {"W19_Progress",        row_hash_contribution_v<W19_Progress>},
    {"W20_Consistency",     row_hash_contribution_v<W20_Consistency>},
    {"W21_OpaqueLifetime",  row_hash_contribution_v<W21_OpaqueLifetime>},
    {"W22_Crash",           row_hash_contribution_v<W22_Crash>},
    {"W23_Budgeted",        row_hash_contribution_v<W23_Budgeted>},
    {"W24_EpochVersioned",  row_hash_contribution_v<W24_EpochVersioned>},
    {"W25_NumaPlacement",   row_hash_contribution_v<W25_NumaPlacement>},
    {"W26_RecipeSpec",      row_hash_contribution_v<W26_RecipeSpec>},
    {"W27_Witness",         row_hash_contribution_v<W27_Witness>},
    {"W28_Hw",              row_hash_contribution_v<W28_Hw>},
    {"W29_BarrierGuarded",  row_hash_contribution_v<W29_BarrierGuarded>},
    {"W30_SimdWidthPinned", row_hash_contribution_v<W30_SimdWidthPinned>},
    {"W31_ScopedFence",     row_hash_contribution_v<W31_ScopedFence>},
    {"W32_ClockSource",     row_hash_contribution_v<W32_ClockSource>},
    {"W33_SuspendBehavior", row_hash_contribution_v<W33_SuspendBehavior>},
    {"W34_JoinPolicy",      row_hash_contribution_v<W34_JoinPolicy>},
    {"W35_SchedClass",      row_hash_contribution_v<W35_SchedClass>},
    {"W36_FpModePinned_R",  row_hash_contribution_v<W36_FpModePinned_R>},
    {"W37_FpModePinned_F",  row_hash_contribution_v<W37_FpModePinned_F>},
    {"S01_PureLinear",      row_hash_contribution_v<S01_PureLinear>},
    {"S02_PureCopy",        row_hash_contribution_v<S02_PureCopy>},
    {"S03_IoFunction",      row_hash_contribution_v<S03_IoFunction>},
    {"S04_BgWorker",        row_hash_contribution_v<S04_BgWorker>},
    {"S05_CtCrypto",        row_hash_contribution_v<S05_CtCrypto>},
    {"S06_AsyncEndpoint",   row_hash_contribution_v<S06_AsyncEndpoint>},
    {"S07_RealtimeHot",     row_hash_contribution_v<S07_RealtimeHot>},
}};

inline constexpr std::size_t kEntryCount = kEntries.size();

// ── Cross-link to test/test_row_hash_distinctness.cpp anchor ──────
//
// SAME seed + SAME fold algorithm + SAME entry ordering ⇒ SAME anchor.
// A divergence in count, ordering, or any individual hash flips this
// value and reddens the build BEFORE the golden-file diff would notice.
inline constexpr std::uint64_t kFoldSeed   = 0xC0FFEEBADF00DBA5ULL;
inline constexpr std::uint64_t kFoldAnchor = 0x7A48BBE6D5D97B9DULL;

[[nodiscard]] consteval std::uint64_t fold_anchor() noexcept {
    std::uint64_t acc = kFoldSeed;
    for (const auto& e : kEntries) {
        acc = cd::detail::combine_ids(acc, e.value);
    }
    return acc;
}

static_assert(fold_anchor() == kFoldAnchor,
    "FIXY-FOUND-052: dump tool's fold anchor diverged from "
    "test/test_row_hash_distinctness.cpp's kFoldAnchor.  The two "
    "enumerations MUST stay synchronized — when adding/removing a "
    "wrapper, update BOTH files AND roll BOTH anchor literals to the "
    "new fold_anchor() value.  See the ceremony discipline at the top "
    "of this file.");

}  // namespace

int main() {
    // Header — a single block of `#`-prefixed lines that documents the
    // format version, matrix size, anchor, and consumer-side discipline.
    // The `# ` prefix means a future tool can ignore comments while
    // diffing payload lines.
    std::printf("# FIXY-FOUND-052 row_hash cross-build witness\n");
    std::printf("# format-version: 1\n");
    std::printf("# matrix-size:    %zu\n", kEntryCount);
    std::printf("# fold-seed:      0x%016" PRIx64 "\n", kFoldSeed);
    std::printf("# fold-anchor:    0x%016" PRIx64 "\n", kFoldAnchor);
    std::printf("# (a mismatch on diff means a row_hash_contribution "
                "specialization moved — document the ceremony in the "
                "commit message and re-capture this golden.)\n");
    for (const auto& e : kEntries) {
        std::printf("%-22s 0x%016" PRIx64 "\n",
                    e.label,
                    e.value);
    }
    return 0;
}
