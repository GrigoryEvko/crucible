// The compile-time checks of crucible/perf/SyscallLatency.h.

#include <crucible/perf/SyscallLatency.h>

namespace crucible::perf {

static_assert(sizeof(TimelineSyscallEvent) == 32, "TimelineSyscallEvent must be 32 B (duration_ns 8 + tid 4 + "
                                                  "syscall_nr 4 + ts_ns 8 + _pad 8) to match the kernel-side "
                                                  "event struct — wire contract with the BPF_F_MMAPABLE map.  32 "
                                                  "divides 64 evenly, so events[N] never spans two cache lines.");

// Block is the atom that decides this gate.  ColdInitCtx and
// BgCompileCtx both carry Alloc and IO, and the gate rejects both for
// the same missing atom.  The gate reads the wait, not the capability
// source.
static_assert(!CtxFitsSyscallLatencyMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsSyscallLatencyMint<::fixy::BgCompileCtx>);
static_assert(!CtxFitsSyscallLatencyMint<::fixy::BgDrainCtx>);
static_assert(!CtxFitsSyscallLatencyMint<::fixy::HotFgCtx>);
static_assert(CtxFitsSyscallLatencyMint<::fixy::InitLoadCtx>);
static_assert(CtxFitsSyscallLatencyMint<::fixy::BgLoadCtx>);
static_assert(CtxFitsSyscallLatencyMint<::fixy::TestRunnerCtx>);

// The two assertions below hold for a capability source rather than for
// one named context, so a new alias on either side cannot evade them.
static_assert(::foundation::effects::Subrow<syscall_latency_required_row,
                                            ::foundation::effects::cap_permitted_row_t<::foundation::effects::Init>>,
              "The initialization capability must permit every atom this gate demands, or no startup "
              "context could reach this mint.");
static_assert(::foundation::effects::Subrow<syscall_latency_required_row,
                                            ::foundation::effects::cap_permitted_row_t<::foundation::effects::Bg>>,
              "The background capability must permit every atom this gate demands, or no production "
              "context could reach this mint.");

}  // namespace crucible::perf
