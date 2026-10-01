// The compile-time checks of crucible/cntp/Tcam.h.

#include <crucible/cntp/Tcam.h>

namespace crucible::cntp::tcam {

static_assert(sizeof(TcamRuleId) == sizeof(std::uint64_t));
static_assert(sizeof(TcamEntryCount) == sizeof(std::uint32_t));
static_assert(sizeof(TcamPriority) == sizeof(std::uint16_t));
static_assert(sizeof(TcamDscp) == sizeof(std::uint8_t));
static_assert(sizeof(DeclaredTcamFlowRule) == sizeof(TcamFlowRule));
static_assert(::fixy::qtt_consume_tracked || sizeof(OwnedTcamRule) == sizeof(TcamRuleHandle));
static_assert(TcamTableShape<1>);
static_assert(TcamTableShape<kMaxStaticTcamRules>);
static_assert(!TcamTableShape<0>);
static_assert(!TcamTableShape<kMaxStaticTcamRules + 1>);
static_assert(!admit_tcam_entries(0).has_value());
static_assert(!admit_tcam_entries(kMaxStaticTcamRules + 1).has_value());
static_assert(admit_tcam_entries(kMaxStaticTcamRules).value().value() == kMaxStaticTcamRules);
static_assert(CtxFitsTcamMint<::fixy::ColdInitCtx, cog::NicPortTargetCaps>);
static_assert(CtxFitsTcamMint<::fixy::ColdInitCtx, cog::NvSwitchTargetCaps>);
static_assert(!CtxFitsTcamMint<::fixy::BgDrainCtx, cog::NicPortTargetCaps>,
              "a table plan is built at start-up, not on a background drain");
static_assert(!CtxFitsTcamMint<::fixy::TestRunnerCtx, cog::NicPortTargetCaps>,
              "a test context carries no initialization effect");
static_assert(!CtxFitsTcamMint<::fixy::ColdInitCtx, cog::GpuTargetCaps>, "a GPU carries no TCAM");

// A table plan and a rule handle each come from their one door.
static_assert(!std::is_default_constructible_v<TcamTablePlan>);
static_assert(!std::is_default_constructible_v<DeclaredTcamTable>,
              "a default table plan would skip the target and capacity checks");
static_assert(!std::is_default_constructible_v<TcamRuleHandle>);
static_assert(!std::is_constructible_v<TcamRuleHandle, cog::Uuid, TcamRuleId, std::uint32_t, std::uint32_t>);
static_assert(!std::is_copy_constructible_v<TcamRuleHandle>);
static_assert(std::is_nothrow_move_constructible_v<TcamRuleHandle>);

// A refined field keeps a rule, a plan and a handle from being trivially
// copyable, so no byte copy builds one.
static_assert(std::is_trivially_copyable_v<FiveTuple>);
static_assert(!std::is_trivially_copyable_v<TcamFlowAction>);
static_assert(!std::is_trivially_copyable_v<TcamFlowRule>);
static_assert(!std::is_trivially_copyable_v<TcamTablePlan>);
static_assert(std::is_trivially_copy_constructible_v<TcamFlowRule>);

}  // namespace crucible::cntp::tcam
