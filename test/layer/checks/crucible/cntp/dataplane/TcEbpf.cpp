// The compile-time checks of crucible/cntp/dataplane/TcEbpf.h.

#include <crucible/cntp/dataplane/TcEbpf.h>

#include <cstddef>
#include <type_traits>

namespace crucible::cntp::dataplane {

static_assert(static_cast<std::uint8_t>(TcAction::Ok) == 0);
static_assert(static_cast<std::uint8_t>(TcAction::Shot) == 2);
static_assert(static_cast<std::uint8_t>(TcAction::Trap) == 8);
static_assert(sizeof(TcIfIndex) == sizeof(std::uint32_t));
static_assert(sizeof(TcDscp) == sizeof(std::uint8_t));
static_assert(sizeof(TcClassId) == sizeof(std::uint32_t));
static_assert(sizeof(TcFlowPriority) == sizeof(std::uint8_t));
static_assert(sizeof(DeclaredTcProgram) == sizeof(TcProgramSpec));
static_assert(sizeof(DeclaredTcFlowClass) == sizeof(TcFlowClass));
static_assert(std::is_trivially_copy_constructible_v<TcProgramSpec>);
static_assert(std::is_trivially_destructible_v<TcProgramSpec>);
// A refined field keeps a flow class from being trivially copyable, so no
// byte copy builds one.  The record is the byte form that the map holds.
static_assert(!std::is_trivially_copyable_v<TcFlowClass>);
static_assert(std::is_trivially_copy_constructible_v<TcFlowClass>);
static_assert(!BpfScalar<TcFlowClass>, "a kernel map value holds bytes, not a refined class");
// struct crucible_tc_flow_class of bpf/dp_tc_egress_classify.bpf.c:
// 4 + 1 + 1 + 1 + 1 bytes, aligned to 4.
static_assert(BpfMapElement<TcFlowClassRecord>);
static_assert(sizeof(TcFlowClassRecord) == 8);
static_assert(alignof(TcFlowClassRecord) == 4);
static_assert(offsetof(TcFlowClassRecord, classid) == 0);
static_assert(offsetof(TcFlowClassRecord, dscp) == 4);
static_assert(offsetof(TcFlowClassRecord, priority) == 5);
static_assert(offsetof(TcFlowClassRecord, action) == 6);
static_assert(offsetof(TcFlowClassRecord, pad) == 7);
static_assert(sizeof(TcAction) == 1);
// The key of the flow map is the flow key of bpf/net_common.h.
static_assert(BpfKey<NetFlowKey>);
static_assert(BpfMapElement<NetFlowKey>);
static_assert(CtxFitsTcMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsTcMint<::fixy::BgDrainCtx>);
static_assert(!CtxFitsTcMint<::fixy::TestRunnerCtx>);
static_assert(admit_tc_dscp(kMaxTcDscp).is_ok());
static_assert(admit_tc_dscp(kMaxTcDscp + 1).is_err());
static_assert(admit_tc_flow_priority(kMaxTcFlowPriority).is_ok());
static_assert(admit_tc_flow_priority(kMaxTcFlowPriority + 1).is_err());

}  // namespace crucible::cntp::dataplane
