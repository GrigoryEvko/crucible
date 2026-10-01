// The compile-time checks of crucible/perf/PmuSample.h.

#include <crucible/perf/PmuSample.h>

namespace crucible::perf {

static_assert(sizeof(PmuSampleEvent) == 32, "PmuSampleEvent must be 32 B = ip(8) + tid(4) + event_type(1) + "
                                            "_pad[3] + ts_ns(8) + _pad8(8); the trailing pad makes 32 divide "
                                            "64 evenly so each slot is cache-line-coresident");

static_assert(sizeof(PmuSampleHeader) == 64, "PmuSampleHeader must be exactly one cache line so the events "
                                             "array starts at offset 64");

// Block is the atom that decides this gate.  ColdInitCtx and
// BgCompileCtx both carry Alloc and IO, and the gate rejects both for
// the same missing atom.  The gate reads the wait, not the capability
// source.
static_assert(!CtxFitsPmuSampleMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsPmuSampleMint<::fixy::BgCompileCtx>);
static_assert(!CtxFitsPmuSampleMint<::fixy::BgDrainCtx>);
static_assert(!CtxFitsPmuSampleMint<::fixy::HotFgCtx>);
static_assert(CtxFitsPmuSampleMint<::fixy::InitLoadCtx>);
static_assert(CtxFitsPmuSampleMint<::fixy::BgLoadCtx>);
static_assert(CtxFitsPmuSampleMint<::fixy::TestRunnerCtx>);

// The two assertions below hold for a capability source rather than for
// one named context, so a new alias on either side cannot evade them.
static_assert(::foundation::effects::Subrow<pmu_sample_required_row,
                                            ::foundation::effects::cap_permitted_row_t<::foundation::effects::Init>>,
              "The initialization capability must permit every atom this gate demands, or no startup "
              "context could reach this mint.");
static_assert(::foundation::effects::Subrow<pmu_sample_required_row,
                                            ::foundation::effects::cap_permitted_row_t<::foundation::effects::Bg>>,
              "The background capability must permit every atom this gate demands, or no production "
              "context could reach this mint.");

}  // namespace crucible::perf
