// The compile-time checks of crucible/cntp/PathSwap.h.

#include <crucible/cntp/PathSwap.h>

namespace crucible::cntp {

static_assert(sizeof(PositiveFlowId) == sizeof(std::uint64_t));
static_assert(sizeof(PositivePathId) == sizeof(std::uint64_t));
static_assert(sizeof(PositiveSwapTimeout) == sizeof(std::uint64_t));
static_assert(sizeof(OldPathId) == sizeof(std::uint64_t) && sizeof(NewPathId) == sizeof(std::uint64_t));
// Each parameter of the plan mint has a type of its own, and no raw number
// stands for an id or a count of nanoseconds.
static_assert(!std::is_convertible_v<OldPathId, NewPathId> && !std::is_convertible_v<NewPathId, OldPathId>);
static_assert(!std::is_convertible_v<PositivePathId, PositiveFlowId>
              && !std::is_convertible_v<PositiveFlowId, PositivePathId>);
static_assert(!std::is_convertible_v<std::uint64_t, FlowId> && !std::is_convertible_v<std::uint64_t, PathId>);
static_assert(!std::is_convertible_v<std::uint64_t, SwapNanoseconds>);
static_assert(sizeof(DeclaredPathSwapPlan) == sizeof(PathSwapPlan));
static_assert(!std::is_default_constructible_v<PathSwapPlan> && !std::is_aggregate_v<PathSwapPlan>,
              "mint_path_swap_plan must be the only door to a plan");
// A refined member makes the plan not trivially copyable, because no byte
// route may build a refined value.  A copy still costs what a copy of the
// bytes costs.
static_assert(std::is_trivially_copy_constructible_v<PathSwapPlan> && std::is_trivially_destructible_v<PathSwapPlan>);
static_assert(std::is_trivially_copyable_v<PathSwapEvent>);

static_assert(CtxFitsPathSwapMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsPathSwapMint<::fixy::BgDrainCtx>);
static_assert(!CtxFitsPathSwapMint<::fixy::HotFgCtx>);
static_assert(CtxFitsPathSwapTransition<::fixy::BgLoadCtx>);
static_assert(!CtxFitsPathSwapTransition<::fixy::BgDrainCtx>,
              "a transition waits on the gate of the swapper, and a context that owns no Block cannot wait");
static_assert(!CtxFitsPathSwapTransition<::fixy::ColdInitCtx>);
static_assert(!CtxFitsPathSwapTransition<::fixy::TestRunnerCtx>, "a transition is background work");
static_assert(!CtxFitsPathSwapTransition<::fixy::HotFgCtx>);
static_assert(CtxFitsPathSwapRead<::fixy::BgLoadCtx> && CtxFitsPathSwapRead<::fixy::TestRunnerCtx>);
static_assert(!CtxFitsPathSwapRead<::fixy::BgDrainCtx> && !CtxFitsPathSwapRead<::fixy::HotFgCtx>);
static_assert(!std::is_default_constructible_v<PathSwapper<>>, "mint_path_swapper must be the only door to a swapper");

// The state that a step leaves and the state that it enters are two types,
// so a step cannot take its two states in the other order, and no bare state
// stands for either role.
static_assert(!std::is_convertible_v<SwapFromState, SwapToState> && !std::is_convertible_v<SwapToState, SwapFromState>);
static_assert(!std::is_constructible_v<SwapToState, SwapFromState>);
static_assert(!std::is_convertible_v<SwapState, SwapFromState> && !std::is_convertible_v<SwapState, SwapToState>);

}  // namespace crucible::cntp
