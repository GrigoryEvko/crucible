// The compile-time checks of crucible/cntp/dataplane/TcEbpf.h.

#include <crucible/cntp/dataplane/TcEbpf.h>

namespace crucible::cntp::dataplane {

static_assert(static_cast<std::uint8_t>(TcAction::Ok) == 0);
static_assert(static_cast<std::uint8_t>(TcAction::Shot) == 2);
static_assert(sizeof(TcIfIndex) == sizeof(std::uint32_t));
static_assert(sizeof(TcDscp) == sizeof(std::uint8_t));
static_assert(sizeof(TcClassId) == sizeof(std::uint32_t));
static_assert(sizeof(TcFlowPriority) == sizeof(std::uint8_t));
static_assert(sizeof(DeclaredTcProgram) == sizeof(TcProgramSpec));
static_assert(sizeof(DeclaredTcFlowClass) == sizeof(TcFlowClass));
static_assert(sizeof(TcFlowKey) == sizeof(std::int32_t));
static_assert(std::is_trivially_copy_constructible_v<TcProgramSpec>);
static_assert(std::is_trivially_destructible_v<TcProgramSpec>);
// A refined field keeps a flow class from being trivially copyable, so no
// byte copy builds one.  The record is the byte form the map holds.
static_assert(!std::is_trivially_copyable_v<TcFlowClass>);
static_assert(std::is_trivially_copy_constructible_v<TcFlowClass>);
static_assert(std::has_unique_object_representations_v<TcFlowKey>);
static_assert(BpfKey<TcFlowKey>);
static_assert(BpfMapElement<TcFlowClassRecord>);
static_assert(!BpfScalar<TcFlowClass>, "a kernel map value holds bytes, not a refined class");
static_assert(sizeof(TcFlowClassRecord) == 8);
static_assert(offsetof(TcFlowClassRecord, classid) == 0 && offsetof(TcFlowClassRecord, dscp) == 4
              && offsetof(TcFlowClassRecord, priority) == 5 && offsetof(TcFlowClassRecord, action) == 6);
static_assert(CtxFitsTcMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsTcMint<::fixy::BgDrainCtx>);
static_assert(!CtxFitsTcMint<::fixy::TestRunnerCtx>);

}  // namespace crucible::cntp::dataplane
