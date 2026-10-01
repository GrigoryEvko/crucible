// The compile-time checks of crucible/cntp/IncastControlRuntime.h.

#include <crucible/cntp/IncastControlRuntime.h>

namespace crucible::cntp {

static_assert(std::is_trivially_copy_constructible_v<IncastCreditGrant>
              && std::is_trivially_destructible_v<IncastCreditGrant>);
static_assert(CtxFitsIncastConfigure<::fixy::ColdInitCtx>);
static_assert(CtxFitsIncastConfigure<::fixy::BgDrainCtx>);
static_assert(!CtxFitsIncastConfigure<::fixy::HotFgCtx>);
static_assert(CtxFitsIncastCredit<::fixy::BgDrainCtx>);
static_assert(!CtxFitsIncastCredit<::fixy::HotFgCtx>);
// Configuring a socket also reads /proc, so only a context whose row holds
// IO and Block may do it.
static_assert(::fixy::fs::CtxFitsFileMint<::fixy::InitLoadCtx, ProcFileReadMode>);
static_assert(!::fixy::fs::CtxFitsFileMint<::fixy::ColdInitCtx, ProcFileReadMode>);

}  // namespace crucible::cntp
