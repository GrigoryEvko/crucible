#include <crucible/cog/NicConfig.h>

#include <cassert>
#include <cstdio>
#include <string_view>
#include <type_traits>

namespace cog = crucible::cog;
namespace nic = crucible::cog::nic;
namespace cntp = crucible::cntp;
namespace eff = crucible::effects;
namespace saf = crucible::safety;

namespace {

cog::CogIdentity nic_identity() {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x192, 0xC0A6};
    id.kind = cog::CogKind::NicPort;
    return id;
}

void test_admission() {
    auto ring = nic::admit_ring_size(4096);
    assert(ring.has_value());
    assert(ring->value() == 4096);

    auto non_power = nic::admit_ring_size(1000);
    assert(!non_power.has_value());
    assert(non_power.error() == nic::NicConfigError::InvalidRingSize);

    auto huge_ring = nic::admit_ring_size(16384);
    assert(!huge_ring.has_value());
    assert(huge_ring.error() == nic::NicConfigError::InvalidRingSize);

    auto queues = nic::admit_queue_count(64);
    assert(queues.has_value());
    assert(queues->value() == 64);

    auto zero_queues = nic::admit_queue_count(0);
    assert(!zero_queues.has_value());
    assert(zero_queues.error() == nic::NicConfigError::InvalidQueueCount);

    auto busy = nic::admit_busy_poll_us(50);
    assert(busy.has_value());
    assert(busy->value() == 50);

    auto too_busy = nic::admit_busy_poll_us(1'000'001);
    assert(!too_busy.has_value());
    assert(too_busy.error() == nic::NicConfigError::InvalidBusyPollUs);

    std::printf("  test_admission: PASSED\n");
}

void test_mint_and_apply_boundaries() {
    auto iface = cntp::NicInterfaceName::from("eth0");
    assert(iface.has_value());

    nic::EthtoolConfig ethtool{};
    ethtool.tx_queues = *nic::admit_queue_count(16);
    ethtool.rx_queues = *nic::admit_queue_count(16);
    ethtool.combined_queues = *nic::admit_queue_count(16);
    ethtool.offloads = saf::Bits<nic::NicOffload>{
        nic::NicOffload::Tso,
        nic::NicOffload::Gso,
        nic::NicOffload::Gro,
        nic::NicOffload::RxHash,
    };

    nic::QdiscConfig qdisc{};
    qdisc.kind = nic::QdiscKind::FqCodel;
    qdisc.max_quantum = nic::PositiveQdiscParam{std::uint32_t{16'384}};

    nic::SysctlConfig sysctl{};
    sysctl.busy_poll_us = *nic::admit_busy_poll_us(50);
    sysctl.tcp_rto_min_us = *nic::admit_tcp_rto_min_us(10'000);
    auto valid_sysctl = nic::validate_sysctl_config(sysctl);
    assert(valid_sysctl.has_value());

    auto minted = nic::mint_nic_config(
        eff::ColdInitCtx{}, nic_identity(), *iface, ethtool, qdisc, sysctl);
    assert(minted.has_value());
    static_assert(std::same_as<
                  std::remove_cvref_t<decltype(*minted)>,
                  nic::DeclaredNicConfig>);
    assert(minted->value().identity.kind == cog::CogKind::NicPort);
    assert(minted->value().ethtool.interface.view() == "eth0");
    assert(minted->value().qdisc.interface.view() == "eth0");
    assert(minted->value().qdisc.kind == nic::QdiscKind::FqCodel);
    assert(minted->value().sysctl.busy_poll_us.value() == 50);

    auto apply = nic::apply_config(*minted);
    assert(!apply.has_value());
    assert(apply.error() == nic::NicConfigError::PrivilegedApplyDeferred);

    auto ethtool_apply =
        nic::apply_ethtool(nic::declare_ethtool_config(minted->value().ethtool));
    assert(!ethtool_apply.has_value());
    assert(ethtool_apply.error()
           == nic::NicConfigError::PrivilegedApplyDeferred);

    auto qdisc_apply =
        nic::apply_qdisc(nic::declare_qdisc_config(minted->value().qdisc));
    assert(!qdisc_apply.has_value());
    assert(qdisc_apply.error() == nic::NicConfigError::PrivilegedApplyDeferred);

    auto privileged = nic::mint_nic_config(
        eff::ColdInitCtx{}, nic_identity(), *iface, ethtool, qdisc, sysctl, true);
    assert(privileged.has_value());
    auto privileged_apply = nic::apply_config(*privileged);
    assert(!privileged_apply.has_value());
    assert(privileged_apply.error()
           == nic::NicConfigError::PrivilegedBackendUnavailable);

    std::printf("  test_mint_and_apply_boundaries: PASSED\n");
}

void test_identity_and_sysctl_validation() {
    auto iface = cntp::NicInterfaceName::from("eth0");
    assert(iface.has_value());

    auto zero = nic::mint_nic_config(eff::ColdInitCtx{}, cog::CogIdentity{}, *iface);
    assert(!zero.has_value());
    assert(zero.error() == nic::NicConfigError::ZeroCog);

    auto gpu = nic_identity();
    gpu.kind = cog::CogKind::Gpu;
    auto wrong_kind = nic::mint_nic_config(eff::ColdInitCtx{}, gpu, *iface);
    assert(!wrong_kind.has_value());
    assert(wrong_kind.error() == nic::NicConfigError::NonNicCog);

    nic::SysctlConfig invalid{};
    invalid.tcp_rmem.min = *nic::admit_sysctl_bytes(4096);
    invalid.tcp_rmem.pressure = *nic::admit_sysctl_bytes(2048);
    invalid.tcp_rmem.max = *nic::admit_sysctl_bytes(8192);
    auto declared = nic::declare_sysctl_config(invalid);
    assert(!declared.has_value());
    assert(declared.error() == nic::NicConfigError::InvalidTcpMemoryTriple);

    auto empty_ethtool =
        nic::apply_ethtool(nic::declare_ethtool_config(nic::EthtoolConfig{}));
    assert(!empty_ethtool.has_value());
    assert(empty_ethtool.error() == nic::NicConfigError::InvalidInterfaceName);

    nic::NicConfigPlan mismatch{};
    mismatch.identity = nic_identity();
    mismatch.ethtool.interface = *iface;
    mismatch.qdisc.interface = cntp::NicInterfaceName::from("eth1").value();
    auto mismatched_apply =
        nic::apply_config(nic::DeclaredNicConfig{mismatch});
    assert(!mismatched_apply.has_value());
    assert(mismatched_apply.error() == nic::NicConfigError::InterfaceMismatch);

    auto query = nic::query_current(nic_identity(), *iface);
    assert(!query.has_value());
    assert(query.error() == nic::NicConfigError::QueryDeferred);

    std::printf("  test_identity_and_sysctl_validation: PASSED\n");
}

void test_audit_mapping() {
    assert(nic::qdisc_kind_name(nic::QdiscKind::Fq)
           == std::string_view{"fq"});
    assert(nic::qdisc_to_audit_qdisc(nic::QdiscKind::Fq)
           == cog::NicTxQdisc::Fq);
    assert(nic::qdisc_to_audit_qdisc(nic::QdiscKind::FqCodel)
           == cog::NicTxQdisc::FqCodel);
    assert(nic::qdisc_to_audit_qdisc(nic::QdiscKind::Prio)
           == cog::NicTxQdisc::Unknown);

    saf::Bits<nic::NicOffload> offloads{
        nic::NicOffload::Tso,
        nic::NicOffload::Gro,
        nic::NicOffload::RxHash,
    };
    auto features = nic::audit_features_from_offloads(offloads);
    assert(features.test(cog::NicFeature::Tso));
    assert(features.test(cog::NicFeature::Gro));
    assert(features.test(cog::NicFeature::Rss));
    assert(!features.test(cog::NicFeature::Gso));

    std::printf("  test_audit_mapping: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(nic::NicRingSize) == sizeof(std::uint16_t));
    static_assert(sizeof(nic::DeclaredNicConfig) == sizeof(nic::NicConfigPlan));
    static_assert(std::same_as<
                  nic::DeclaredNicConfig::tag_type,
                  saf::source::NicConfig>);
    static_assert(nic::CtxFitsNicConfigMint<eff::ColdInitCtx>);
    static_assert(!nic::CtxFitsNicConfigMint<eff::BgDrainCtx>);
    static_assert(std::is_trivially_copyable_v<nic::EthtoolConfig>);
    static_assert(std::is_trivially_copyable_v<nic::QdiscConfig>);
    static_assert(std::is_trivially_copyable_v<nic::SysctlConfig>);

    std::printf("test_nic_config:\n");
    test_admission();
    test_mint_and_apply_boundaries();
    test_identity_and_sysctl_validation();
    test_audit_mapping();
    std::printf("test_nic_config: all PASSED\n");
    return 0;
}
