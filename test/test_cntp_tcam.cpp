#include <crucible/cntp/Tcam.h>

#include <cassert>
#include <concepts>
#include <cstdio>
#include <limits>
#include <string_view>
#include <type_traits>

namespace cog = crucible::cog;
namespace eff = crucible::effects;
namespace saf = crucible::safety;
namespace tcam = crucible::cntp::tcam;

namespace {

cog::CogIdentity nic_identity(cog::CogKind kind = cog::CogKind::NicPort) {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x148, 0x1};
    id.level = cog::CogLevel::L0_Atomic;
    id.kind = kind;
    return id;
}

cog::CogIdentity switch_identity() {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x148, 0x5};
    id.level = cog::CogLevel::L0_Atomic;
    id.kind = cog::CogKind::NvSwitch;
    return id;
}

cog::NicPortTargetCaps nic_caps(std::uint32_t entries = 1024) {
    cog::NicPortTargetCaps caps{};
    caps.features.set(cog::NicFeature::Tcam);
    caps.tcam_entries = saf::Tagged<std::uint32_t, saf::source::Vendor>{
        entries};
    return caps;
}

cog::NvSwitchTargetCaps switch_caps(std::uint32_t entries = 4096) {
    cog::NvSwitchTargetCaps caps{};
    caps.features.set(cog::SwitchFeature::Tcam);
    caps.tcam_entries = saf::Tagged<std::uint32_t, saf::source::Vendor>{
        entries};
    return caps;
}

tcam::TcamFlowRule flow_rule(tcam::FlowAction action = tcam::FlowAction::Drop,
                             std::uint32_t redirect_ifindex = 0) {
    return tcam::TcamFlowRule{
        .rule_id = *tcam::admit_tcam_rule_id(0x148),
        .match = tcam::FiveTuple{
            .src_ipv4_be = 0x0a000001u,
            .dst_ipv4_be = 0x0a000002u,
            .src_port = 443,
            .dst_port = 8443,
            .protocol = 6,
            .src_prefix_bits = 32,
            .dst_prefix_bits = 32,
            .src_port_wildcard = false,
            .dst_port_wildcard = false,
        },
        .action = tcam::TcamFlowAction{
            .kind = action,
            .redirect_ifindex = redirect_ifindex,
            .dscp = *tcam::admit_tcam_dscp(46),
        },
        .priority = *tcam::admit_tcam_priority(10),
        .audit_to_cipher = true,
    };
}

void test_admission_and_names() {
    assert(tcam::tcam_error_name(tcam::TcamError::CapacityExceeded)
           == std::string_view{"CapacityExceeded"});
    assert(tcam::flow_action_name(tcam::FlowAction::Mirror)
           == std::string_view{"Mirror"});
    assert(tcam::tcam_target_kind_name(tcam::TcamTargetKind::Switch)
           == std::string_view{"Switch"});

    assert(tcam::admit_tcam_rule_id(1).has_value());
    assert(!tcam::admit_tcam_rule_id(0).has_value());
    assert(tcam::admit_tcam_entries(1).has_value());
    assert(!tcam::admit_tcam_entries(0).has_value());
    assert(!tcam::admit_tcam_entries(
               tcam::kMaxStaticTcamRules + 1).has_value());
    assert(tcam::admit_tcam_dscp(63).has_value());
    assert(!tcam::admit_tcam_dscp(64).has_value());

    auto bad_redirect = tcam::declare_tcam_rule(
        flow_rule(tcam::FlowAction::Redirect));
    assert(!bad_redirect.has_value());
    assert(bad_redirect.error() == tcam::TcamError::InvalidActionParameter);

    auto bad_prefix = flow_rule();
    bad_prefix.match.src_prefix_bits = 33;
    auto bad_match = tcam::declare_tcam_rule(bad_prefix);
    assert(!bad_match.has_value());
    assert(bad_match.error() == tcam::TcamError::InvalidMatchParameter);

    std::printf("  test_admission_and_names: PASSED\n");
}

void test_table_minting() {
    auto table = tcam::mint_tcam_table(
        eff::ColdInitCtx{}, nic_identity(), nic_caps(),
        *tcam::admit_tcam_entries(64));
    assert(table.has_value());
    assert(table->value().target_kind == tcam::TcamTargetKind::NicPort);

    auto sw = tcam::mint_tcam_table(
        eff::ColdInitCtx{}, switch_identity(), switch_caps(),
        *tcam::admit_tcam_entries(128));
    assert(sw.has_value());
    assert(sw->value().target_kind == tcam::TcamTargetKind::Switch);

    auto no_cap = nic_caps();
    no_cap.features.unset(cog::NicFeature::Tcam);
    auto missing = tcam::mint_tcam_table(
        eff::ColdInitCtx{}, nic_identity(), no_cap,
        *tcam::admit_tcam_entries(64));
    assert(!missing.has_value());
    assert(missing.error() == tcam::TcamError::MissingTcamCapability);

    auto over = tcam::mint_tcam_table(
        eff::ColdInitCtx{}, nic_identity(), nic_caps(4),
        *tcam::admit_tcam_entries(8));
    assert(!over.has_value());
    assert(over.error() == tcam::TcamError::CapacityExceeded);

    auto wrong = tcam::mint_tcam_table(
        eff::ColdInitCtx{}, nic_identity(cog::CogKind::Gpu), nic_caps(),
        *tcam::admit_tcam_entries(64));
    assert(!wrong.has_value());
    assert(wrong.error() == tcam::TcamError::WrongTargetKind);

    std::printf("  test_table_minting: PASSED\n");
}

void test_rule_table_lifecycle() {
    auto table_plan = tcam::mint_tcam_table(
        eff::ColdInitCtx{}, nic_identity(), nic_caps(),
        *tcam::admit_tcam_entries(1));
    assert(table_plan.has_value());

    tcam::TcamRules<4> table{*table_plan};
    auto declared = tcam::declare_tcam_rule(flow_rule());
    assert(declared.has_value());

    auto handle = table.add_rule(*declared);
    assert(handle.has_value());
    assert(table.installed_rules() == 1);
    assert(table.available_rules_remaining() == 0);

    auto full = table.add_rule(*declared);
    assert(!full.has_value());
    assert(full.error() == tcam::TcamError::TableFull);

    assert(table.note_match(*handle, 7).has_value());
    auto count = table.query_counter(*handle);
    assert(count.has_value());
    assert(*count == 7);
    auto const overflow_delta =
        std::numeric_limits<std::uint64_t>::max() - *count + 1u;
    assert(!table.note_match(*handle, overflow_delta).has_value());

    auto removed = table.remove_rule(std::move(*handle));
    assert(removed.has_value());
    assert(table.installed_rules() == 0);

    std::printf("  test_rule_table_lifecycle: PASSED\n");
}

void test_backend_boundary() {
    auto table_plan = tcam::mint_tcam_table(
        eff::ColdInitCtx{}, nic_identity(), nic_caps(),
        *tcam::admit_tcam_entries(4), true);
    assert(table_plan.has_value());
    auto declared = tcam::declare_tcam_rule(flow_rule());
    assert(declared.has_value());
    auto backend = tcam::force_tcam_backend_boundary(*table_plan, *declared);
    assert(!backend.has_value());
    assert(backend.error() == tcam::TcamError::VendorBackendUnavailable);

    std::printf("  test_backend_boundary: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(tcam::TcamRuleId) == sizeof(std::uint64_t));
    static_assert(sizeof(tcam::TcamEntryCount) == sizeof(std::uint32_t));
    static_assert(sizeof(tcam::TcamPriority) == sizeof(std::uint16_t));
    static_assert(sizeof(tcam::TcamDscp) == sizeof(std::uint8_t));
    static_assert(sizeof(tcam::DeclaredTcamFlowRule)
                  == sizeof(tcam::TcamFlowRule));
    static_assert(sizeof(tcam::OwnedTcamRule)
                  == sizeof(tcam::TcamRuleHandle));
    static_assert(std::same_as<
                  tcam::DeclaredTcamFlowRule::tag_type,
                  saf::source::TcamFlowRule>);
    static_assert(tcam::TcamTableShape<1>);
    static_assert(!tcam::TcamTableShape<0>);
    static_assert(tcam::CtxFitsTcamMint<eff::ColdInitCtx>);
    static_assert(!tcam::CtxFitsTcamMint<eff::BgDrainCtx>);
    static_assert(std::is_trivially_copyable_v<tcam::FiveTuple>);
    static_assert(std::is_trivially_copyable_v<tcam::TcamFlowRule>);

    std::printf("test_cntp_tcam:\n");
    test_admission_and_names();
    test_table_minting();
    test_rule_table_lifecycle();
    test_backend_boundary();
    std::printf("test_cntp_tcam: all PASSED\n");
    return 0;
}
