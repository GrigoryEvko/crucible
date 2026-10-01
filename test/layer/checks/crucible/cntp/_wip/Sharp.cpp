// The compile-time checks of crucible/cntp/_wip/Sharp.h.

#include <crucible/cntp/_wip/Sharp.h>

namespace crucible::cntp::_wip::sharp {

static_assert(sizeof(SharpParticipantCount) == sizeof(std::uint16_t));
static_assert(sizeof(DeclaredSharpFabricPlan) == sizeof(SharpFabricPlan));
static_assert(sizeof(DeclaredSharpDispatch) == sizeof(SharpDispatchResult));
static_assert(::fixy::qtt_consume_tracked || sizeof(SharpContext) == sizeof(SharpContextHandle));
static_assert(CtxFitsSharpMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsSharpMint<::fixy::BgDrainCtx>);
static_assert(CtxFitsSharpDispatch<::fixy::BgDrainCtx>);
static_assert(!CtxFitsSharpDispatch<::fixy::ColdInitCtx>);
static_assert(std::is_trivially_copyable_v<SharpDispatchResult>);
// A refined member makes a plan not trivially copyable, because no byte
// route may build a refined value.  A copy still costs what copying the
// bytes costs.
static_assert(std::is_trivially_copy_constructible_v<SharpFabricPlan>
              && std::is_trivially_destructible_v<SharpFabricPlan>);
// No declared plan exists before its count, and no context handle exists
// outside mint_sharp_context or beside the one it names.
static_assert(!std::is_default_constructible_v<DeclaredSharpFabricPlan>);
static_assert(!std::is_constructible_v<SharpContextHandle, cog::Uuid, SharpParticipantCount, bool, bool>);
static_assert(!std::is_copy_constructible_v<SharpContextHandle>
              && std::is_nothrow_move_constructible_v<SharpContextHandle>);

}  // namespace crucible::cntp::_wip::sharp
