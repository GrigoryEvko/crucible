// The compile-time checks of crucible/perf/LockContention.h.

#include <crucible/perf/LockContention.h>

namespace crucible::perf {

static_assert(sizeof(TimelineLockEvent) == 32, "TimelineLockEvent must be 32 B (futex_addr 8 + wait_ns 8 + "
                                               "tid 4 + _pad 4 + ts_ns 8) to match the kernel-side event "
                                               "struct — wire contract with the BPF_F_MMAPABLE map.  32 "
                                               "divides 64 evenly, so events[N] never spans two cache lines.");

// Block is the atom that decides this gate.  ColdInitCtx and
// BgCompileCtx both carry Alloc and IO, and the gate rejects both for
// the same missing atom.  The gate reads the wait, not the capability
// source.
static_assert(!CtxFitsLockContentionMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsLockContentionMint<::fixy::BgCompileCtx>);
static_assert(!CtxFitsLockContentionMint<::fixy::BgDrainCtx>);
static_assert(!CtxFitsLockContentionMint<::fixy::HotFgCtx>);
static_assert(CtxFitsLockContentionMint<::fixy::InitLoadCtx>);
static_assert(CtxFitsLockContentionMint<::fixy::BgLoadCtx>);
static_assert(CtxFitsLockContentionMint<::fixy::TestRunnerCtx>);

// The two assertions below hold for a capability source rather than for
// one named context, so a new alias on either side cannot evade them.
static_assert(::foundation::effects::Subrow<lock_contention_required_row,
                                            ::foundation::effects::cap_permitted_row_t<::foundation::effects::Init>>,
              "The initialization capability must permit every atom this gate demands, or no startup "
              "context could reach this mint.");
static_assert(::foundation::effects::Subrow<lock_contention_required_row,
                                            ::foundation::effects::cap_permitted_row_t<::foundation::effects::Bg>>,
              "The background capability must permit every atom this gate demands, or no production "
              "context could reach this mint.");

}  // namespace crucible::perf
