#include <crucible/cntp/RoceConfig.h>
#include <fixy/Ctx.h>
#include <foundation/reflect/EnumName.h>

#include "test_assert.h"
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string_view>
#include <type_traits>

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
    assert(pfc.has_value());
    assert(pfc->value() == 0b00001000);

    auto zero_pfc = cntp::admit_pfc_priorities(0);
    assert(!zero_pfc.has_value());
    assert(zero_pfc.error() == cntp::RoceError::InvalidPfcPriorityMask);

    auto dscp = cntp::admit_roce_dscp(26);
    assert(dscp.has_value());
    assert(dscp->value() == 26);

    // The DSCP field is six bits wide, so 63 is the last value it takes.
    assert(cntp::admit_roce_dscp(63).has_value());
    auto invalid_dscp = cntp::admit_roce_dscp(64);
    assert(!invalid_dscp.has_value());
    assert(invalid_dscp.error() == cntp::RoceError::InvalidDscp);

    auto alpha = cntp::admit_dcqcn_alpha_ppm(500000);
    assert(alpha.has_value());
    assert(alpha->value() == 500000);

    auto alpha_zero = cntp::admit_dcqcn_alpha_ppm(0);
    assert(!alpha_zero.has_value());
    assert(alpha_zero.error() == cntp::RoceError::InvalidDcqcnAlpha);

    // An alpha is a fraction of one million parts, so one million is the
    // whole and one part more is refused.
    assert(cntp::admit_dcqcn_alpha_ppm(1000000).has_value());
    assert(cntp::admit_dcqcn_alpha_ppm(1000001).error() == cntp::RoceError::InvalidDcqcnAlpha);

    auto target = cntp::admit_dcqcn_target_packets(5);
    assert(target.has_value());
    assert(target->value() == 5);

    auto target_zero = cntp::admit_dcqcn_target_packets(0);
    assert(!target_zero.has_value());
    assert(target_zero.error() == cntp::RoceError::InvalidDcqcnTargetPackets);

    auto ce = cntp::admit_dcqcn_ce_threshold_bytes(64 * 1024);
    assert(ce.has_value());
    assert(ce->value() == 64 * 1024);
    assert(cntp::admit_dcqcn_ce_threshold_bytes(0).error() == cntp::RoceError::InvalidCeThresholdBytes);

    crucible::test::pass("  test_admission_and_names: PASSED\n");
}

void test_config_minting_and_validation() {
    auto iface = cntp::NicInterfaceName::from("eth0");
    assert(iface.has_value());

    auto config = cntp::mint_roce_config<0b00001000, 26>(*iface);
    static_assert(std::same_as<decltype(config), cntp::DeclaredRoceConfig>);
    assert(config.value().interface.view() == "eth0");
    assert(config.value().enable_pfc);
    assert(config.value().pfc_priorities.value() == 0b00001000);
    assert(config.value().trust_dscp);
    assert(config.value().enable_ecn);
    assert(config.value().enable_dcqcn);
    assert(config.value().roce_dscp.value() == 26);
    assert(config.value().dcqcn.alpha_ppm.value() == 500000);
    assert(config.value().dcqcn.target_packets.value() == 5);

    auto valid = cntp::validate_roce_config(config);
    assert(valid.has_value());

    auto apply = cntp::apply_roce_config(config);
    assert(!apply.has_value());
    assert(apply.error() == cntp::RoceError::PrivilegedApplyDeferred);

    auto privileged = cntp::mint_roce_config<0b00001000, 26>(*iface, cntp::DcqcnParams{}, true);
    auto privileged_apply = cntp::apply_roce_config(privileged);
    assert(!privileged_apply.has_value());
    assert(privileged_apply.error() == cntp::RoceError::VendorBackendUnavailable);

    // A value that entered through the trusted door skips the predicate.
    // The validation reads the predicate again and refuses it.
    cntp::RoceConfig forged{.interface = *iface};
    forged.roce_dscp = ::fixy::mint_refined_trusted<cntp::roce_dscp_bits>(std::uint8_t{64});
    auto forged_valid = cntp::validate_roce_config(::fixy::mint_tagged<src::RoceConfig>(forged));
    assert(!forged_valid.has_value());
    assert(forged_valid.error() == cntp::RoceError::InvalidDscp);

    crucible::test::pass("  test_config_minting_and_validation: PASSED\n");
}

void test_pause_counter_parse() {
    auto parsed = cntp::parse_pfc_pause_counters(" 17\n", "23\n");
    assert(parsed.has_value());
    assert(parsed->rx_pause_frames == 17);
    assert(parsed->tx_pause_frames == 23);

    auto bad = cntp::parse_pfc_pause_counters("17x", "23\n");
    assert(!bad.has_value());
    assert(bad.error() == cntp::RoceError::CounterParseFailed);

    // The largest counter parses, and one more is refused, not wrapped.
    auto widest = cntp::parse_pfc_pause_counters("18446744073709551615\n", "0");
    assert(widest.has_value());
    assert(widest->rx_pause_frames == std::numeric_limits<std::uint64_t>::max());
    auto overflow = cntp::parse_pfc_pause_counters("0", "18446744073709551616");
    assert(!overflow.has_value());
    assert(overflow.error() == cntp::RoceError::CounterParseFailed);

    auto blank = cntp::parse_pfc_pause_counters("   ", "1");
    assert(!blank.has_value());
    assert(blank.error() == cntp::RoceError::CounterParseFailed);

    crucible::test::pass("  test_pause_counter_parse: PASSED\n");
}

void test_live_surfaces_if_available() {
    ::fixy::InitLoadCtx load{fe::testing::init()};
    auto lo = cntp::NicInterfaceName::from("lo");
    assert(lo.has_value());

    auto counters = cntp::query_pfc_pause_counters(load, *lo);
    if (!counters.has_value()) {
        assert(counters.error() == cntp::RoceError::CounterUnavailable
               || counters.error() == cntp::RoceError::CounterParseFailed);
        ::fixy::report(::fixy::Sink::Out, "  test_live_pfc_pause_counters: SKIPPED\n");
    } else {
        crucible::test::pass("  test_live_pfc_pause_counters: PASSED\n");
    }

    // An interface name may be "..", and the read stays under the sysfs
    // root, so that name reaches no file.
    auto parent = cntp::NicInterfaceName::from("..");
    assert(parent.has_value());
    auto escaped = cntp::query_pfc_pause_counters(load, *parent);
    assert(!escaped.has_value());
    assert(escaped.error() == cntp::RoceError::CounterUnavailable);

    // With no vendor probe in place the answer is explicitly unknown, not
    // inactive. A caller has to be able to tell "the card says off" from "we
    // did not ask".
    auto state = cntp::query_dcqcn_state(*lo);
    assert(state == cntp::DcqcnState::BackendUnavailable);
    static_assert(::foundation::reflect::enum_name(cntp::DcqcnState::BackendUnavailable)
                  == std::string_view{"BackendUnavailable"});
    static_assert(::foundation::reflect::enum_name(cntp::DcqcnState::Inactive) == std::string_view{"Inactive"});
    static_assert(::foundation::reflect::enum_name(cntp::DcqcnState::Active) == std::string_view{"Active"});

    // The boolean-shaped accessor maps that unknown state onto its own error
    // code, so a caller written against it keeps working unchanged.
    auto dcqcn = cntp::verify_dcqcn_active(*lo);
    assert(!dcqcn.has_value());
    assert(dcqcn.error() == cntp::RoceError::DcqcnStatusUnavailable);

    crucible::test::pass("  test_live_surfaces_if_available: PASSED\n");
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

    auto iface = cntp::NicInterfaceName::from("eth0");
    assert(iface.has_value());

    auto deferred = cntp::mint_roce_config<0b00001000, 26>(*iface);
    auto deferred_apply = cntp::apply_roce_config(deferred);
    assert(!deferred_apply.has_value());
    assert(deferred_apply.error() == cntp::RoceError::PrivilegedApplyDeferred);

    auto requested = cntp::mint_roce_config<0b00001000, 26>(*iface, cntp::DcqcnParams{}, true);
    auto requested_apply = cntp::apply_roce_config(requested);
    assert(!requested_apply.has_value());
    assert(requested_apply.error() == cntp::RoceError::VendorBackendUnavailable);

    // The read side is stubbed in step with the write side.
    auto state = cntp::query_dcqcn_state(*iface);
    assert(state == cntp::DcqcnState::BackendUnavailable);
    auto verify = cntp::verify_dcqcn_active(*iface);
    assert(!verify.has_value());
    assert(verify.error() == cntp::RoceError::DcqcnStatusUnavailable);

    crucible::test::pass("  test_apply_paths_are_stubbed: PASSED\n");
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
    crucible::test::pass("test_cntp_roce_config: all PASSED\n");
    return 0;
}

#pragma GCC diagnostic pop
