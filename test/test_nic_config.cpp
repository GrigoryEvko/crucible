#include <crucible/cog/NicConfig.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/reflect/EnumName.h>

#include <cassert>
#include <cstdio>
#include <string_view>
#include <type_traits>

// Every apply and query entry point this file calls carries a
// [[deprecated("CRUCIBLE_STUB:...")]] attribute, because no
// privileged backend stands behind it yet.  Calling them on purpose
// is the whole point here, so the warning is suppressed for this file
// alone.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace cog = crucible::cog;
namespace nic = crucible::cog::nic;
namespace cntp = crucible::cntp;
namespace eff = ::fixy;

namespace {

eff::ColdInitCtx init_ctx() { return eff::ColdInitCtx{::foundation::effects::testing::init()}; }

cog::CogIdentity nic_identity() {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x192, 0xC0A6};
    id.kind = cog::CogKind::NicPort;
    return id;
}

// A configuration under the source tag that skipped its validator.  The
// apply paths check again, and this is how a test reaches that check.
template <typename Config>
::fixy::Tagged<Config, ::fixy::tags::source::NicConfig> unvalidated_tag(Config const& config) {
    return ::fixy::mint_tagged<::fixy::tags::source::NicConfig>(config);
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

    auto rss = nic::admit_rss_table_size(4097);
    assert(!rss.has_value());
    assert(rss.error() == nic::NicConfigError::InvalidRssTableSize);

    auto busy = nic::admit_busy_poll_us(50);
    assert(busy.has_value());
    assert(busy->value() == 50);

    auto too_busy = nic::admit_busy_poll_us(1000001);
    assert(!too_busy.has_value());
    assert(too_busy.error() == nic::NicConfigError::InvalidBusyPollUs);

    auto zero_bytes = nic::admit_sysctl_bytes(0);
    assert(!zero_bytes.has_value());
    assert(zero_bytes.error() == nic::NicConfigError::InvalidSysctlBytes);

    auto zero_rto = nic::admit_tcp_rto_min_us(0);
    assert(!zero_rto.has_value());
    assert(zero_rto.error() == nic::NicConfigError::InvalidTcpRtoMinUs);

    std::printf("  test_admission: PASSED\n");
}

void test_mint_and_apply_boundaries() {
    auto iface = cntp::NicInterfaceName::from("eth0");
    assert(iface.has_value());

    nic::EthtoolConfig ethtool{};
    ethtool.tx_queues = *nic::admit_queue_count(16);
    ethtool.rx_queues = *nic::admit_queue_count(16);
    ethtool.combined_queues = *nic::admit_queue_count(16);
    ethtool.offloads = ::fixy::Bits<nic::NicOffload>{
        nic::NicOffload::Tso,
        nic::NicOffload::Gso,
        nic::NicOffload::Gro,
        nic::NicOffload::RxHash,
    };

    nic::QdiscConfig qdisc{};
    qdisc.kind = nic::QdiscKind::FqCodel;
    qdisc.max_quantum = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{16384});

    nic::SysctlConfig sysctl{};
    sysctl.busy_poll_us = *nic::admit_busy_poll_us(50);
    sysctl.tcp_rto_min_us = *nic::admit_tcp_rto_min_us(10000);
    auto valid_sysctl = nic::validate_sysctl_config(sysctl);
    assert(valid_sysctl.has_value());

    auto minted = nic::mint_nic_config(init_ctx(), nic_identity(), *iface, ethtool, qdisc, sysctl);
    assert(minted.has_value());
    static_assert(std::same_as<std::remove_cvref_t<decltype(*minted)>, nic::DeclaredNicConfig>);
    assert(minted->value().identity.kind == cog::CogKind::NicPort);
    assert(minted->value().ethtool.interface.view() == "eth0");
    assert(minted->value().qdisc.interface.view() == "eth0");
    assert(minted->value().qdisc.kind == nic::QdiscKind::FqCodel);
    assert(minted->value().sysctl.busy_poll_us.value() == 50);

    auto apply = nic::apply_config(*minted);
    assert(!apply.has_value());
    assert(apply.error() == nic::NicConfigError::PrivilegedApplyDeferred);

    auto declared_ethtool = nic::declare_ethtool_config(minted->value().ethtool);
    assert(declared_ethtool.has_value());
    auto ethtool_apply = nic::apply_ethtool(*declared_ethtool);
    assert(!ethtool_apply.has_value());
    assert(ethtool_apply.error() == nic::NicConfigError::PrivilegedApplyDeferred);

    auto declared_qdisc = nic::declare_qdisc_config(minted->value().qdisc);
    assert(declared_qdisc.has_value());
    auto qdisc_apply = nic::apply_qdisc(*declared_qdisc);
    assert(!qdisc_apply.has_value());
    assert(qdisc_apply.error() == nic::NicConfigError::PrivilegedApplyDeferred);

    auto declared_sysctl = nic::declare_sysctl_config(sysctl);
    assert(declared_sysctl.has_value());
    auto sysctl_apply = nic::apply_sysctl(*declared_sysctl);
    assert(!sysctl_apply.has_value());
    assert(sysctl_apply.error() == nic::NicConfigError::PrivilegedApplyDeferred);

    auto privileged = nic::mint_nic_config(init_ctx(), nic_identity(), *iface, ethtool, qdisc, sysctl, true);
    assert(privileged.has_value());
    auto privileged_apply = nic::apply_config(*privileged);
    assert(!privileged_apply.has_value());
    assert(privileged_apply.error() == nic::NicConfigError::PrivilegedBackendUnavailable);

    std::printf("  test_mint_and_apply_boundaries: PASSED\n");
}

void test_identity_and_sysctl_validation() {
    auto iface = cntp::NicInterfaceName::from("eth0");
    assert(iface.has_value());

    auto zero = nic::mint_nic_config(init_ctx(), cog::CogIdentity{}, *iface);
    assert(!zero.has_value());
    assert(zero.error() == nic::NicConfigError::ZeroCog);

    auto gpu = nic_identity();
    gpu.kind = cog::CogKind::Gpu;
    auto wrong_kind = nic::mint_nic_config(init_ctx(), gpu, *iface);
    assert(!wrong_kind.has_value());
    assert(wrong_kind.error() == nic::NicConfigError::NonNicCog);

    nic::SysctlConfig invalid{};
    invalid.tcp_rmem.min = *nic::admit_sysctl_bytes(4096);
    invalid.tcp_rmem.pressure = *nic::admit_sysctl_bytes(2048);
    invalid.tcp_rmem.max = *nic::admit_sysctl_bytes(8192);
    auto declared = nic::declare_sysctl_config(invalid);
    assert(!declared.has_value());
    assert(declared.error() == nic::NicConfigError::InvalidTcpMemoryTriple);

    auto unordered_apply = nic::apply_sysctl(unvalidated_tag(invalid));
    assert(!unordered_apply.has_value());
    assert(unordered_apply.error() == nic::NicConfigError::InvalidTcpMemoryTriple);

    auto empty_ethtool = nic::declare_ethtool_config(nic::EthtoolConfig{});
    assert(!empty_ethtool.has_value());
    assert(empty_ethtool.error() == nic::NicConfigError::InvalidInterfaceName);

    auto empty_qdisc = nic::declare_qdisc_config(nic::QdiscConfig{});
    assert(!empty_qdisc.has_value());
    assert(empty_qdisc.error() == nic::NicConfigError::InvalidInterfaceName);

    auto empty_ethtool_apply = nic::apply_ethtool(unvalidated_tag(nic::EthtoolConfig{}));
    assert(!empty_ethtool_apply.has_value());
    assert(empty_ethtool_apply.error() == nic::NicConfigError::InvalidInterfaceName);

    nic::NicConfigPlan mismatch{};
    mismatch.identity = nic_identity();
    mismatch.ethtool.interface = *iface;
    mismatch.qdisc.interface = cntp::NicInterfaceName::from("eth1").value();
    assert(nic::validate_nic_config(mismatch).error() == nic::NicConfigError::InterfaceMismatch);
    auto mismatched_apply = nic::apply_config(unvalidated_tag(mismatch));
    assert(!mismatched_apply.has_value());
    assert(mismatched_apply.error() == nic::NicConfigError::InterfaceMismatch);

    auto query = nic::query_current(nic_identity(), *iface);
    assert(!query.has_value());
    assert(query.error() == nic::NicConfigError::QueryDeferred);

    std::printf("  test_identity_and_sysctl_validation: PASSED\n");
}

// Nothing here touches the kernel yet, and three separate claims say
// so rather than one.  A caller that did not ask for privileged work
// is told the work was deferred.  A caller that did ask is told the
// backend is absent, which is different from being quietly admitted
// and ignored.  A reader of the current settings is told the query
// was deferred, so the read side cannot fabricate defaults while the
// write side does nothing.
//
// A real backend means flipping the marker, replacing these
// assertions with ones driven against a live interface, and checking
// the query against a known interface state.
void test_apply_paths_are_stubbed() {
    static_assert(nic::privileged_apply_implemented == false,
                  "The privileged apply paths are still stubs.  Setting this "
                  "marker true requires a backend that can change interface "
                  "settings, and these assertions replaced by ones driven "
                  "against a live interface.");
    static_assert(std::is_same_v<decltype(nic::privileged_apply_implemented), const bool>,
                  "The marker must be a compile-time bool.");

    auto iface = cntp::NicInterfaceName::from("eth0");
    assert(iface.has_value());

    nic::EthtoolConfig ethtool{};
    ethtool.tx_queues = *nic::admit_queue_count(8);
    ethtool.rx_queues = *nic::admit_queue_count(8);

    // A configuration that does not ask for privileged work.
    auto deferred = nic::mint_nic_config(init_ctx(), nic_identity(), *iface, ethtool);
    assert(deferred.has_value());
    auto deferred_apply = nic::apply_config(*deferred);
    assert(!deferred_apply.has_value());
    assert(deferred_apply.error() == nic::NicConfigError::PrivilegedApplyDeferred);

    auto deferred_ethtool = nic::apply_ethtool(*nic::declare_ethtool_config(deferred->value().ethtool));
    assert(!deferred_ethtool.has_value());
    assert(deferred_ethtool.error() == nic::NicConfigError::PrivilegedApplyDeferred);

    auto deferred_qdisc = nic::apply_qdisc(*nic::declare_qdisc_config(deferred->value().qdisc));
    assert(!deferred_qdisc.has_value());
    assert(deferred_qdisc.error() == nic::NicConfigError::PrivilegedApplyDeferred);

    // One that does ask, and is told the backend is missing.
    auto requested = nic::mint_nic_config(init_ctx(), nic_identity(), *iface, ethtool, {}, {}, true);
    assert(requested.has_value());
    auto requested_apply = nic::apply_config(*requested);
    assert(!requested_apply.has_value());
    assert(requested_apply.error() == nic::NicConfigError::PrivilegedBackendUnavailable);

    // The read side defers in the same way the write side does.
    auto query = nic::query_current(nic_identity(), *iface);
    assert(!query.has_value());
    assert(query.error() == nic::NicConfigError::QueryDeferred);

    std::printf("  test_apply_paths_are_stubbed: PASSED\n");
}

void test_audit_mapping() {
    assert(nic::qdisc_kind_name(nic::QdiscKind::Fq) == std::string_view{"fq"});
    assert(nic::qdisc_kind_name(nic::QdiscKind::FqCodel) == std::string_view{"fq_codel"});
    assert(nic::qdisc_to_audit_qdisc(nic::QdiscKind::Fq) == cog::NicTxQdisc::Fq);
    assert(nic::qdisc_to_audit_qdisc(nic::QdiscKind::FqCodel) == cog::NicTxQdisc::FqCodel);
    assert(nic::qdisc_to_audit_qdisc(nic::QdiscKind::Prio) == cog::NicTxQdisc::Unknown);

    ::fixy::Bits<nic::NicOffload> offloads{
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

void test_enumerator_names() {
    using ::foundation::reflect::enum_name;
    assert(enum_name(nic::NicConfigError::QueryDeferred) == std::string_view{"QueryDeferred"});
    assert(enum_name(nic::NicOffload::RxHash) == std::string_view{"RxHash"});
    assert(enum_name(static_cast<nic::NicConfigError>(0xFF)) == std::string_view{"<unknown NicConfigError>"});

    std::printf("  test_enumerator_names: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(nic::NicRingSize) == sizeof(std::uint16_t));
    static_assert(sizeof(nic::DeclaredNicConfig) == sizeof(nic::NicConfigPlan));
    static_assert(std::same_as<nic::DeclaredNicConfig::tag_type, ::fixy::tags::source::NicConfig>);
    static_assert(nic::CtxFitsNicConfigMint<eff::ColdInitCtx>);
    static_assert(!nic::CtxFitsNicConfigMint<eff::BgDrainCtx>);
    static_assert(!nic::CtxFitsNicConfigMint<eff::HotFgCtx>);
    static_assert(!nic::CtxFitsNicConfigMint<int>);
    static_assert(!std::is_constructible_v<nic::NicRingSize, std::uint16_t>,
                  "a ring size is reached only through its mint or its admission");
    static_assert(!std::is_constructible_v<nic::DeclaredNicConfig, nic::NicConfigPlan>,
                  "a declared configuration is reached only through a mint");
    static_assert(std::is_trivially_copy_constructible_v<nic::EthtoolConfig>);
    static_assert(std::is_trivially_copy_constructible_v<nic::QdiscConfig>);
    static_assert(std::is_trivially_copy_constructible_v<nic::SysctlConfig>);

    // The same marker, checked at translation time, so that flipping
    // it without rewriting the runtime witness fails the build.
    static_assert(!nic::privileged_apply_implemented, "The configuration substrate is still a stub: every apply "
                                                      "path defers or reports the backend missing, and every query "
                                                      "defers.  Setting this marker true requires a backend that "
                                                      "can change interface settings, and fixtures driven against a "
                                                      "live interface.");

    std::printf("test_nic_config:\n");
    test_admission();
    test_mint_and_apply_boundaries();
    test_identity_and_sysctl_validation();
    test_audit_mapping();
    test_enumerator_names();
    test_apply_paths_are_stubbed();
    std::printf("test_nic_config: all PASSED\n");
    return 0;
}

#pragma GCC diagnostic pop
