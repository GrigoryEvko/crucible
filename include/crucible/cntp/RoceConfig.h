#pragma once

#include <crucible/cntp/Pacing.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/RefinedAlgebra.h>
#include <crucible/safety/Tagged.h>

#include <cstdint>
#include <expected>
#include <string_view>
#include <type_traits>

namespace crucible::cntp {

// False: no privileged path mutates sysfs or drives a vendor tool, so
// apply_roce_config installs no policy.  Typed admission, config validation
// and the pause-counter reads are real.  The DCQCN probe reports
// BackendUnavailable, which means no evidence, not off.
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
using DcqcnAlphaPpm = safety::Bounded<std::uint32_t{1}, std::uint32_t{1000000}, std::uint32_t>;
using DcqcnTargetPackets = safety::Positive<std::uint16_t>;
using DcqcnCeThresholdBytes = safety::Positive<std::uint32_t>;

struct DcqcnParams {
    DcqcnAlphaPpm alpha_ppm{std::uint32_t{500000}};
    DcqcnTargetPackets target_packets{std::uint16_t{5}};
    DcqcnCeThresholdBytes ce_threshold_bytes{std::uint32_t{64 * 1024}};
};

struct RoceConfig {
    NicInterfaceName interface{};
    bool enable_pfc = true;
    PfcPriorityMask pfc_priorities{std::uint8_t{0b00001000}};
    bool trust_dscp = true;
    bool enable_ecn = true;
    bool enable_dcqcn = true;
    DcqcnParams dcqcn{};
    RoceDscp roce_dscp{std::uint8_t{26}};
    bool allow_privileged_apply = false;
};

using DeclaredRoceConfig = safety::Tagged<RoceConfig, safety::source::RoceConfig>;

struct PfcPauseStats {
    std::uint64_t rx_pause_frames = 0;
    std::uint64_t tx_pause_frames = 0;
};

[[nodiscard]] constexpr std::expected<PfcPriorityMask, RoceError> admit_pfc_priorities(std::uint8_t mask) noexcept {
    if (mask == 0u) {
        return std::unexpected(RoceError::InvalidPfcPriorityMask);
    }
    return PfcPriorityMask{mask, typename PfcPriorityMask::Trusted{}};
}

[[nodiscard]] constexpr std::expected<RoceDscp, RoceError> admit_roce_dscp(std::uint8_t dscp) noexcept {
    if (dscp > 63u) {
        return std::unexpected(RoceError::InvalidDscp);
    }
    return RoceDscp{dscp, typename RoceDscp::Trusted{}};
}

[[nodiscard]] constexpr std::expected<DcqcnAlphaPpm, RoceError>
admit_dcqcn_alpha_ppm(std::uint32_t alpha_ppm) noexcept {
    if (alpha_ppm == 0u || alpha_ppm > 1000000u) {
        return std::unexpected(RoceError::InvalidDcqcnAlpha);
    }
    return DcqcnAlphaPpm{alpha_ppm, typename DcqcnAlphaPpm::Trusted{}};
}

[[nodiscard]] constexpr std::expected<DcqcnTargetPackets, RoceError>
admit_dcqcn_target_packets(std::uint16_t packets) noexcept {
    if (packets == 0u) {
        return std::unexpected(RoceError::InvalidDcqcnTargetPackets);
    }
    return DcqcnTargetPackets{packets, typename DcqcnTargetPackets::Trusted{}};
}

[[nodiscard]] constexpr std::expected<DcqcnCeThresholdBytes, RoceError>
admit_dcqcn_ce_threshold_bytes(std::uint32_t bytes) noexcept {
    if (bytes == 0u) {
        return std::unexpected(RoceError::InvalidCeThresholdBytes);
    }
    return DcqcnCeThresholdBytes{bytes, typename DcqcnCeThresholdBytes::Trusted{}};
}

template <std::uint8_t PfcPriorities = 0b00001000, std::uint8_t Dscp = 26>
    requires ValidPfcPriorityMask<PfcPriorities> && ValidRoceDscp<Dscp>
[[nodiscard]] constexpr DeclaredRoceConfig mint_roce_config(NicInterfaceName iface, DcqcnParams dcqcn = {},
                                                            bool allow_privileged_apply = false) noexcept {
    return DeclaredRoceConfig{RoceConfig{
        .interface = iface,
        .enable_pfc = true,
        .pfc_priorities = PfcPriorityMask{PfcPriorities, typename PfcPriorityMask::Trusted{}},
        .trust_dscp = true,
        .enable_ecn = true,
        .enable_dcqcn = true,
        .dcqcn = dcqcn,
        .roce_dscp = RoceDscp{Dscp, typename RoceDscp::Trusted{}},
        .allow_privileged_apply = allow_privileged_apply,
    }};
}

[[nodiscard]] constexpr std::expected<void, RoceError> validate_roce_config(DeclaredRoceConfig config) noexcept {
    auto const& raw = config.value();
    if (raw.enable_pfc && raw.pfc_priorities.value() == 0u) {
        return std::unexpected(RoceError::InvalidPfcPriorityMask);
    }
    if (raw.roce_dscp.value() > 63u) {
        return std::unexpected(RoceError::InvalidDscp);
    }
    return {};
}

// Carries [[deprecated]] not because it is going away but because the
// attribute makes every call site warn, so a stub cannot be reached without
// notice at compile time.  The two pause-counter functions below are real and
// carry no such attribute.
[[nodiscard, deprecated("CRUCIBLE_STUB: no sysfs or vendor tool installs RoCEv2 "
                        "policy. Returns PrivilegedApplyDeferred or VendorBackendUnavailable")]]
std::expected<void, RoceError> apply_roce_config(DeclaredRoceConfig config) noexcept;

[[nodiscard]] std::expected<PfcPauseStats, RoceError> parse_pfc_pause_counters(std::string_view rx_text,
                                                                               std::string_view tx_text) noexcept;

[[nodiscard]] std::expected<PfcPauseStats, RoceError> query_pfc_pause_counters(NicInterfaceName iface) noexcept;

// BackendUnavailable is an explicit unknown and is not the same fact as
// Inactive.  A caller that collapses the two reads "nobody could answer" as
// "the NIC says DCQCN is off".
enum class DcqcnState : std::uint8_t {
    BackendUnavailable,
    Inactive,
    Active,
};

[[nodiscard]] std::string_view dcqcn_state_name(DcqcnState state) noexcept;

// No vendor probe is wired, so this answers BackendUnavailable for every
// interface.  A caller must read that as unknown, not as off.
[[nodiscard, deprecated("CRUCIBLE_STUB: no vendor sysfs or ethtool probe reads the "
                        "DCQCN state. Returns DcqcnState::BackendUnavailable")]]
DcqcnState query_dcqcn_state(NicInterfaceName iface) noexcept;

// Separate from verify_dcqcn_active so the mapping is checkable without a
// working backend.  The Active and Inactive arms are unreachable while no
// probe exists, and the static_asserts below hold them correct.
[[nodiscard]] constexpr std::expected<bool, RoceError> dcqcn_state_to_bool(DcqcnState state) noexcept {
    switch (state) {
        case DcqcnState::Active:
            return true;
        case DcqcnState::Inactive:
            return false;
        case DcqcnState::BackendUnavailable:
        default:
            return std::unexpected(RoceError::DcqcnStatusUnavailable);
    }
}

// A thin wrapper over query_dcqcn_state that folds the unknown back into an
// error, so an older call site keeps its error path.  New code should call
// query_dcqcn_state and branch on the state instead.
[[nodiscard, deprecated("CRUCIBLE_STUB: this chains on query_dcqcn_state, which reads "
                        "nothing, so it always returns RoceError::DcqcnStatusUnavailable")]]
std::expected<bool, RoceError> verify_dcqcn_active(NicInterfaceName iface) noexcept;

static_assert(dcqcn_state_to_bool(DcqcnState::Active).value() == true);
static_assert(dcqcn_state_to_bool(DcqcnState::Inactive).value() == false);
static_assert(!dcqcn_state_to_bool(DcqcnState::BackendUnavailable).has_value());
static_assert(dcqcn_state_to_bool(DcqcnState::BackendUnavailable).error() == RoceError::DcqcnStatusUnavailable);

static_assert(sizeof(PfcPriorityMask) == sizeof(std::uint8_t));
static_assert(sizeof(RoceDscp) == sizeof(std::uint8_t));
static_assert(sizeof(DcqcnAlphaPpm) == sizeof(std::uint32_t));
static_assert(sizeof(DcqcnTargetPackets) == sizeof(std::uint16_t));
static_assert(sizeof(DcqcnCeThresholdBytes) == sizeof(std::uint32_t));
static_assert(sizeof(DeclaredRoceConfig) == sizeof(RoceConfig));
static_assert(std::is_trivially_copyable_v<DcqcnParams>);
static_assert(std::is_trivially_copyable_v<RoceConfig>);
static_assert(std::is_trivially_copyable_v<PfcPauseStats>);
static_assert(ValidPfcPriorityMask<0b00001000>);
static_assert(!ValidPfcPriorityMask<0>);
static_assert(ValidRoceDscp<26>);
static_assert(!ValidRoceDscp<64>);

}  // namespace crucible::cntp
