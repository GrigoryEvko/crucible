#pragma once

// RoCEv2 policy for one interface: the PFC priorities, the DSCP that RoCE
// traffic carries, and the DCQCN parameters.  Typed admission, the config
// mint and the pause-counter reads are real.  Nothing installs the policy.

#include <crucible/cntp/Pacing.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/os/Fs.h>
#include <foundation/effects/Ctx.h>
#include <foundation/os/File.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace crucible::cntp {

// False: no privileged path mutates sysfs or drives a vendor tool, so
// apply_roce_config installs no policy.  The DCQCN probe reports
// BackendUnavailable, which means no evidence, not off.
inline constexpr bool privileged_apply_implemented = false;

// An error has no spelling of its own, so its name is its enumerator's
// identifier, read by foundation::reflect::enum_name.
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

// The bounds, stated once.  The refined types, the admission doors, the
// template clauses of mint_roce_config and validate_roce_config all read
// these predicates, so a bound cannot change in one place and not the
// others.  A PFC mask must select a priority, a DSCP is a 6-bit value, and
// the DCQCN alpha is a fraction in parts per million that is not zero.
inline constexpr auto pfc_priority_mask = ::fixy::non_zero;
inline constexpr auto roce_dscp_bits = ::fixy::bounded_above<std::uint8_t{63}>;
inline constexpr auto dcqcn_alpha_ppm = ::fixy::in_range<std::uint32_t{1}, std::uint32_t{1000000}>;

template <std::uint8_t Mask>
concept ValidPfcPriorityMask = pfc_priority_mask(Mask);

template <std::uint8_t Dscp>
concept ValidRoceDscp = roce_dscp_bits(Dscp);

using PfcPriorityMask = ::fixy::Refined<pfc_priority_mask, std::uint8_t>;
using RoceDscp = ::fixy::Refined<roce_dscp_bits, std::uint8_t>;
using DcqcnAlphaPpm = ::fixy::Refined<dcqcn_alpha_ppm, std::uint32_t>;
using DcqcnTargetPackets = ::fixy::Positive<std::uint16_t>;
using DcqcnCeThresholdBytes = ::fixy::Positive<std::uint32_t>;

struct DcqcnParams {
    DcqcnAlphaPpm alpha_ppm = ::fixy::mint_refined<dcqcn_alpha_ppm>(std::uint32_t{500000});
    DcqcnTargetPackets target_packets = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{5});
    DcqcnCeThresholdBytes ce_threshold_bytes = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{64 * 1024});
};

struct RoceConfig {
    NicInterfaceName interface{};
    bool enable_pfc = true;
    PfcPriorityMask pfc_priorities = ::fixy::mint_refined<pfc_priority_mask>(std::uint8_t{0b00001000});
    bool trust_dscp = true;
    bool enable_ecn = true;
    bool enable_dcqcn = true;
    DcqcnParams dcqcn{};
    RoceDscp roce_dscp = ::fixy::mint_refined<roce_dscp_bits>(std::uint8_t{26});
    bool allow_privileged_apply = false;
};

using DeclaredRoceConfig = ::fixy::Tagged<RoceConfig, ::fixy::tags::source::RoceConfig>;

struct PfcPauseStats {
    std::uint64_t rx_pause_frames = 0;
    std::uint64_t tx_pause_frames = 0;
};

namespace detail::roce {

[[nodiscard]] constexpr bool is_space(char c) noexcept { return c == ' ' || c == '\n' || c == '\t' || c == '\r'; }

// A counter is decimal digits with optional white space around them.  A
// value past the range of 64 bits is refused, not wrapped.  O(n) in the
// length of the text.
[[nodiscard]] constexpr ::fixy::Result<std::uint64_t, RoceError> parse_counter(::fixy::TextView text) noexcept {
    std::uint64_t value = 0;
    std::size_t digit_count = 0;
    bool is_past_digits = false;
    for (char const character : text) {
        bool const is_digit = character >= '0' && character <= '9';
        if (is_digit && !is_past_digits) {
            auto const digit = static_cast<std::uint64_t>(character - '0');
            if (value > (::fixy::max_of<std::uint64_t> - digit) / 10u) {
                return ::fixy::err(RoceError::CounterParseFailed);
            }
            value = value * 10u + digit;
            ++digit_count;
        } else if (is_space(character)) {
            is_past_digits = digit_count != 0;
        } else {
            return ::fixy::err(RoceError::CounterParseFailed);
        }
    }
    if (digit_count == 0) {
        return ::fixy::err(RoceError::CounterParseFailed);
    }
    return value;
}

inline constexpr ::fixy::TextView rx_pause_leaf = "statistics/rx_pause_frames";
inline constexpr ::fixy::TextView tx_pause_leaf = "statistics/tx_pause_frames";

// A counter holds at most twenty digits and a newline, so a read that fills
// the text is not a counter, and it is refused rather than cut short.
inline constexpr std::size_t counter_read_bytes = 64;

// Reads one statistics counter of the interface through the file door of
// the base.  The door takes no context, so the gate of this function holds
// the row of the read.  The path stays in the sysfs directory of the
// interface, because a NicInterfaceName is not "." or "..".
template <::foundation::effects::IsExecCtx Ctx>
    requires ::fixy::fs::CtxFitsFileMint<Ctx, ProcFileReadMode>
[[nodiscard]] ::fixy::Result<std::uint64_t, RoceError> read_counter(Ctx const&, NicInterfaceName iface,
                                                                    ::fixy::TextView leaf) noexcept {
    ::crucible::cntp::detail::InterfacePath const path = ::crucible::cntp::detail::interface_path_of(iface, leaf);
    ::fixy::FixedText<counter_read_bytes> text{};
    if (::foundation::os::read_file(path.c_text(), text).is_err() || text.size() == 0) {
        return ::fixy::err(RoceError::CounterUnavailable);
    }
    if (text.size() == counter_read_bytes) {
        return ::fixy::err(RoceError::CounterParseFailed);
    }
    return parse_counter(text.view());
}

}  // namespace detail::roce

[[nodiscard]] constexpr ::fixy::Result<PfcPriorityMask, RoceError> admit_pfc_priorities(std::uint8_t mask) noexcept {
    return ::fixy::admit_refined<pfc_priority_mask>(mask, RoceError::InvalidPfcPriorityMask);
}

[[nodiscard]] constexpr ::fixy::Result<RoceDscp, RoceError> admit_roce_dscp(std::uint8_t dscp) noexcept {
    return ::fixy::admit_refined<roce_dscp_bits>(dscp, RoceError::InvalidDscp);
}

[[nodiscard]] constexpr ::fixy::Result<DcqcnAlphaPpm, RoceError>
admit_dcqcn_alpha_ppm(std::uint32_t alpha_ppm) noexcept {
    return ::fixy::admit_refined<dcqcn_alpha_ppm>(alpha_ppm, RoceError::InvalidDcqcnAlpha);
}

[[nodiscard]] constexpr ::fixy::Result<DcqcnTargetPackets, RoceError>
admit_dcqcn_target_packets(std::uint16_t packets) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(packets, RoceError::InvalidDcqcnTargetPackets);
}

[[nodiscard]] constexpr ::fixy::Result<DcqcnCeThresholdBytes, RoceError>
admit_dcqcn_ce_threshold_bytes(std::uint32_t bytes) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(bytes, RoceError::InvalidCeThresholdBytes);
}

template <std::uint8_t PfcPriorities = 0b00001000, std::uint8_t Dscp = 26>
    requires ValidPfcPriorityMask<PfcPriorities> && ValidRoceDscp<Dscp>
[[nodiscard]] constexpr DeclaredRoceConfig mint_roce_config(NicInterfaceName iface, DcqcnParams dcqcn = {},
                                                            bool allow_privileged_apply = false) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::RoceConfig>(RoceConfig{
        .interface = iface,
        .enable_pfc = true,
        .pfc_priorities = ::fixy::mint_refined<pfc_priority_mask>(PfcPriorities),
        .trust_dscp = true,
        .enable_ecn = true,
        .enable_dcqcn = true,
        .dcqcn = dcqcn,
        .roce_dscp = ::fixy::mint_refined<roce_dscp_bits>(Dscp),
        .allow_privileged_apply = allow_privileged_apply,
    });
}

// The refined fields already hold these bounds.  The check reads the same
// predicates again, so a value that entered through mint_refined_trusted is
// still refused here.
[[nodiscard]] constexpr ::fixy::Result<::fixy::Unit, RoceError>
validate_roce_config(DeclaredRoceConfig const& config) noexcept {
    auto const& raw = config.value();
    if (raw.enable_pfc && !pfc_priority_mask(raw.pfc_priorities.value())) {
        return ::fixy::err(RoceError::InvalidPfcPriorityMask);
    }
    if (!roce_dscp_bits(raw.roce_dscp.value())) {
        return ::fixy::err(RoceError::InvalidDscp);
    }
    return ::fixy::Unit{};
}

// Carries [[deprecated]] not because it is going away but because the
// attribute makes every call site warn, so a stub cannot be reached without
// notice at compile time.  The pause-counter functions below are real and
// carry no such attribute.
[[nodiscard, deprecated("CRUCIBLE_STUB: no sysfs or vendor tool installs RoCEv2 "
                        "policy. Returns PrivilegedApplyDeferred or VendorBackendUnavailable")]]
constexpr ::fixy::Result<::fixy::Unit, RoceError> apply_roce_config(DeclaredRoceConfig const& config) noexcept {
    CRUCIBLE_TRY(_, validate_roce_config(config));
    if (!config.value().allow_privileged_apply) {
        return ::fixy::err(RoceError::PrivilegedApplyDeferred);
    }
    return ::fixy::err(RoceError::VendorBackendUnavailable);
}

[[nodiscard]] constexpr ::fixy::Result<PfcPauseStats, RoceError>
parse_pfc_pause_counters(::fixy::TextView rx_text, ::fixy::TextView tx_text) noexcept {
    CRUCIBLE_TRY(rx, detail::roce::parse_counter(rx_text));
    CRUCIBLE_TRY(tx, detail::roce::parse_counter(tx_text));
    return PfcPauseStats{
        .rx_pause_frames = rx,
        .tx_pause_frames = tx,
    };
}

// The two counters are files under sysfs.  The open and the read of a file
// count as IO and Block, so only a context that carries both can read them.
template <::foundation::effects::IsExecCtx Ctx>
    requires ::fixy::fs::CtxFitsFileMint<Ctx, ProcFileReadMode>
[[nodiscard]] ::fixy::Result<PfcPauseStats, RoceError> query_pfc_pause_counters(Ctx const& ctx,
                                                                                NicInterfaceName iface) noexcept {
    CRUCIBLE_TRY(rx, detail::roce::read_counter(ctx, iface, detail::roce::rx_pause_leaf));
    CRUCIBLE_TRY(tx, detail::roce::read_counter(ctx, iface, detail::roce::tx_pause_leaf));
    return PfcPauseStats{
        .rx_pause_frames = rx,
        .tx_pause_frames = tx,
    };
}

// BackendUnavailable is an explicit unknown and is not the same fact as
// Inactive.  A caller that collapses the two reads "nobody could answer" as
// "the NIC says DCQCN is off".  The name of each state is its enumerator's
// identifier.
enum class DcqcnState : std::uint8_t {
    BackendUnavailable,
    Inactive,
    Active,
};

// No vendor probe is wired, so this answers BackendUnavailable for every
// interface.  A caller must read that as unknown, not as off.
[[nodiscard, deprecated("CRUCIBLE_STUB: no vendor sysfs or ethtool probe reads the "
                        "DCQCN state. Returns DcqcnState::BackendUnavailable")]]
constexpr DcqcnState query_dcqcn_state(NicInterfaceName iface) noexcept {
    static_cast<void>(iface);
    return DcqcnState::BackendUnavailable;
}

// Separate from verify_dcqcn_active so the mapping is checkable without a
// working backend.  The Active and Inactive arms are unreachable while no
// probe exists, and the static_asserts below hold them correct.
[[nodiscard]] constexpr ::fixy::Result<bool, RoceError> dcqcn_state_to_bool(DcqcnState state) noexcept {
    switch (state) {
        case DcqcnState::Active:
            return true;
        case DcqcnState::Inactive:
            return false;
        case DcqcnState::BackendUnavailable:
        default:
            return ::fixy::err(RoceError::DcqcnStatusUnavailable);
    }
}

// A thin wrapper over query_dcqcn_state that folds the unknown back into an
// error, so a caller written against a boolean keeps its error path.  New
// code calls query_dcqcn_state and branches on the state instead.
[[nodiscard, deprecated("CRUCIBLE_STUB: this chains on query_dcqcn_state, which reads "
                        "nothing, so it always returns RoceError::DcqcnStatusUnavailable")]]
constexpr ::fixy::Result<bool, RoceError> verify_dcqcn_active(NicInterfaceName iface) noexcept {
    // The suppression covers this one call, so a caller outside this
    // function still sees the warning at its own call site.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    return dcqcn_state_to_bool(query_dcqcn_state(iface));
#pragma GCC diagnostic pop
}

}  // namespace crucible::cntp
