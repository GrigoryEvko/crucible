#include <crucible/cog/NicConfig.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/reflect/EnumName.h>

#include <fixy/Core.h>
#include <fixy/check/Check.h>
#include <cstdio>
#include <string_view>
#include <type_traits>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

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
    CRUCIBLE_TEST_ASSERT(kTest, ring.is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, std::move(ring).expect("4096 slots fit").value() == 4096);

    auto non_power = nic::admit_ring_size(1000);
    CRUCIBLE_TEST_ASSERT(kTest, non_power.is_err());
    CRUCIBLE_TEST_ASSERT(kTest, non_power.err() == ::fixy::some(nic::NicConfigError::InvalidRingSize));

    auto huge_ring = nic::admit_ring_size(16384);
    CRUCIBLE_TEST_ASSERT(kTest, huge_ring.is_err());
    CRUCIBLE_TEST_ASSERT(kTest, huge_ring.err() == ::fixy::some(nic::NicConfigError::InvalidRingSize));

    auto queues = nic::admit_queue_count(64);
    CRUCIBLE_TEST_ASSERT(kTest, queues.is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, std::move(queues).expect("64 queues fit").value() == 64);

    auto zero_queues = nic::admit_queue_count(0);
    CRUCIBLE_TEST_ASSERT(kTest, zero_queues.is_err());
    CRUCIBLE_TEST_ASSERT(kTest, zero_queues.err() == ::fixy::some(nic::NicConfigError::InvalidQueueCount));

    auto rss = nic::admit_rss_table_size(4097);
    CRUCIBLE_TEST_ASSERT(kTest, rss.is_err());
    CRUCIBLE_TEST_ASSERT(kTest, rss.err() == ::fixy::some(nic::NicConfigError::InvalidRssTableSize));

    auto busy = nic::admit_busy_poll_us(50);
    CRUCIBLE_TEST_ASSERT(kTest, busy.is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, std::move(busy).expect("50 us fits").value() == 50);

    auto too_busy = nic::admit_busy_poll_us(1000001);
    CRUCIBLE_TEST_ASSERT(kTest, too_busy.is_err());
    CRUCIBLE_TEST_ASSERT(kTest, too_busy.err() == ::fixy::some(nic::NicConfigError::InvalidBusyPollUs));

    auto zero_bytes = nic::admit_sysctl_bytes(0);
    CRUCIBLE_TEST_ASSERT(kTest, zero_bytes.is_err());
    CRUCIBLE_TEST_ASSERT(kTest, zero_bytes.err() == ::fixy::some(nic::NicConfigError::InvalidSysctlBytes));

    auto zero_rto = nic::admit_tcp_rto_min_us(0);
    CRUCIBLE_TEST_ASSERT(kTest, zero_rto.is_err());
    CRUCIBLE_TEST_ASSERT(kTest, zero_rto.err() == ::fixy::some(nic::NicConfigError::InvalidTcpRtoMinUs));

    ::fixy::check::pass(kTest, "  test_admission: PASSED\n");
}

void test_mint_and_apply_boundaries() {
    cntp::NicInterfaceName const iface = cntp::NicInterfaceName::from("eth0").expect("eth0 is an interface name");

    nic::EthtoolConfig ethtool{};
    ethtool.tx_queues = nic::admit_queue_count(16).expect("16 queues fit");
    ethtool.rx_queues = nic::admit_queue_count(16).expect("16 queues fit");
    ethtool.combined_queues = nic::admit_queue_count(16).expect("16 queues fit");
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
    sysctl.busy_poll_us = nic::admit_busy_poll_us(50).expect("50 us fits");
    sysctl.tcp_rto_min_us = nic::admit_tcp_rto_min_us(10000).expect("10 ms fits");
    auto valid_sysctl = nic::validate_sysctl_config(sysctl);
    CRUCIBLE_TEST_ASSERT(kTest, valid_sysctl.is_ok());

    auto minted = nic::mint_nic_config(init_ctx(), nic_identity(), iface, ethtool, qdisc, sysctl)
                      .expect("a NicPort identity and a named interface make a configuration");
    static_assert(std::same_as<decltype(minted), nic::DeclaredNicConfig>);
    CRUCIBLE_TEST_ASSERT(kTest, minted.value().identity.kind == cog::CogKind::NicPort);
    CRUCIBLE_TEST_ASSERT(kTest, minted.value().ethtool.interface.view() == "eth0");
    CRUCIBLE_TEST_ASSERT(kTest, minted.value().qdisc.interface.view() == "eth0");
    CRUCIBLE_TEST_ASSERT(kTest, minted.value().qdisc.kind == nic::QdiscKind::FqCodel);
    CRUCIBLE_TEST_ASSERT(kTest, minted.value().sysctl.busy_poll_us.value() == 50);

    auto apply = nic::apply_config(minted);
    CRUCIBLE_TEST_ASSERT(kTest, apply.err() == ::fixy::some(nic::NicConfigError::PrivilegedApplyDeferred));

    auto declared_ethtool =
        nic::declare_ethtool_config(minted.value().ethtool).expect("the minted ethtool configuration is valid");
    auto ethtool_apply = nic::apply_ethtool(declared_ethtool);
    CRUCIBLE_TEST_ASSERT(kTest, ethtool_apply.err() == ::fixy::some(nic::NicConfigError::PrivilegedApplyDeferred));

    auto declared_qdisc =
        nic::declare_qdisc_config(minted.value().qdisc).expect("the minted qdisc configuration is valid");
    auto qdisc_apply = nic::apply_qdisc(declared_qdisc);
    CRUCIBLE_TEST_ASSERT(kTest, qdisc_apply.err() == ::fixy::some(nic::NicConfigError::PrivilegedApplyDeferred));

    auto declared_sysctl = nic::declare_sysctl_config(sysctl).expect("the sysctl configuration is valid");
    auto sysctl_apply = nic::apply_sysctl(declared_sysctl);
    CRUCIBLE_TEST_ASSERT(kTest, sysctl_apply.err() == ::fixy::some(nic::NicConfigError::PrivilegedApplyDeferred));

    auto privileged = nic::mint_nic_config(init_ctx(), nic_identity(), iface, ethtool, qdisc, sysctl, true)
                          .expect("a privileged request makes a configuration too");
    auto privileged_apply = nic::apply_config(privileged);
    CRUCIBLE_TEST_ASSERT(kTest,
                         privileged_apply.err() == ::fixy::some(nic::NicConfigError::PrivilegedBackendUnavailable));

    ::fixy::check::pass(kTest, "  test_mint_and_apply_boundaries: PASSED\n");
}

void test_identity_and_sysctl_validation() {
    cntp::NicInterfaceName const iface = cntp::NicInterfaceName::from("eth0").expect("eth0 is an interface name");

    auto zero = nic::mint_nic_config(init_ctx(), cog::CogIdentity{}, iface);
    CRUCIBLE_TEST_ASSERT(kTest, zero.err() == ::fixy::some(nic::NicConfigError::ZeroCog));

    auto gpu = nic_identity();
    gpu.kind = cog::CogKind::Gpu;
    auto wrong_kind = nic::mint_nic_config(init_ctx(), gpu, iface);
    CRUCIBLE_TEST_ASSERT(kTest, wrong_kind.err() == ::fixy::some(nic::NicConfigError::NonNicCog));

    nic::SysctlConfig invalid{};
    invalid.tcp_rmem.min = nic::admit_sysctl_bytes(4096).expect("4096 bytes fit");
    invalid.tcp_rmem.pressure = nic::admit_sysctl_bytes(2048).expect("2048 bytes fit");
    invalid.tcp_rmem.max = nic::admit_sysctl_bytes(8192).expect("8192 bytes fit");
    auto declared = nic::declare_sysctl_config(invalid);
    CRUCIBLE_TEST_ASSERT(kTest, declared.err() == ::fixy::some(nic::NicConfigError::InvalidTcpMemoryTriple));

    auto unordered_apply = nic::apply_sysctl(unvalidated_tag(invalid));
    CRUCIBLE_TEST_ASSERT(kTest, unordered_apply.err() == ::fixy::some(nic::NicConfigError::InvalidTcpMemoryTriple));

    auto empty_ethtool = nic::declare_ethtool_config(nic::EthtoolConfig{});
    CRUCIBLE_TEST_ASSERT(kTest, empty_ethtool.err() == ::fixy::some(nic::NicConfigError::InvalidInterfaceName));

    auto empty_qdisc = nic::declare_qdisc_config(nic::QdiscConfig{});
    CRUCIBLE_TEST_ASSERT(kTest, empty_qdisc.err() == ::fixy::some(nic::NicConfigError::InvalidInterfaceName));

    auto empty_ethtool_apply = nic::apply_ethtool(unvalidated_tag(nic::EthtoolConfig{}));
    CRUCIBLE_TEST_ASSERT(kTest, empty_ethtool_apply.err() == ::fixy::some(nic::NicConfigError::InvalidInterfaceName));

    nic::NicConfigPlan mismatch{};
    mismatch.identity = nic_identity();
    mismatch.ethtool.interface = iface;
    mismatch.qdisc.interface = cntp::NicInterfaceName::from("eth1").expect("eth1 is an interface name");
    CRUCIBLE_TEST_ASSERT(kTest, nic::validate_nic_config(mismatch).err()
                                    == ::fixy::some(nic::NicConfigError::InterfaceMismatch));
    auto mismatched_apply = nic::apply_config(unvalidated_tag(mismatch));
    CRUCIBLE_TEST_ASSERT(kTest, mismatched_apply.err() == ::fixy::some(nic::NicConfigError::InterfaceMismatch));

    auto query = nic::query_current(nic_identity(), iface);
    CRUCIBLE_TEST_ASSERT(kTest, query.err() == ::fixy::some(nic::NicConfigError::QueryDeferred));

    ::fixy::check::pass(kTest, "  test_identity_and_sysctl_validation: PASSED\n");
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

    cntp::NicInterfaceName const iface = cntp::NicInterfaceName::from("eth0").expect("eth0 is an interface name");

    nic::EthtoolConfig ethtool{};
    ethtool.tx_queues = nic::admit_queue_count(8).expect("8 queues fit");
    ethtool.rx_queues = nic::admit_queue_count(8).expect("8 queues fit");

    // A configuration that does not ask for privileged work.
    auto deferred = nic::mint_nic_config(init_ctx(), nic_identity(), iface, ethtool)
                        .expect("a NicPort identity and a named interface make a configuration");
    auto deferred_apply = nic::apply_config(deferred);
    CRUCIBLE_TEST_ASSERT(kTest, deferred_apply.err() == ::fixy::some(nic::NicConfigError::PrivilegedApplyDeferred));

    auto deferred_ethtool = nic::apply_ethtool(
        nic::declare_ethtool_config(deferred.value().ethtool).expect("the minted ethtool configuration is valid"));
    CRUCIBLE_TEST_ASSERT(kTest, deferred_ethtool.err() == ::fixy::some(nic::NicConfigError::PrivilegedApplyDeferred));

    auto deferred_qdisc = nic::apply_qdisc(
        nic::declare_qdisc_config(deferred.value().qdisc).expect("the minted qdisc configuration is valid"));
    CRUCIBLE_TEST_ASSERT(kTest, deferred_qdisc.err() == ::fixy::some(nic::NicConfigError::PrivilegedApplyDeferred));

    // One that does ask, and is told the backend is missing.
    auto requested = nic::mint_nic_config(init_ctx(), nic_identity(), iface, ethtool, {}, {}, true)
                         .expect("a privileged request makes a configuration too");
    auto requested_apply = nic::apply_config(requested);
    CRUCIBLE_TEST_ASSERT(kTest,
                         requested_apply.err() == ::fixy::some(nic::NicConfigError::PrivilegedBackendUnavailable));

    // The read side defers in the same way the write side does.
    auto query = nic::query_current(nic_identity(), iface);
    CRUCIBLE_TEST_ASSERT(kTest, query.err() == ::fixy::some(nic::NicConfigError::QueryDeferred));

    ::fixy::check::pass(kTest, "  test_apply_paths_are_stubbed: PASSED\n");
}

void test_audit_mapping() {
    CRUCIBLE_TEST_ASSERT(kTest, nic::qdisc_kind_name(nic::QdiscKind::Fq) == ::fixy::TextView{"fq"});
    CRUCIBLE_TEST_ASSERT(kTest, nic::qdisc_kind_name(nic::QdiscKind::FqCodel) == ::fixy::TextView{"fq_codel"});
    CRUCIBLE_TEST_ASSERT(kTest, nic::qdisc_to_audit_qdisc(nic::QdiscKind::Fq) == cog::NicTxQdisc::Fq);
    CRUCIBLE_TEST_ASSERT(kTest, nic::qdisc_to_audit_qdisc(nic::QdiscKind::FqCodel) == cog::NicTxQdisc::FqCodel);
    CRUCIBLE_TEST_ASSERT(kTest, nic::qdisc_to_audit_qdisc(nic::QdiscKind::Prio) == cog::NicTxQdisc::Unknown);

    ::fixy::Bits<nic::NicOffload> offloads{
        nic::NicOffload::Tso,
        nic::NicOffload::Gro,
        nic::NicOffload::RxHash,
    };
    auto features = nic::audit_features_from_offloads(offloads);
    CRUCIBLE_TEST_ASSERT(kTest, features.test(cog::NicFeature::Tso));
    CRUCIBLE_TEST_ASSERT(kTest, features.test(cog::NicFeature::Gro));
    CRUCIBLE_TEST_ASSERT(kTest, features.test(cog::NicFeature::Rss));
    CRUCIBLE_TEST_ASSERT(kTest, !features.test(cog::NicFeature::Gso));

    ::fixy::check::pass(kTest, "  test_audit_mapping: PASSED\n");
}

void test_enumerator_names() {
    using ::fixy::enum_name_text;
    CRUCIBLE_TEST_ASSERT(kTest,
                         enum_name_text(nic::NicConfigError::QueryDeferred) == ::fixy::TextView{"QueryDeferred"});
    CRUCIBLE_TEST_ASSERT(kTest, enum_name_text(nic::NicOffload::RxHash) == ::fixy::TextView{"RxHash"});
    CRUCIBLE_TEST_ASSERT(kTest, enum_name_text(static_cast<nic::NicConfigError>(0xFF))
                                    == ::fixy::TextView{"<unknown NicConfigError>"});

    ::fixy::check::pass(kTest, "  test_enumerator_names: PASSED\n");
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

    ::fixy::report(::fixy::Sink::Out, "test_nic_config:\n");
    test_admission();
    test_mint_and_apply_boundaries();
    test_identity_and_sysctl_validation();
    test_audit_mapping();
    test_enumerator_names();
    test_apply_paths_are_stubbed();
    ::fixy::check::pass(kTest, "test_nic_config: all PASSED\n");
    return 0;
}

#pragma GCC diagnostic pop
