// The compile-time checks of crucible/perf/SenseHubV2.h.

#include <crucible/perf/SenseHubV2.h>

namespace crucible::perf {

namespace v2 {

static_assert(static_cast<uint32_t>(Idx::MAP_FULL_DROPS) < NUM_COUNTERS,
              "Last basic Idx must fit within NUM_COUNTERS — wire contract assertion");

static_assert(sizeof(sense_meta) == 64, "sense_meta must be exactly one cache line — wire contract");

}  // namespace v2

struct DummyStateV2 {};
static_assert(sizeof(SenseHubV2) == sizeof(std::unique_ptr<DummyStateV2>),
              "SenseHubV2 must be EBO-equivalent to unique_ptr<State> — regression "
              "indicates a non-EBO field crept in (likely a missing [[no_unique_address]] "
              "or a polymorphic vptr).");

// Block is the atom that decides this gate.  ColdInitCtx and
// BgCompileCtx both carry Alloc and IO, and the gate rejects both for
// the same missing atom.  The gate reads the wait, not the capability
// source.
static_assert(!CtxFitsSenseHubV2Mint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsSenseHubV2Mint<::fixy::BgCompileCtx>);
static_assert(!CtxFitsSenseHubV2Mint<::fixy::BgDrainCtx>);
static_assert(!CtxFitsSenseHubV2Mint<::fixy::HotFgCtx>);
static_assert(CtxFitsSenseHubV2Mint<::fixy::InitLoadCtx>);
static_assert(CtxFitsSenseHubV2Mint<::fixy::BgLoadCtx>);
static_assert(CtxFitsSenseHubV2Mint<::fixy::TestRunnerCtx>);

// The two assertions below hold for a capability source rather than for
// one named context, so a new alias on either side cannot evade them.
static_assert(::foundation::effects::Subrow<sense_hub_v2_required_row,
                                            ::foundation::effects::cap_permitted_row_t<::foundation::effects::Init>>,
              "The initialization capability must permit every atom this gate demands, or no startup "
              "context could reach this mint.");
static_assert(::foundation::effects::Subrow<sense_hub_v2_required_row,
                                            ::foundation::effects::cap_permitted_row_t<::foundation::effects::Bg>>,
              "The background capability must permit every atom this gate demands, or no production "
              "context could reach this mint.");

}  // namespace crucible::perf
