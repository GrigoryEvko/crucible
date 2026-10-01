// The compile-time checks of crucible/perf/SchedSwitch.h.

#include <crucible/perf/SchedSwitch.h>

namespace crucible::perf {

static_assert(sizeof(TimelineSchedEvent) == 32, "TimelineSchedEvent must be 32 B (with 8 B trailing pad) to "
                                                "match the kernel-side event struct — wire contract with the "
                                                "BPF_F_MMAPABLE map.  32 divides 64 evenly, so events[N] never "
                                                "spans two cache lines.");

static_assert(sizeof(TimelineHeader) == 64, "TimelineHeader must be exactly one cache line so the events "
                                            "array starts at offset 64 — pinned by BPF program layout");

// Block is the atom that decides this gate.  ColdInitCtx and
// BgCompileCtx both carry Alloc and IO, and the gate rejects both for
// the same missing atom.  The gate reads the wait, not the capability
// source.
static_assert(!CtxFitsSchedSwitchMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsSchedSwitchMint<::fixy::BgCompileCtx>);
static_assert(!CtxFitsSchedSwitchMint<::fixy::BgDrainCtx>);
static_assert(!CtxFitsSchedSwitchMint<::fixy::HotFgCtx>);
static_assert(CtxFitsSchedSwitchMint<::fixy::InitLoadCtx>);
static_assert(CtxFitsSchedSwitchMint<::fixy::BgLoadCtx>);
static_assert(CtxFitsSchedSwitchMint<::fixy::TestRunnerCtx>);

// The two assertions below hold for a capability source rather than for
// one named context, so a new alias on either side cannot evade them.
static_assert(::foundation::effects::Subrow<sched_switch_required_row,
                                            ::foundation::effects::cap_permitted_row_t<::foundation::effects::Init>>,
              "The initialization capability must permit every atom this gate demands, or no startup "
              "context could reach this mint.");
static_assert(::foundation::effects::Subrow<sched_switch_required_row,
                                            ::foundation::effects::cap_permitted_row_t<::foundation::effects::Bg>>,
              "The background capability must permit every atom this gate demands, or no production "
              "context could reach this mint.");

}  // namespace crucible::perf
