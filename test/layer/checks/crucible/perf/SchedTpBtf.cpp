// The compile-time checks of crucible/perf/SchedTpBtf.h.

#include <crucible/perf/SchedTpBtf.h>

namespace crucible::perf {

// Block is the atom that decides this gate.  ColdInitCtx and
// BgCompileCtx both carry Alloc and IO, and the gate rejects both for
// the same missing atom.  The gate reads the wait, not the capability
// source.
static_assert(!CtxFitsSchedTpBtfMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsSchedTpBtfMint<::fixy::BgCompileCtx>);
static_assert(!CtxFitsSchedTpBtfMint<::fixy::BgDrainCtx>);
static_assert(!CtxFitsSchedTpBtfMint<::fixy::HotFgCtx>);
static_assert(CtxFitsSchedTpBtfMint<::fixy::InitLoadCtx>);
static_assert(CtxFitsSchedTpBtfMint<::fixy::BgLoadCtx>);
static_assert(CtxFitsSchedTpBtfMint<::fixy::TestRunnerCtx>);

// The two assertions below hold for a capability source rather than for
// one named context, so a new alias on either side cannot evade them.
static_assert(::foundation::effects::Subrow<sched_tp_btf_required_row,
                                            ::foundation::effects::cap_permitted_row_t<::foundation::effects::Init>>,
              "The initialization capability must permit every atom this gate demands, or no startup "
              "context could reach this mint.");
static_assert(::foundation::effects::Subrow<sched_tp_btf_required_row,
                                            ::foundation::effects::cap_permitted_row_t<::foundation::effects::Bg>>,
              "The background capability must permit every atom this gate demands, or no production "
              "context could reach this mint.");

}  // namespace crucible::perf
