#include <crucible/cntp/RoceConfig.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include <foundation/reflect/EnumName.h>

#include <cstdint>
#include <cstdio>
#include <string_view>
#include <type_traits>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

// apply_roce_config, query_dcqcn_state and verify_dcqcn_active all carry
// [[deprecated("CRUCIBLE_STUB:...")]] while no vendor policy installer
// exists. Suppressing the diagnostic here is what lets the stubs be tested.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace cntp = crucible::cntp;
namespace fe = ::foundation::effects;
namespace src = ::fixy::tags::source;

namespace {

void test_admission_and_names() {
    static_assert(::foundation::reflect::enum_name(cntp::RoceError::InvalidDscp) == std::string_view{"InvalidDscp"});

    auto pfc = cntp::admit_pfc_priorities(0b00001000);
    CRUCIBLE_TEST_ASSERT(kTest, pfc.is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, std::move(pfc).expect("one priority is set").value() == 0b00001000);

    auto zero_pfc = cntp::admit_pfc_priorities(0);
    CRUCIBLE_TEST_ASSERT(kTest, zero_pfc.is_err());
    CRUCIBLE_TEST_ASSERT(kTest, zero_pfc.err() == ::fixy::some(cntp::RoceError::InvalidPfcPriorityMask));

    auto dscp = cntp::admit_roce_dscp(26);
    CRUCIBLE_TEST_ASSERT(kTest, dscp.is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, std::move(dscp).expect("26 is a DSCP value").value() == 26);

    // The DSCP field is six bits wide, so 63 is the last value it takes.
    CRUCIBLE_TEST_ASSERT(kTest, cntp::admit_roce_dscp(63).is_ok());
    auto invalid_dscp = cntp::admit_roce_dscp(64);
    CRUCIBLE_TEST_ASSERT(kTest, invalid_dscp.is_err());
    CRUCIBLE_TEST_ASSERT(kTest, invalid_dscp.err() == ::fixy::some(cntp::RoceError::InvalidDscp));

    auto alpha = cntp::admit_dcqcn_alpha_ppm(500000);
    CRUCIBLE_TEST_ASSERT(kTest, alpha.is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, std::move(alpha).expect("half of one million fits").value() == 500000);

    auto alpha_zero = cntp::admit_dcqcn_alpha_ppm(0);
    CRUCIBLE_TEST_ASSERT(kTest, alpha_zero.is_err());
    CRUCIBLE_TEST_ASSERT(kTest, alpha_zero.err() == ::fixy::some(cntp::RoceError::InvalidDcqcnAlpha));

    // An alpha is a fraction of one million parts, so one million is the
    // whole and one part more is refused.
    CRUCIBLE_TEST_ASSERT(kTest, cntp::admit_dcqcn_alpha_ppm(1000000).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, cntp::admit_dcqcn_alpha_ppm(1000001).err()
                                    == ::fixy::some(cntp::RoceError::InvalidDcqcnAlpha));

    auto target = cntp::admit_dcqcn_target_packets(5);
    CRUCIBLE_TEST_ASSERT(kTest, target.is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, std::move(target).expect("5 packets is positive").value() == 5);

    auto target_zero = cntp::admit_dcqcn_target_packets(0);
    CRUCIBLE_TEST_ASSERT(kTest, target_zero.is_err());
    CRUCIBLE_TEST_ASSERT(kTest, target_zero.err() == ::fixy::some(cntp::RoceError::InvalidDcqcnTargetPackets));

    auto ce = cntp::admit_dcqcn_ce_threshold_bytes(64 * 1024);
    CRUCIBLE_TEST_ASSERT(kTest, ce.is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, std::move(ce).expect("64 KiB is positive").value() == 64 * 1024);
    CRUCIBLE_TEST_ASSERT(kTest, cntp::admit_dcqcn_ce_threshold_bytes(0).err()
                                    == ::fixy::some(cntp::RoceError::InvalidCeThresholdBytes));

    ::fixy::check::pass(kTest, "  test_admission_and_names: PASSED\n");
}

// The error of a call that fails in this test.  A call that succeeds ends
// the process.
template <class Value>
cntp::RoceError error_of(::fixy::Result<Value, cntp::RoceError> result) {
    return std::move(result).err().expect("the call fails in this test");
}

[[nodiscard]] cntp::NicInterfaceName interface_named(::fixy::TextView name) {
    return cntp::NicInterfaceName::from(name).expect("the test names a valid interface");
}

void test_config_minting_and_validation() {
    cntp::NicInterfaceName const iface = interface_named("eth0");

    auto config = cntp::mint_roce_config<0b00001000, 26>(iface);
    static_assert(std::same_as<decltype(config), cntp::DeclaredRoceConfig>);
    CRUCIBLE_TEST_ASSERT(kTest, config.value().interface.view() == "eth0");
    CRUCIBLE_TEST_ASSERT(kTest, config.value().enable_pfc);
    CRUCIBLE_TEST_ASSERT(kTest, config.value().pfc_priorities.value() == 0b00001000);
    CRUCIBLE_TEST_ASSERT(kTest, config.value().trust_dscp);
    CRUCIBLE_TEST_ASSERT(kTest, config.value().enable_ecn);
    CRUCIBLE_TEST_ASSERT(kTest, config.value().enable_dcqcn);
    CRUCIBLE_TEST_ASSERT(kTest, config.value().roce_dscp.value() == 26);
    CRUCIBLE_TEST_ASSERT(kTest, config.value().dcqcn.alpha_ppm.value() == 500000);
    CRUCIBLE_TEST_ASSERT(kTest, config.value().dcqcn.target_packets.value() == 5);

    CRUCIBLE_TEST_ASSERT(kTest, cntp::validate_roce_config(config).is_ok());

    CRUCIBLE_TEST_ASSERT(kTest, error_of(cntp::apply_roce_config(config)) == cntp::RoceError::PrivilegedApplyDeferred);

    auto privileged = cntp::mint_roce_config<0b00001000, 26>(iface, cntp::DcqcnParams{}, true);
    CRUCIBLE_TEST_ASSERT(kTest,
                         error_of(cntp::apply_roce_config(privileged)) == cntp::RoceError::VendorBackendUnavailable);

    // A value that entered through the trusted door skips the predicate.
    // The validation reads the predicate again and refuses it.
    cntp::RoceConfig forged{.interface = iface};
    forged.roce_dscp = ::fixy::mint_refined_trusted<cntp::roce_dscp_bits>(std::uint8_t{64});
    CRUCIBLE_TEST_ASSERT(kTest, error_of(cntp::validate_roce_config(::fixy::mint_tagged<src::RoceConfig>(forged)))
                                    == cntp::RoceError::InvalidDscp);

    ::fixy::check::pass(kTest, "  test_config_minting_and_validation: PASSED\n");
}

void test_pause_counter_parse() {
    cntp::PfcPauseStats const parsed =
        cntp::parse_pfc_pause_counters(" 17\n", "23\n").expect("two counters with white space parse");
    CRUCIBLE_TEST_ASSERT(kTest, parsed.rx_pause_frames == 17);
    CRUCIBLE_TEST_ASSERT(kTest, parsed.tx_pause_frames == 23);

    CRUCIBLE_TEST_ASSERT(kTest, error_of(cntp::parse_pfc_pause_counters("17x", "23\n"))
                                    == cntp::RoceError::CounterParseFailed);
    CRUCIBLE_TEST_ASSERT(kTest, error_of(cntp::parse_pfc_pause_counters("1 7", "23\n"))
                                    == cntp::RoceError::CounterParseFailed);
    CRUCIBLE_TEST_ASSERT(kTest, error_of(cntp::parse_pfc_pause_counters("0x11", "23\n"))
                                    == cntp::RoceError::CounterParseFailed);

    // The largest counter parses, and one more is refused, not wrapped.
    cntp::PfcPauseStats const widest =
        cntp::parse_pfc_pause_counters("18446744073709551615\n", "0").expect("the largest counter parses");
    CRUCIBLE_TEST_ASSERT(kTest, widest.rx_pause_frames == ::fixy::max_of<std::uint64_t>);
    CRUCIBLE_TEST_ASSERT(kTest, error_of(cntp::parse_pfc_pause_counters("0", "18446744073709551616"))
                                    == cntp::RoceError::CounterParseFailed);

    CRUCIBLE_TEST_ASSERT(kTest,
                         error_of(cntp::parse_pfc_pause_counters("   ", "1")) == cntp::RoceError::CounterParseFailed);

    ::fixy::check::pass(kTest, "  test_pause_counter_parse: PASSED\n");
}

void test_live_surfaces_if_available() {
    ::fixy::InitLoadCtx load{fe::testing::init()};
    cntp::NicInterfaceName const lo = interface_named("lo");

    auto counters = cntp::query_pfc_pause_counters(load, lo);
    if (counters.is_err()) {
        cntp::RoceError const refusal = error_of(std::move(counters));
        CRUCIBLE_TEST_ASSERT(kTest, refusal == cntp::RoceError::CounterUnavailable
                                        || refusal == cntp::RoceError::CounterParseFailed);
        ::fixy::report(::fixy::Sink::Out, "  test_live_pfc_pause_counters: SKIPPED\n");
    } else {
        ::fixy::check::pass(kTest, "  test_live_pfc_pause_counters: PASSED\n");
    }

    // A counter of an interface that does not exist is refused.
    CRUCIBLE_TEST_ASSERT(kTest, error_of(cntp::query_pfc_pause_counters(load, interface_named("no-such-nic.0")))
                                    == cntp::RoceError::CounterUnavailable);

    // No interface name is "." or "..", so the path of a counter stays in
    // the sysfs directory of its interface.
    CRUCIBLE_TEST_ASSERT(kTest, cntp::NicInterfaceName::from("..").is_err());
    CRUCIBLE_TEST_ASSERT(kTest, cntp::NicInterfaceName::from(".").is_err());

    // With no vendor probe in place the answer is explicitly unknown, not
    // inactive. A caller has to be able to tell "the card says off" from "we
    // did not ask".
    auto state = cntp::query_dcqcn_state(lo);
    CRUCIBLE_TEST_ASSERT(kTest, state == cntp::DcqcnState::BackendUnavailable);
    static_assert(::foundation::reflect::enum_name(cntp::DcqcnState::BackendUnavailable)
                  == std::string_view{"BackendUnavailable"});
    static_assert(::foundation::reflect::enum_name(cntp::DcqcnState::Inactive) == std::string_view{"Inactive"});
    static_assert(::foundation::reflect::enum_name(cntp::DcqcnState::Active) == std::string_view{"Active"});

    // The boolean-shaped accessor maps that unknown state onto its own error
    // code, so a caller written against it keeps working unchanged.
    CRUCIBLE_TEST_ASSERT(kTest, error_of(cntp::verify_dcqcn_active(lo)) == cntp::RoceError::DcqcnStatusUnavailable);

    ::fixy::check::pass(kTest, "  test_live_surfaces_if_available: PASSED\n");
}

// The distinction these assertions defend is between a substrate that
// reports "not attempted" and one that silently admits the request and does
// nothing. Both the write side and the read side must name which of the two
// they are, and they must say it consistently.
void test_apply_paths_are_stubbed() {
    static_assert(cntp::privileged_apply_implemented == false,
                  "the RoCEv2 apply paths are stubs. privileged_apply_implemented "
                  "turns true only alongside a vendor policy installer, a per-vendor "
                  "congestion-control probe, and live-NIC fixtures in place of this "
                  "one.");
    static_assert(std::is_same_v<decltype(cntp::privileged_apply_implemented), const bool>,
                  "the honesty trait must be a compile-time bool");

    cntp::NicInterfaceName const iface = interface_named("eth0");

    auto deferred = cntp::mint_roce_config<0b00001000, 26>(iface);
    CRUCIBLE_TEST_ASSERT(kTest,
                         error_of(cntp::apply_roce_config(deferred)) == cntp::RoceError::PrivilegedApplyDeferred);

    auto requested = cntp::mint_roce_config<0b00001000, 26>(iface, cntp::DcqcnParams{}, true);
    CRUCIBLE_TEST_ASSERT(kTest,
                         error_of(cntp::apply_roce_config(requested)) == cntp::RoceError::VendorBackendUnavailable);

    // The read side is stubbed in step with the write side.
    auto state = cntp::query_dcqcn_state(iface);
    CRUCIBLE_TEST_ASSERT(kTest, state == cntp::DcqcnState::BackendUnavailable);
    CRUCIBLE_TEST_ASSERT(kTest, error_of(cntp::verify_dcqcn_active(iface)) == cntp::RoceError::DcqcnStatusUnavailable);

    ::fixy::check::pass(kTest, "  test_apply_paths_are_stubbed: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(cntp::PfcPriorityMask) == sizeof(std::uint8_t));
    static_assert(sizeof(cntp::RoceDscp) == sizeof(std::uint8_t));
    static_assert(sizeof(cntp::DeclaredRoceConfig) == sizeof(cntp::RoceConfig));
    static_assert(cntp::ValidPfcPriorityMask<0b00001000>);
    static_assert(!cntp::ValidPfcPriorityMask<0>);
    static_assert(cntp::ValidRoceDscp<26>);
    static_assert(cntp::ValidRoceDscp<63>);
    static_assert(!cntp::ValidRoceDscp<64>);
    static_assert(std::same_as<cntp::DeclaredRoceConfig::tag_type, src::RoceConfig>);
    static_assert(std::is_trivially_copy_constructible_v<cntp::DcqcnParams>
                  && std::is_trivially_destructible_v<cntp::DcqcnParams>);
    static_assert(std::is_trivially_copy_constructible_v<cntp::RoceConfig>
                  && std::is_trivially_destructible_v<cntp::RoceConfig>);

    // The counter read opens a file, which can park the caller, so the
    // context must own Block as well as IO.
    static_assert(::fixy::fs::CtxFitsFileMint<::fixy::InitLoadCtx, cntp::ProcFileReadMode>);
    static_assert(::fixy::fs::CtxFitsFileMint<::fixy::BgLoadCtx, cntp::ProcFileReadMode>);
    static_assert(!::fixy::fs::CtxFitsFileMint<::fixy::ColdInitCtx, cntp::ProcFileReadMode>);
    static_assert(!::fixy::fs::CtxFitsFileMint<::fixy::HotFgCtx, cntp::ProcFileReadMode>);

    static_assert(!cntp::privileged_apply_implemented,
                  "the RoCEv2 substrate is a stub. Every apply path returns "
                  "PrivilegedApplyDeferred or VendorBackendUnavailable, and the "
                  "congestion-control check returns DcqcnStatusUnavailable. The trait "
                  "turns true only alongside a vendor policy installer, a per-vendor "
                  "probe, and live-NIC fixtures.");

    ::fixy::report(::fixy::Sink::Out, "test_cntp_roce_config:\n");
    test_admission_and_names();
    test_config_minting_and_validation();
    test_pause_counter_parse();
    test_live_surfaces_if_available();
    test_apply_paths_are_stubbed();
    ::fixy::check::pass(kTest, "test_cntp_roce_config: all PASSED\n");
    return 0;
}

#pragma GCC diagnostic pop
