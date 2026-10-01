// The compile-time checks of crucible/topology/Telemetry.h.

#include <crucible/topology/Telemetry.h>

namespace crucible::topology {

static_assert(sizeof(ExternalTelemetryText) == sizeof(std::string_view));
static_assert(sizeof(DeclaredNetdevCounters) == sizeof(NetdevCounters));
static_assert(sizeof(DeclaredQdiscBacklog) == sizeof(QdiscBacklog));
static_assert(sizeof(DeclaredSysctlSnapshot) == sizeof(SysctlSnapshot));
static_assert(std::is_trivially_copyable_v<NetdevCounters>);
static_assert(std::is_trivially_copyable_v<QdiscBacklog>);
static_assert(std::is_trivially_copyable_v<SysctlSnapshot>);
static_assert(std::is_trivially_destructible_v<NicTelemetrySnapshot>);
static_assert(!std::is_default_constructible_v<NicTelemetrySnapshot>, "a snapshot is reached only through its mint");
static_assert(!std::is_default_constructible_v<NicTelemetryHistory<1>>, "a history is reached only through its mint");
static_assert(CtxFitsNicTelemetryMint<::fixy::ColdInitCtx> && !CtxFitsNicTelemetryMint<::fixy::BgDrainCtx>
              && !CtxFitsNicTelemetryMint<::fixy::HotFgCtx>);
static_assert(CtxFitsNicTelemetryRecord<::fixy::BgDrainCtx> && !CtxFitsNicTelemetryRecord<::fixy::ColdInitCtx>
              && !CtxFitsNicTelemetryRecord<::fixy::HotFgCtx>);
static_assert(CtxFitsNicTelemetryRead<::fixy::ColdInitCtx> && CtxFitsNicTelemetryRead<::fixy::BgDrainCtx>
              && !CtxFitsNicTelemetryRead<::fixy::HotFgCtx> && !CtxFitsNicTelemetryRead<::fixy::TestRunnerCtx>);

}  // namespace crucible::topology
