#include <crucible/cntp/dataplane/TcEbpf.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Tags.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <concepts>
#include <type_traits>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace cntp = crucible::cntp;
namespace cog = crucible::cog;
namespace dataplane = crucible::cntp::dataplane;

namespace {

// The value of a call that succeeds in this test.
template <class Value, class Error>
Value take(::fixy::Result<Value, Error> result) {
    return std::move(result).expect("the call succeeds in this test");
}

// The error of a call that fails in this test.
template <class Value, class Error>
Error error_of(::fixy::Result<Value, Error> result) {
    return std::move(result).err().expect("the call fails in this test");
}

[[nodiscard]] cntp::NicInterfaceName iface() {
    return cntp::NicInterfaceName::from("eth0").expect("eth0 is an interface name");
}

[[nodiscard]] cog::CogIdentity nic_identity() {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x193, 0x1};
    id.level = cog::CogLevel::L0_Atomic;
    id.kind = cog::CogKind::NicPort;
    return id;
}

[[nodiscard]] cog::CogIdentity gpu_identity() {
    cog::CogIdentity id = nic_identity();
    id.kind = cog::CogKind::Gpu;
    return id;
}

[[nodiscard]] cog::NicPortTargetCaps tc_caps() {
    cog::NicPortTargetCaps caps{};
    caps.features.set(cog::NicFeature::TcEbpf);
    return caps;
}

// A TCP flow from 10.0.0.1:40000 to 10.0.0.2:443, and the same flow to the
// port 8443.
constexpr dataplane::NetFlowKey kHttpsFlow{
    .src_ip4 = 0x0a000001u,
    .dst_ip4 = 0x0a000002u,
    .src_port = 40000,
    .dst_port = 443,
    .proto = 6,
};
constexpr dataplane::NetFlowKey kAlternateFlow{
    .src_ip4 = 0x0a000001u,
    .dst_ip4 = 0x0a000002u,
    .src_port = 40000,
    .dst_port = 8443,
    .proto = 6,
};

void test_names_and_admission() {
    CRUCIBLE_TEST_ASSERT(kTest, dataplane::tc_action_name(dataplane::TcAction::Shot) == "Shot");
    CRUCIBLE_TEST_ASSERT(kTest, dataplane::tc_attach_point_name(dataplane::TcAttachPoint::Egress) == "Egress");
    CRUCIBLE_TEST_ASSERT(kTest, dataplane::tc_program_kind_name(dataplane::TcProgramKind::PacingGate) == "PacingGate");
    CRUCIBLE_TEST_ASSERT(kTest, dataplane::tc_error_name(dataplane::TcError::MissingTcEbpf) == "MissingTcEbpf");
    CRUCIBLE_TEST_ASSERT(kTest, dataplane::tc_action_name(static_cast<dataplane::TcAction>(5)) == "<unknown TcAction>");

    CRUCIBLE_TEST_ASSERT(kTest, take(dataplane::admit_tc_dscp(63)).value() == 63u);
    CRUCIBLE_TEST_ASSERT(kTest, error_of(dataplane::admit_tc_dscp(64)) == dataplane::TcError::InvalidDscp);
    CRUCIBLE_TEST_ASSERT(kTest, take(dataplane::admit_tc_classid(1)).value() == 1u);
    CRUCIBLE_TEST_ASSERT(kTest, error_of(dataplane::admit_tc_classid(0)) == dataplane::TcError::InvalidClassId);
    CRUCIBLE_TEST_ASSERT(kTest, take(dataplane::admit_tc_flow_priority(7)).value() == 7u);
    CRUCIBLE_TEST_ASSERT(kTest,
                         error_of(dataplane::admit_tc_flow_priority(8)) == dataplane::TcError::InvalidFlowPriority);

    ::fixy::check::pass(kTest, "  test_names_and_admission: PASSED\n");
}

void test_program_caps() {
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    dataplane::TcIfIndex const ifindex = take(dataplane::admit_xdp_ifindex(11));

    auto const program = dataplane::mint_tc_program(init, iface(), ifindex, dataplane::TcAttachPoint::Egress,
                                                    dataplane::TcProgramKind::EgressMark);
    static_assert(std::same_as<decltype(program)::tag_type, ::fixy::tags::source::TcEbpf>);
    CRUCIBLE_TEST_ASSERT(kTest, program.value().required_features.test(cog::NicFeature::TcEbpf));
    CRUCIBLE_TEST_ASSERT(kTest, program.value().is_direct_action);
    CRUCIBLE_TEST_ASSERT(kTest, dataplane::tc_admit_nic(nic_identity(), tc_caps(), program).is_ok());

    cog::NicPortTargetCaps const no_tc{};
    CRUCIBLE_TEST_ASSERT(kTest, error_of(dataplane::tc_admit_nic(nic_identity(), no_tc, program))
                                    == dataplane::TcError::MissingTcEbpf);
    CRUCIBLE_TEST_ASSERT(kTest, error_of(dataplane::tc_admit_nic(gpu_identity(), tc_caps(), program))
                                    == dataplane::TcError::WrongCogKind);
    CRUCIBLE_TEST_ASSERT(kTest, error_of(dataplane::attach_tc_program(program))
                                    == dataplane::TcError::PrivilegedAttachDeferred);

    ::fixy::check::pass(kTest, "  test_program_caps: PASSED\n");
}

// The map is keyed by the flow key of the kernel map, and it stores the byte
// record that the kernel program reads.  A lookup builds the checked class
// from the record again, with each field intact.
void test_flow_class_map() {
    auto const cls =
        dataplane::mint_tc_flow_class(take(dataplane::admit_tc_dscp(46)), take(dataplane::admit_tc_classid(0x10001)),
                                      take(dataplane::admit_tc_flow_priority(5)), dataplane::TcAction::Shot);
    static_assert(std::same_as<decltype(cls)::tag_type, ::fixy::tags::source::TcEbpf>);

    dataplane::TcFlowClassMap<2> map{};
    CRUCIBLE_TEST_ASSERT(kTest, map.update(kHttpsFlow, cls).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, map.size() == 1u);

    dataplane::DeclaredTcFlowClass const found = map.lookup(kHttpsFlow).expect("the map holds the flow");
    CRUCIBLE_TEST_ASSERT(kTest, found.value().dscp.value() == 46u);
    CRUCIBLE_TEST_ASSERT(kTest, found.value().classid.value() == 0x10001u);
    CRUCIBLE_TEST_ASSERT(kTest, found.value().priority.value() == 5u);
    CRUCIBLE_TEST_ASSERT(kTest, found.value().action == dataplane::TcAction::Shot);

    CRUCIBLE_TEST_ASSERT(kTest, map.lookup(kAlternateFlow).is_none());

    // The pad bytes are part of the key, so a key with a pad byte that is
    // not zero is a different key.
    dataplane::NetFlowKey padded = kHttpsFlow;
    padded.pad = ::fixy::FixedArray<std::uint8_t, 3>{std::uint8_t{0}, std::uint8_t{0}, std::uint8_t{1}};
    CRUCIBLE_TEST_ASSERT(kTest, !(padded == kHttpsFlow));
    CRUCIBLE_TEST_ASSERT(kTest, map.lookup(padded).is_none());

    CRUCIBLE_TEST_ASSERT(kTest, map.erase(kHttpsFlow).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, map.lookup(kHttpsFlow).is_none());
    CRUCIBLE_TEST_ASSERT(kTest, map.size() == 0u);

    ::fixy::check::pass(kTest, "  test_flow_class_map: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(dataplane::DeclaredTcProgram) == sizeof(dataplane::TcProgramSpec));
    static_assert(sizeof(dataplane::DeclaredTcFlowClass) == sizeof(dataplane::TcFlowClass));
    static_assert(dataplane::CtxFitsTcMint<::fixy::ColdInitCtx>);
    static_assert(!dataplane::CtxFitsTcMint<::fixy::BgDrainCtx>);
    static_assert(sizeof(dataplane::TcFlowClassRecord) == 8, "the record is the value of the kernel map");
    static_assert(!std::copy_constructible<dataplane::TcFlowClassMap<2>>);

    ::fixy::report(::fixy::Sink::Out, "test_cntp_dataplane_tc_ebpf:\n");
    test_names_and_admission();
    test_program_caps();
    test_flow_class_map();
    ::fixy::check::pass(kTest, "test_cntp_dataplane_tc_ebpf: all PASSED\n");
    return 0;
}
