#pragma once

// GAPS-125.  RoCEv2 lossless-fabric configuration substrate.
//
// This header owns typed admission and read-only verification for RoCEv2
// PFC/ECN/DCQCN intent.  It deliberately does not invoke vendor tools,
// mutate sysfs, install switch policy, or claim DCQCN liveness from
// unavailable evidence; those are NicConfig/operator-policy tasks.

#include <crucible/cntp/Pacing.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/RefinedAlgebra.h>
#include <crucible/safety/Tagged.h>

#include <cstdint>
#include <expected>
#include <string_view>
#include <type_traits>

namespace crucible::cntp {

// fixy-A5-002 honesty marker.  Live tier = admission + RoCEv2 typed
// validation (PFC mask / DSCP / DCQCN alpha + target / CE threshold) +
// /proc-counter reads; stub tier = privileged apply paths (sysfs / vendor
// tool mutation) which currently return PrivilegedApplyDeferred or
// VendorBackendUnavailable, and `verify_dcqcn_active` which returns
// DcqcnState::BackendUnavailable (per fixy-A5-042, an honest "no live
// evidence" sentinel — NOT a fabricated Inactive).  Flipping
// `privileged_apply_implemented` to true requires a vendor-policy
// installer (Mellanox mlxconfig / Broadcom bnxt_re tools) + a lockstep
// update to test_cntp_roce_config::test_apply_paths_are_stubbed.
// Tracked by FIXY-U-087.
inline constexpr bool privileged_apply_implemented = false;

enum class RoceError : std::uint8_t {
    InvalidPfcPriorityMask,
    InvalidDscp,
    InvalidDcqcnAlpha,
    InvalidDcqcnTargetPackets,
    InvalidCeThresholdBytes,
    CounterUnavailable,
    CounterParseFailed,
    PrivilegedApplyDeferred,
    VendorBackendUnavailable,
    DcqcnStatusUnavailable,
};

[[nodiscard]] std::string_view roce_error_name(RoceError error) noexcept;

template <std::uint8_t Mask>
concept ValidPfcPriorityMask = Mask != 0u;

template <std::uint8_t Dscp>
concept ValidRoceDscp = Dscp <= 63u;

using PfcPriorityMask = safety::Refined<safety::non_zero, std::uint8_t>;
using RoceDscp = safety::Bounded<std::uint8_t{0}, std::uint8_t{63}, std::uint8_t>;
using DcqcnAlphaPpm =
    safety::Bounded<std::uint32_t{1}, std::uint32_t{1'000'000}, std::uint32_t>;
using DcqcnTargetPackets = safety::Positive<std::uint16_t>;
using DcqcnCeThresholdBytes = safety::Positive<std::uint32_t>;

struct DcqcnParams {
    DcqcnAlphaPpm alpha_ppm{std::uint32_t{500'000}};
    DcqcnTargetPackets target_packets{std::uint16_t{5}};
    DcqcnCeThresholdBytes ce_threshold_bytes{std::uint32_t{64 * 1024}};
};

struct RoceConfig {
    NicInterfaceName interface{};
    bool enable_pfc = true;
    PfcPriorityMask pfc_priorities{std::uint8_t{0b0000'1000}};
    bool trust_dscp = true;
    bool enable_ecn = true;
    bool enable_dcqcn = true;
    DcqcnParams dcqcn{};
    RoceDscp roce_dscp{std::uint8_t{26}};
    bool allow_privileged_apply = false;
};

using DeclaredRoceConfig =
    safety::Tagged<RoceConfig, safety::source::RoceConfig>;

struct PfcPauseStats {
    std::uint64_t rx_pause_frames = 0;
    std::uint64_t tx_pause_frames = 0;
};

[[nodiscard]] constexpr std::expected<PfcPriorityMask, RoceError>
admit_pfc_priorities(std::uint8_t mask) noexcept {
    if (mask == 0u) {
        return std::unexpected(RoceError::InvalidPfcPriorityMask);
    }
    return PfcPriorityMask{mask, typename PfcPriorityMask::Trusted{}};
}

[[nodiscard]] constexpr std::expected<RoceDscp, RoceError>
admit_roce_dscp(std::uint8_t dscp) noexcept {
    if (dscp > 63u) {
        return std::unexpected(RoceError::InvalidDscp);
    }
    return RoceDscp{dscp, typename RoceDscp::Trusted{}};
}

[[nodiscard]] constexpr std::expected<DcqcnAlphaPpm, RoceError>
admit_dcqcn_alpha_ppm(std::uint32_t alpha_ppm) noexcept {
    if (alpha_ppm == 0u || alpha_ppm > 1'000'000u) {
        return std::unexpected(RoceError::InvalidDcqcnAlpha);
    }
    return DcqcnAlphaPpm{alpha_ppm, typename DcqcnAlphaPpm::Trusted{}};
}

[[nodiscard]] constexpr std::expected<DcqcnTargetPackets, RoceError>
admit_dcqcn_target_packets(std::uint16_t packets) noexcept {
    if (packets == 0u) {
        return std::unexpected(RoceError::InvalidDcqcnTargetPackets);
    }
    return DcqcnTargetPackets{
        packets, typename DcqcnTargetPackets::Trusted{}};
}

[[nodiscard]] constexpr std::expected<DcqcnCeThresholdBytes, RoceError>
admit_dcqcn_ce_threshold_bytes(std::uint32_t bytes) noexcept {
    if (bytes == 0u) {
        return std::unexpected(RoceError::InvalidCeThresholdBytes);
    }
    return DcqcnCeThresholdBytes{
        bytes, typename DcqcnCeThresholdBytes::Trusted{}};
}

template <std::uint8_t PfcPriorities = 0b0000'1000,
          std::uint8_t Dscp = 26>
    requires ValidPfcPriorityMask<PfcPriorities> && ValidRoceDscp<Dscp>
[[nodiscard]] constexpr DeclaredRoceConfig
mint_roce_config(NicInterfaceName iface,
                 DcqcnParams dcqcn = {},
                 bool allow_privileged_apply = false) noexcept {
    return DeclaredRoceConfig{RoceConfig{
        .interface = iface,
        .enable_pfc = true,
        .pfc_priorities = PfcPriorityMask{
            PfcPriorities, typename PfcPriorityMask::Trusted{}},
        .trust_dscp = true,
        .enable_ecn = true,
        .enable_dcqcn = true,
        .dcqcn = dcqcn,
        .roce_dscp = RoceDscp{Dscp, typename RoceDscp::Trusted{}},
        .allow_privileged_apply = allow_privileged_apply,
    }};
}

[[nodiscard]] constexpr std::expected<void, RoceError>
validate_roce_config(DeclaredRoceConfig config) noexcept {
    auto const& raw = config.value();
    if (raw.enable_pfc && raw.pfc_priorities.value() == 0u) {
        return std::unexpected(RoceError::InvalidPfcPriorityMask);
    }
    if (raw.roce_dscp.value() > 63u) {
        return std::unexpected(RoceError::InvalidDscp);
    }
    return {};
}

// FIXY-U-087: stub-vs-live deprecation discipline.  `apply_roce_config` is
// a STUB (see `privileged_apply_implemented = false`).
// `parse_pfc_pause_counters` and `query_pfc_pause_counters` are LIVE — they
// parse / read /proc-counter text and ship today.  Authorized stub callers
// (`test/test_cntp_roce_config.cpp`, `test/safety_neg/neg_roce_raw_config_apply.cpp`)
// suppress the warning with `#pragma GCC diagnostic push/ignored
// "-Wdeprecated-declarations"/pop`.
[[nodiscard, deprecated("CRUCIBLE_STUB: privileged sysfs/vendor-tool RoCEv2 "
    "policy install (Mellanox mlxconfig / Broadcom bnxt_re) not yet attached; "
    "returns PrivilegedApplyDeferred or VendorBackendUnavailable; see "
    "fixy-A5-002 / FIXY-U-087")]]
std::expected<void, RoceError>
apply_roce_config(DeclaredRoceConfig config) noexcept;

[[nodiscard]] std::expected<PfcPauseStats, RoceError>
parse_pfc_pause_counters(std::string_view rx_text,
                         std::string_view tx_text) noexcept;

[[nodiscard]] std::expected<PfcPauseStats, RoceError>
query_pfc_pause_counters(NicInterfaceName iface) noexcept;

// fixy-A5-042: explicit unknown discriminator.  Pre-fix
// `verify_dcqcn_active` returned `std::expected<bool, RoceError>`
// which forced callers to treat the "no backend can answer" case as
// an error — indistinguishable in caller flow from "the NIC says
// DCQCN is genuinely inactive".  `DcqcnState::BackendUnavailable`
// is the explicit unknown; callers can branch on it without
// pattern-matching on an error code.
enum class DcqcnState : std::uint8_t {
    BackendUnavailable,
    Inactive,
    Active,
};

[[nodiscard]] std::string_view dcqcn_state_name(DcqcnState state) noexcept;

// Returns the queried DCQCN state.  `BackendUnavailable` means no
// vendor-specific probe succeeded for this interface — callers
// should treat it as "unknown" rather than "off".  No allocation,
// no errno propagation; vendor sysfs / ethtool probes land here as
// they ship per-vendor.  FIXY-U-087: STUB until a vendor probe ships
// (today the body returns `DcqcnState::BackendUnavailable` for every
// interface — the explicit-unknown sentinel of fixy-A5-042).
[[nodiscard, deprecated("CRUCIBLE_STUB: DCQCN state probe (vendor sysfs / "
    "ethtool) not yet wired; returns DcqcnState::BackendUnavailable; see "
    "fixy-A5-002 / fixy-A5-042 / FIXY-U-087")]]
DcqcnState
query_dcqcn_state(NicInterfaceName iface) noexcept;

// Pure mapping from queried state to the legacy `bool, RoceError`
// expected shape.  Extracted from `verify_dcqcn_active` so the
// back-compat contract is testable at compile time without
// requiring a working backend — the Active→true / Inactive→false
// branches stay dead until a vendor probe ships, but the mapping
// must remain correct for that future ship-day.
[[nodiscard]] constexpr std::expected<bool, RoceError>
dcqcn_state_to_bool(DcqcnState state) noexcept {
    switch (state) {
        case DcqcnState::Active:   return true;
        case DcqcnState::Inactive: return false;
        case DcqcnState::BackendUnavailable:
        default:
            return std::unexpected(RoceError::DcqcnStatusUnavailable);
    }
}

// Back-compat thin wrapper over `query_dcqcn_state`.  Returns
// `unexpected(DcqcnStatusUnavailable)` for the no-backend case so
// existing call sites keep their error path; new code should
// prefer `query_dcqcn_state` and branch on `DcqcnState` directly.
// FIXY-U-087: STUB by composition — chains on `query_dcqcn_state` (above),
// so the only attainable return today is
// `unexpected(DcqcnStatusUnavailable)`.
[[nodiscard, deprecated("CRUCIBLE_STUB: DCQCN active-state verification not "
    "yet wired (transitively, via query_dcqcn_state); returns "
    "RoceError::DcqcnStatusUnavailable; see fixy-A5-002 / fixy-A5-042 / "
    "FIXY-U-087")]]
std::expected<bool, RoceError>
verify_dcqcn_active(NicInterfaceName iface) noexcept;

// fixy-A5-042 follow-up: prove the back-compat mapping covers all
// three DcqcnState discriminators correctly.  Branches stay dead
// until a vendor probe ships; the static_assert is the
// regression net for the ship-day mapping.
static_assert(dcqcn_state_to_bool(DcqcnState::Active).value() == true);
static_assert(dcqcn_state_to_bool(DcqcnState::Inactive).value() == false);
static_assert(!dcqcn_state_to_bool(DcqcnState::BackendUnavailable).has_value());
static_assert(dcqcn_state_to_bool(DcqcnState::BackendUnavailable).error() ==
              RoceError::DcqcnStatusUnavailable);

static_assert(sizeof(PfcPriorityMask) == sizeof(std::uint8_t));
static_assert(sizeof(RoceDscp) == sizeof(std::uint8_t));
static_assert(sizeof(DcqcnAlphaPpm) == sizeof(std::uint32_t));
static_assert(sizeof(DcqcnTargetPackets) == sizeof(std::uint16_t));
static_assert(sizeof(DcqcnCeThresholdBytes) == sizeof(std::uint32_t));
static_assert(sizeof(DeclaredRoceConfig) == sizeof(RoceConfig));
static_assert(std::is_trivially_copyable_v<DcqcnParams>);
static_assert(std::is_trivially_copyable_v<RoceConfig>);
static_assert(std::is_trivially_copyable_v<PfcPauseStats>);
static_assert(ValidPfcPriorityMask<0b0000'1000>);
static_assert(!ValidPfcPriorityMask<0>);
static_assert(ValidRoceDscp<26>);
static_assert(!ValidRoceDscp<64>);

}  // namespace crucible::cntp
