#include <crucible/rt/TcEbpf.h>

#include "test_assert.h"

#include <cstdio>
#include <string_view>
#include <type_traits>

namespace cntp = crucible::cntp;
namespace cog = crucible::cog;
namespace effects = crucible::effects;
namespace rt = crucible::rt;
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
    assert(rt::tc_action_name(rt::TcAction::Shot) == std::string_view{"Shot"});
    assert(rt::tc_attach_point_name(rt::TcAttachPoint::Egress) ==
           std::string_view{"Egress"});
    assert(rt::tc_program_kind_name(rt::TcProgramKind::PacingGate) ==
           std::string_view{"PacingGate"});
    assert(rt::tc_error_name(rt::TcError::MissingTcEbpf) ==
           std::string_view{"MissingTcEbpf"});

    assert(rt::admit_tc_dscp(63).has_value());
    assert(!rt::admit_tc_dscp(64).has_value());
    assert(rt::admit_tc_classid(1).has_value());
    assert(!rt::admit_tc_classid(0).has_value());
    assert(rt::admit_tc_flow_priority(7).has_value());
    assert(!rt::admit_tc_flow_priority(8).has_value());

    std::printf("  test_names_and_admission: PASSED\n");
}

void test_program_caps() {
    effects::ColdInitCtx init{};
    auto ifindex = rt::admit_xdp_ifindex(11);
    assert(ifindex.has_value());

    auto program = rt::mint_tc_program(init, iface(), *ifindex,
                                       rt::TcAttachPoint::Egress,
                                       rt::TcProgramKind::EgressMark);
    static_assert(std::same_as<decltype(program)::tag_type,
                               saf::source::TcEbpf>);
    assert(program.value().required_features.test(cog::NicFeature::TcEbpf));
    assert(rt::tc_admit_nic(nic_identity(), tc_caps(), program).has_value());

    cog::NicPortTargetCaps no_tc{};
    auto missing = rt::tc_admit_nic(nic_identity(), no_tc, program);
    assert(!missing.has_value());
    assert(missing.error() == rt::TcError::MissingTcEbpf);

    auto wrong_cog = rt::tc_admit_nic(gpu_identity(), tc_caps(), program);
    assert(!wrong_cog.has_value());
    assert(wrong_cog.error() == rt::TcError::WrongCogKind);

    auto attached = rt::attach_tc_program(program);
    assert(!attached.has_value());
    assert(attached.error() == rt::TcError::PrivilegedAttachDeferred);

    std::printf("  test_program_caps: PASSED\n");
}

void test_flow_class_map() {
    auto dscp = rt::admit_tc_dscp(46);
    auto classid = rt::admit_tc_classid(0x10001);
    auto priority = rt::admit_tc_flow_priority(5);
    assert(dscp.has_value());
    assert(classid.has_value());
    assert(priority.has_value());

    auto cls = rt::mint_tc_flow_class(*dscp, *classid, *priority,
                                      rt::TcAction::Ok);
    static_assert(std::same_as<decltype(cls)::tag_type, saf::source::TcEbpf>);

    rt::TcFlowClassMap<2> map{};
    auto key = rt::tc_flow_key(cntp::SocketFd{7});
    assert(map.update(key, cls).has_value());
    auto found = map.lookup(key);
    assert(found.has_value());
    assert(found->value().dscp.value() == 46);
    assert(found->value().classid.value() == 0x10001u);

    std::printf("  test_flow_class_map: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(rt::DeclaredTcProgram) == sizeof(rt::TcProgramSpec));
    static_assert(sizeof(rt::DeclaredTcFlowClass) == sizeof(rt::TcFlowClass));
    static_assert(rt::CtxFitsTcMint<effects::ColdInitCtx>);
    static_assert(!rt::CtxFitsTcMint<effects::BgDrainCtx>);
    static_assert(!std::copy_constructible<rt::TcFlowClassMap<2>>);

    std::printf("test_rt_tc_ebpf:\n");
    test_names_and_admission();
    test_program_caps();
    test_flow_class_map();
    std::printf("test_rt_tc_ebpf: all PASSED\n");
    return 0;
}
