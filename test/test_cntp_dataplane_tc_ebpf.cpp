#include <crucible/cntp/dataplane/TcEbpf.h>

#include "test_assert.h"

#include <cstdio>
#include <string_view>
#include <type_traits>

namespace cntp = crucible::cntp;
namespace cog = crucible::cog;
namespace effects = crucible::effects;
namespace dataplane = crucible::cntp::dataplane;
namespace saf = crucible::safety;

namespace {

[[nodiscard]] cntp::NicInterfaceName iface() {
    auto parsed = cntp::NicInterfaceName::from("eth0");
    assert(parsed.has_value());
    return *parsed;
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

void test_names_and_admission() {
    assert(dataplane::tc_action_name(dataplane::TcAction::Shot) == std::string_view{"Shot"});
    assert(dataplane::tc_attach_point_name(dataplane::TcAttachPoint::Egress) ==
           std::string_view{"Egress"});
    assert(dataplane::tc_program_kind_name(dataplane::TcProgramKind::PacingGate) ==
           std::string_view{"PacingGate"});
    assert(dataplane::tc_error_name(dataplane::TcError::MissingTcEbpf) ==
           std::string_view{"MissingTcEbpf"});

    assert(dataplane::admit_tc_dscp(63).has_value());
    assert(!dataplane::admit_tc_dscp(64).has_value());
    assert(dataplane::admit_tc_classid(1).has_value());
    assert(!dataplane::admit_tc_classid(0).has_value());
    assert(dataplane::admit_tc_flow_priority(7).has_value());
    assert(!dataplane::admit_tc_flow_priority(8).has_value());

    std::printf("  test_names_and_admission: PASSED\n");
}

void test_program_caps() {
    effects::ColdInitCtx init{};
    auto ifindex = dataplane::admit_xdp_ifindex(11);
    assert(ifindex.has_value());

    auto program = dataplane::mint_tc_program(init, iface(), *ifindex,
                                       dataplane::TcAttachPoint::Egress,
                                       dataplane::TcProgramKind::EgressMark);
    static_assert(std::same_as<decltype(program)::tag_type,
                               saf::source::TcEbpf>);
    assert(program.value().required_features.test(cog::NicFeature::TcEbpf));
    assert(dataplane::tc_admit_nic(nic_identity(), tc_caps(), program).has_value());

    cog::NicPortTargetCaps no_tc{};
    auto missing = dataplane::tc_admit_nic(nic_identity(), no_tc, program);
    assert(!missing.has_value());
    assert(missing.error() == dataplane::TcError::MissingTcEbpf);

    auto wrong_cog = dataplane::tc_admit_nic(gpu_identity(), tc_caps(), program);
    assert(!wrong_cog.has_value());
    assert(wrong_cog.error() == dataplane::TcError::WrongCogKind);

    auto attached = dataplane::attach_tc_program(program);
    assert(!attached.has_value());
    assert(attached.error() == dataplane::TcError::PrivilegedAttachDeferred);

    std::printf("  test_program_caps: PASSED\n");
}

void test_flow_class_map() {
    auto dscp = dataplane::admit_tc_dscp(46);
    auto classid = dataplane::admit_tc_classid(0x10001);
    auto priority = dataplane::admit_tc_flow_priority(5);
    assert(dscp.has_value());
    assert(classid.has_value());
    assert(priority.has_value());

    auto cls = dataplane::mint_tc_flow_class(*dscp, *classid, *priority,
                                      dataplane::TcAction::Ok);
    static_assert(std::same_as<decltype(cls)::tag_type, saf::source::TcEbpf>);

    dataplane::TcFlowClassMap<2> map{};
    auto key = dataplane::tc_flow_key(cntp::SocketFd{7});
    assert(map.update(key, cls).has_value());
    auto found = map.lookup(key);
    assert(found.has_value());
    assert(found->value().dscp.value() == 46);
    assert(found->value().classid.value() == 0x10001u);

    std::printf("  test_flow_class_map: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(dataplane::DeclaredTcProgram) == sizeof(dataplane::TcProgramSpec));
    static_assert(sizeof(dataplane::DeclaredTcFlowClass) == sizeof(dataplane::TcFlowClass));
    static_assert(dataplane::CtxFitsTcMint<effects::ColdInitCtx>);
    static_assert(!dataplane::CtxFitsTcMint<effects::BgDrainCtx>);
    static_assert(!std::copy_constructible<dataplane::TcFlowClassMap<2>>);

    std::printf("test_cntp_dataplane_tc_ebpf:\n");
    test_names_and_admission();
    test_program_caps();
    test_flow_class_map();
    std::printf("test_cntp_dataplane_tc_ebpf: all PASSED\n");
    return 0;
}
