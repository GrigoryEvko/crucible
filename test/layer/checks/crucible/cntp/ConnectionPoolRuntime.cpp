// The compile-time checks of crucible/cntp/ConnectionPoolRuntime.h.

#include <crucible/cntp/ConnectionPoolRuntime.h>

namespace crucible::cntp {

static_assert(CtxFitsConnectionPoolMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsConnectionPoolMint<::fixy::BgDrainCtx>);
static_assert(CtxFitsConnectionPoolRuntime<::fixy::BgLoadCtx>);
static_assert(CtxFitsConnectionPoolRuntime<::fixy::TestRunnerCtx>);
static_assert(!CtxFitsConnectionPoolRuntime<::fixy::BgDrainCtx>,
              "a context that owns no Block cannot wait on the gate of the pool");
static_assert(!CtxFitsConnectionPoolRuntime<::fixy::InitLoadCtx>);
static_assert(!CtxFitsConnectionPoolRuntime<::fixy::HotFgCtx>);
static_assert(!std::is_default_constructible_v<ConnectionPool<TransportClass::Tcp, 1, 1>>,
              "mint_connection_pool must be the only door to a pool");

}  // namespace crucible::cntp
