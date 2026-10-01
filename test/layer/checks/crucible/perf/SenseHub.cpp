// The compile-time checks of crucible/perf/SenseHub.h.

#include <crucible/perf/SenseHub.h>

namespace crucible::perf {

static_assert(sizeof(Snapshot) == NUM_COUNTERS * sizeof(uint64_t),
              "Snapshot must be a tight 96*u64 = 768B = 12 cache lines; "
              "mmap contract with BPF_F_MMAPABLE array map depends on it");

// Block is the atom that decides this gate.  ColdInitCtx and
// BgCompileCtx both carry Alloc and IO, and the gate rejects both for
// the same missing atom.  The gate reads the wait, not the capability
// source.
static_assert(!CtxFitsSenseHubMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsSenseHubMint<::fixy::BgCompileCtx>);
static_assert(!CtxFitsSenseHubMint<::fixy::BgDrainCtx>);
static_assert(!CtxFitsSenseHubMint<::fixy::HotFgCtx>);
static_assert(CtxFitsSenseHubMint<::fixy::InitLoadCtx>);
static_assert(CtxFitsSenseHubMint<::fixy::BgLoadCtx>);
static_assert(CtxFitsSenseHubMint<::fixy::TestRunnerCtx>);

// The two assertions below hold for a capability source rather than for
// one named context, so a new alias on either side cannot evade them.
static_assert(::foundation::effects::Subrow<sense_hub_required_row,
                                            ::foundation::effects::cap_permitted_row_t<::foundation::effects::Init>>,
              "The initialization capability must permit every atom this gate demands, or no startup "
              "context could reach this mint.");
static_assert(::foundation::effects::Subrow<sense_hub_required_row,
                                            ::foundation::effects::cap_permitted_row_t<::foundation::effects::Bg>>,
              "The background capability must permit every atom this gate demands, or no production "
              "context could reach this mint.");

}  // namespace crucible::perf
