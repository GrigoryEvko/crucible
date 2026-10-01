// The compile-time checks of crucible/perf/SyscallTpBtf.h.

#include <crucible/perf/SyscallTpBtf.h>

namespace crucible::perf {

// Block is the atom that decides this gate.  ColdInitCtx and
// BgCompileCtx both carry Alloc and IO, and the gate rejects both for
// the same missing atom.  The gate reads the wait, not the capability
// source.
static_assert(!CtxFitsSyscallTpBtfMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsSyscallTpBtfMint<::fixy::BgCompileCtx>);
static_assert(!CtxFitsSyscallTpBtfMint<::fixy::BgDrainCtx>);
static_assert(!CtxFitsSyscallTpBtfMint<::fixy::HotFgCtx>);
static_assert(CtxFitsSyscallTpBtfMint<::fixy::InitLoadCtx>);
static_assert(CtxFitsSyscallTpBtfMint<::fixy::BgLoadCtx>);
static_assert(CtxFitsSyscallTpBtfMint<::fixy::TestRunnerCtx>);

// The two assertions below hold for a capability source rather than for
// one named context, so a new alias on either side cannot evade them.
static_assert(::foundation::effects::Subrow<syscall_tp_btf_required_row,
                                            ::foundation::effects::cap_permitted_row_t<::foundation::effects::Init>>,
              "The initialization capability must permit every atom this gate demands, or no startup "
              "context could reach this mint.");
static_assert(::foundation::effects::Subrow<syscall_tp_btf_required_row,
                                            ::foundation::effects::cap_permitted_row_t<::foundation::effects::Bg>>,
              "The background capability must permit every atom this gate demands, or no production "
              "context could reach this mint.");

}  // namespace crucible::perf
