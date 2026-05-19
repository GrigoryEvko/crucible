#include <crucible/cntp/RoceConfig.h>

#include <cassert>
#include <cstdio>
#include <string_view>
#include <type_traits>

namespace cntp = crucible::cntp;
namespace saf = crucible::safety;

namespace {

void test_admission_and_names() {
    assert(cntp::roce_error_name(cntp::RoceError::InvalidDscp) ==
           std::string_view{"InvalidDscp"});

    auto pfc = cntp::admit_pfc_priorities(0b0000'1000);
    assert(pfc.has_value());
    assert(pfc->value() == 0b0000'1000);

    auto zero_pfc = cntp::admit_pfc_priorities(0);
    assert(!zero_pfc.has_value());
    assert(zero_pfc.error() == cntp::RoceError::InvalidPfcPriorityMask);

    auto dscp = cntp::admit_roce_dscp(26);
    assert(dscp.has_value());
    assert(dscp->value() == 26);

    auto invalid_dscp = cntp::admit_roce_dscp(64);
    assert(!invalid_dscp.has_value());
    assert(invalid_dscp.error() == cntp::RoceError::InvalidDscp);

    auto alpha = cntp::admit_dcqcn_alpha_ppm(500'000);
    assert(alpha.has_value());
    assert(alpha->value() == 500'000);

    auto alpha_zero = cntp::admit_dcqcn_alpha_ppm(0);
    assert(!alpha_zero.has_value());
    assert(alpha_zero.error() == cntp::RoceError::InvalidDcqcnAlpha);

    auto target = cntp::admit_dcqcn_target_packets(5);
    assert(target.has_value());
    assert(target->value() == 5);

    auto target_zero = cntp::admit_dcqcn_target_packets(0);
    assert(!target_zero.has_value());
    assert(target_zero.error() == cntp::RoceError::InvalidDcqcnTargetPackets);

    auto ce = cntp::admit_dcqcn_ce_threshold_bytes(64 * 1024);
    assert(ce.has_value());
    assert(ce->value() == 64 * 1024);

    std::printf("  test_admission_and_names: PASSED\n");
}

void test_config_minting_and_validation() {
    auto iface = cntp::NicInterfaceName::from("eth0");
    assert(iface.has_value());

    auto config = cntp::mint_roce_config<0b0000'1000, 26>(*iface);
    static_assert(std::same_as<decltype(config), cntp::DeclaredRoceConfig>);
    assert(config.value().interface.view() == "eth0");
    assert(config.value().enable_pfc);
    assert(config.value().pfc_priorities.value() == 0b0000'1000);
    assert(config.value().trust_dscp);
    assert(config.value().enable_ecn);
    assert(config.value().enable_dcqcn);
    assert(config.value().roce_dscp.value() == 26);
    assert(config.value().dcqcn.alpha_ppm.value() == 500'000);
    assert(config.value().dcqcn.target_packets.value() == 5);

    auto valid = cntp::validate_roce_config(config);
    assert(valid.has_value());

    auto apply = cntp::apply_roce_config(config);
    assert(!apply.has_value());
    assert(apply.error() == cntp::RoceError::PrivilegedApplyDeferred);

    auto privileged = cntp::mint_roce_config<0b0000'1000, 26>(
        *iface, cntp::DcqcnParams{}, true);
    auto privileged_apply = cntp::apply_roce_config(privileged);
    assert(!privileged_apply.has_value());
    assert(privileged_apply.error() == cntp::RoceError::VendorBackendUnavailable);

    std::printf("  test_config_minting_and_validation: PASSED\n");
}

void test_pause_counter_parse() {
    auto parsed = cntp::parse_pfc_pause_counters(" 17\n", "23\n");
    assert(parsed.has_value());
    assert(parsed->rx_pause_frames == 17);
    assert(parsed->tx_pause_frames == 23);

    auto bad = cntp::parse_pfc_pause_counters("17x", "23\n");
    assert(!bad.has_value());
    assert(bad.error() == cntp::RoceError::CounterParseFailed);

    std::printf("  test_pause_counter_parse: PASSED\n");
}

void test_live_surfaces_if_available() {
    auto lo = cntp::NicInterfaceName::from("lo");
    assert(lo.has_value());

    auto counters = cntp::query_pfc_pause_counters(*lo);
    if (!counters.has_value()) {
        assert(counters.error() == cntp::RoceError::CounterUnavailable ||
               counters.error() == cntp::RoceError::CounterParseFailed);
        std::printf("  test_live_pfc_pause_counters: SKIPPED\n");
    } else {
        std::printf("  test_live_pfc_pause_counters: PASSED\n");
    }

    // fixy-A5-042 regression: query_dcqcn_state must return the
    // explicit `BackendUnavailable` discriminator (NOT Inactive)
    // when no vendor probe has shipped — caller flow distinguishes
    // "NIC says off" from "we don't know".
    auto state = cntp::query_dcqcn_state(*lo);
    assert(state == cntp::DcqcnState::BackendUnavailable);
    assert(cntp::dcqcn_state_name(state) ==
           std::string_view{"BackendUnavailable"});
    assert(cntp::dcqcn_state_name(cntp::DcqcnState::Inactive) ==
           std::string_view{"Inactive"});
    assert(cntp::dcqcn_state_name(cntp::DcqcnState::Active) ==
           std::string_view{"Active"});

    // Back-compat: verify_dcqcn_active maps the explicit-unknown
    // state to the legacy error code so existing callers keep
    // their pattern.
    auto dcqcn = cntp::verify_dcqcn_active(*lo);
    assert(!dcqcn.has_value());
    assert(dcqcn.error() == cntp::RoceError::DcqcnStatusUnavailable);

    std::printf("  test_live_surfaces_if_available: PASSED\n");
}

// fixy-A5-002 honesty-marker fixture.  Proves four machine-readable
// claims that together define "the RoCEv2 substrate currently does
// nothing privileged":
//
//   (a) the compile-time honesty marker `privileged_apply_implemented`
//       is `false` — a backend author who wires a real Mellanox
//       mlxconfig / Broadcom bnxt_re installer MUST flip this in
//       lockstep, or the static_assert in main() reds;
//   (b) apply_roce_config with allow_privileged_apply=false returns
//       PrivilegedApplyDeferred — proving the substrate honestly
//       advertises "not attempted" rather than fabricating success;
//   (c) apply_roce_config with allow_privileged_apply=true returns
//       VendorBackendUnavailable — proving the privileged path is
//       genuinely absent (NOT a silent admit-and-do-nothing);
//   (d) the DCQCN read side (query_dcqcn_state / verify_dcqcn_active)
//       returns BackendUnavailable / DcqcnStatusUnavailable — proving
//       the read side is structurally stubbed in lockstep with the
//       write side, NOT fabricating an "Inactive" answer.  This
//       composes with the existing fixy-A5-042 invariant.
//
// When a vendor-policy installer lands, the migration is:
//   (1) flip cntp::privileged_apply_implemented to true,
//   (2) replace this fixture's "stub-returns-Deferred" assertions
//       with live-NIC fixtures that exercise mlxconfig / bnxt_re
//       privileged paths against a probe NIC,
//   (3) ship per-vendor DCQCN probe so query_dcqcn_state returns
//       Active / Inactive rather than BackendUnavailable.
// Tracked by FIXY-U-087.
void test_apply_paths_are_stubbed() {
    static_assert(cntp::privileged_apply_implemented == false,
        "fixy-A5-002: RoCEv2 apply paths are substrate stubs.  "
        "Flipping privileged_apply_implemented to true requires "
        "(a) a vendor-policy installer (Mellanox mlxconfig / "
        "Broadcom bnxt_re), (b) test_apply_paths_are_stubbed "
        "replaced with live-NIC fixtures, (c) per-vendor DCQCN "
        "probe wired, and (d) FIXY-U-087 sweep audit.");
    static_assert(std::is_same_v<decltype(cntp::privileged_apply_implemented),
                                 const bool>,
        "fixy-A5-002: honesty trait must be a compile-time bool");

    auto iface = cntp::NicInterfaceName::from("eth0");
    assert(iface.has_value());

    // (b) Deferred path: allow_privileged_apply defaults to false.
    auto deferred = cntp::mint_roce_config<0b0000'1000, 26>(*iface);
    auto deferred_apply = cntp::apply_roce_config(deferred);
    assert(!deferred_apply.has_value());
    assert(deferred_apply.error() == cntp::RoceError::PrivilegedApplyDeferred);

    // (c) Backend-unavailable path: allow_privileged_apply=true.
    auto requested = cntp::mint_roce_config<0b0000'1000, 26>(
        *iface, cntp::DcqcnParams{}, true);
    auto requested_apply = cntp::apply_roce_config(requested);
    assert(!requested_apply.has_value());
    assert(requested_apply.error()
           == cntp::RoceError::VendorBackendUnavailable);

    // (d) DCQCN read side stubbed in lockstep with write side.
    auto state = cntp::query_dcqcn_state(*iface);
    assert(state == cntp::DcqcnState::BackendUnavailable);
    auto verify = cntp::verify_dcqcn_active(*iface);
    assert(!verify.has_value());
    assert(verify.error() == cntp::RoceError::DcqcnStatusUnavailable);

    std::printf("  test_apply_paths_are_stubbed: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(cntp::PfcPriorityMask) == sizeof(std::uint8_t));
    static_assert(sizeof(cntp::RoceDscp) == sizeof(std::uint8_t));
    static_assert(sizeof(cntp::DeclaredRoceConfig) == sizeof(cntp::RoceConfig));
    static_assert(cntp::ValidPfcPriorityMask<0b0000'1000>);
    static_assert(!cntp::ValidPfcPriorityMask<0>);
    static_assert(cntp::ValidRoceDscp<26>);
    static_assert(!cntp::ValidRoceDscp<64>);
    static_assert(std::same_as<
                  cntp::DeclaredRoceConfig::tag_type,
                  saf::source::RoceConfig>);
    static_assert(std::is_trivially_copyable_v<cntp::DcqcnParams>);
    static_assert(std::is_trivially_copyable_v<cntp::RoceConfig>);

    static_assert(!cntp::privileged_apply_implemented,
        "fixy-A5-002: RoCEv2 substrate is documented stub — every "
        "apply_roce_config path returns PrivilegedApplyDeferred or "
        "VendorBackendUnavailable; verify_dcqcn_active returns "
        "DcqcnStatusUnavailable.  Flipping the trait to true requires "
        "(a) a vendor-policy installer, (b) live-NIC fixtures "
        "replacing test_apply_paths_are_stubbed, (c) per-vendor "
        "DCQCN probe wired, and (d) FIXY-U-087 sweep audit.");

    std::printf("test_cntp_roce_config:\n");
    test_admission_and_names();
    test_config_minting_and_validation();
    test_pause_counter_parse();
    test_live_surfaces_if_available();
    test_apply_paths_are_stubbed();
    std::printf("test_cntp_roce_config: all PASSED\n");
    return 0;
}
