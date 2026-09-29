#pragma once

// RoCEv2 policy for one interface: the PFC priorities, the DSCP that RoCE
// traffic carries, and the DCQCN parameters.  Typed admission, the config
// mint and the pause-counter reads are real.  Nothing installs the policy.

#include <crucible/cntp/Pacing.h>
#include <fixy/Ctx.h>
#include <fixy/Path.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/os/Fs.h>
#include <foundation/effects/Ctx.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

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
[[nodiscard]] constexpr std::expected<std::uint64_t, RoceError> parse_counter(std::string_view text) noexcept {
    std::uint64_t value = 0;
    std::size_t pos = 0;
    while (pos < text.size() && is_space(text[pos])) {
        ++pos;
    }
    const std::size_t first_digit = pos;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
        const auto digit = static_cast<std::uint64_t>(text[pos] - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10u) {
            return std::unexpected(RoceError::CounterParseFailed);
        }
        value = value * 10u + digit;
        ++pos;
    }
    if (pos == first_digit) {
        return std::unexpected(RoceError::CounterParseFailed);
    }
    while (pos < text.size()) {
        if (!is_space(text[pos])) {
            return std::unexpected(RoceError::CounterParseFailed);
        }
        ++pos;
    }
    return value;
}

inline constexpr std::string_view sysfs_net_root = "/sys/class/net";
inline constexpr std::string_view rx_pause_leaf = "rx_pause_frames";
inline constexpr std::string_view tx_pause_leaf = "tx_pause_frames";

// A counter holds at most twenty digits and a newline, so a read that fills
// the buffer is not a counter, and it is refused rather than cut short.
inline constexpr std::size_t counter_read_bytes = 64;

// Reads one statistics counter of the interface through the fs door.  The
// path is held under the sysfs root, so an interface name of ".." or "."
// cannot reach a file outside the directory of its own interface.
template <::foundation::effects::IsExecCtx Ctx>
    requires ::fixy::fs::CtxFitsFileMint<Ctx, ProcFileReadMode>
[[nodiscard]] std::expected<std::uint64_t, RoceError> read_counter(Ctx const& ctx, NicInterfaceName iface,
                                                                   std::string_view leaf) noexcept {
    const std::filesystem::path root{sysfs_net_root};
    auto path = ::fixy::sanitize::path_traversal::sanitize_path_root_locked(
        ::fixy::mint_tagged<::fixy::tags::source::External>(root / iface.view() / "statistics" / leaf), root);
    if (!path.has_value()) {
        return std::unexpected(RoceError::CounterUnavailable);
    }
    auto opened = ::fixy::fs::mint_file<ProcFileReadMode>(ctx, std::move(*path));
    if (!opened.has_value()) {
        return std::unexpected(RoceError::CounterUnavailable);
    }
    const ::fixy::fs::OwnedFd fd = std::move(*opened).consume();
    std::array<char, counter_read_bytes> text{};
    auto nread = ::fixy::fs::read_full(ctx, fd, std::as_writable_bytes(std::span{text}));
    if (!nread.has_value() || *nread == 0) {
        return std::unexpected(RoceError::CounterUnavailable);
    }
    if (*nread == text.size()) {
        return std::unexpected(RoceError::CounterParseFailed);
    }
    return parse_counter(std::string_view{text.data(), *nread});
}

}  // namespace detail::roce

[[nodiscard]] constexpr std::expected<PfcPriorityMask, RoceError> admit_pfc_priorities(std::uint8_t mask) noexcept {
    return ::fixy::admit_refined<pfc_priority_mask>(mask, RoceError::InvalidPfcPriorityMask);
}

[[nodiscard]] constexpr std::expected<RoceDscp, RoceError> admit_roce_dscp(std::uint8_t dscp) noexcept {
    return ::fixy::admit_refined<roce_dscp_bits>(dscp, RoceError::InvalidDscp);
}

[[nodiscard]] constexpr std::expected<DcqcnAlphaPpm, RoceError>
admit_dcqcn_alpha_ppm(std::uint32_t alpha_ppm) noexcept {
    return ::fixy::admit_refined<dcqcn_alpha_ppm>(alpha_ppm, RoceError::InvalidDcqcnAlpha);
}

[[nodiscard]] constexpr std::expected<DcqcnTargetPackets, RoceError>
admit_dcqcn_target_packets(std::uint16_t packets) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(packets, RoceError::InvalidDcqcnTargetPackets);
}

[[nodiscard]] constexpr std::expected<DcqcnCeThresholdBytes, RoceError>
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
[[nodiscard]] constexpr std::expected<void, RoceError> validate_roce_config(DeclaredRoceConfig const& config) noexcept {
    auto const& raw = config.value();
    if (raw.enable_pfc && !pfc_priority_mask(raw.pfc_priorities.value())) {
        return std::unexpected(RoceError::InvalidPfcPriorityMask);
    }
    if (!roce_dscp_bits(raw.roce_dscp.value())) {
        return std::unexpected(RoceError::InvalidDscp);
    }
    return {};
}

// Carries [[deprecated]] not because it is going away but because the
// attribute makes every call site warn, so a stub cannot be reached without
// notice at compile time.  The pause-counter functions below are real and
// carry no such attribute.
[[nodiscard, deprecated("CRUCIBLE_STUB: no sysfs or vendor tool installs RoCEv2 "
                        "policy. Returns PrivilegedApplyDeferred or VendorBackendUnavailable")]]
constexpr std::expected<void, RoceError> apply_roce_config(DeclaredRoceConfig const& config) noexcept {
    auto valid = validate_roce_config(config);
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }
    if (!config.value().allow_privileged_apply) {
        return std::unexpected(RoceError::PrivilegedApplyDeferred);
    }
    return std::unexpected(RoceError::VendorBackendUnavailable);
}

[[nodiscard]] constexpr std::expected<PfcPauseStats, RoceError>
parse_pfc_pause_counters(std::string_view rx_text, std::string_view tx_text) noexcept {
    auto rx = detail::roce::parse_counter(rx_text);
    if (!rx.has_value()) {
        return std::unexpected(rx.error());
    }
    auto tx = detail::roce::parse_counter(tx_text);
    if (!tx.has_value()) {
        return std::unexpected(tx.error());
    }
    return PfcPauseStats{
        .rx_pause_frames = *rx,
        .tx_pause_frames = *tx,
    };
}

// The two counters are files under sysfs, and the fs door that opens them
// counts an open and a read as IO and Block, so only a context that carries
// both can read them.
template <::foundation::effects::IsExecCtx Ctx>
    requires ::fixy::fs::CtxFitsFileMint<Ctx, ProcFileReadMode>
[[nodiscard]] std::expected<PfcPauseStats, RoceError> query_pfc_pause_counters(Ctx const& ctx,
                                                                               NicInterfaceName iface) noexcept {
    auto rx = detail::roce::read_counter(ctx, iface, detail::roce::rx_pause_leaf);
    if (!rx.has_value()) {
        return std::unexpected(rx.error());
    }
    auto tx = detail::roce::read_counter(ctx, iface, detail::roce::tx_pause_leaf);
    if (!tx.has_value()) {
        return std::unexpected(tx.error());
    }
    return PfcPauseStats{
        .rx_pause_frames = *rx,
        .tx_pause_frames = *tx,
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
// error, so a caller written against a boolean keeps its error path.  New
// code calls query_dcqcn_state and branches on the state instead.
[[nodiscard, deprecated("CRUCIBLE_STUB: this chains on query_dcqcn_state, which reads "
                        "nothing, so it always returns RoceError::DcqcnStatusUnavailable")]]
constexpr std::expected<bool, RoceError> verify_dcqcn_active(NicInterfaceName iface) noexcept {
    // The suppression covers this one call, so a caller outside this
    // function still sees the warning at its own call site.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    return dcqcn_state_to_bool(query_dcqcn_state(iface));
#pragma GCC diagnostic pop
}

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
// A refined member makes a config not trivially copyable, because no byte
// route may build a refined value.  A copy still costs what copying the
// bytes costs.
static_assert(std::is_trivially_copy_constructible_v<DcqcnParams> && std::is_trivially_destructible_v<DcqcnParams>);
static_assert(std::is_trivially_copy_constructible_v<RoceConfig> && std::is_trivially_destructible_v<RoceConfig>);
static_assert(std::is_trivially_copyable_v<PfcPauseStats>);
static_assert(ValidPfcPriorityMask<0b00001000>);
static_assert(!ValidPfcPriorityMask<0>);
static_assert(ValidRoceDscp<63>);
static_assert(!ValidRoceDscp<64>);
static_assert(parse_pfc_pause_counters(" 17\n", "23\n").value().tx_pause_frames == 23);
static_assert(parse_pfc_pause_counters("18446744073709551615", "0").value().rx_pause_frames
              == std::numeric_limits<std::uint64_t>::max());
static_assert(parse_pfc_pause_counters("18446744073709551616", "0").error() == RoceError::CounterParseFailed);
static_assert(parse_pfc_pause_counters("", "0").error() == RoceError::CounterParseFailed);
static_assert(::fixy::fs::CtxFitsFileMint<::fixy::InitLoadCtx, ProcFileReadMode>);
static_assert(!::fixy::fs::CtxFitsFileMint<::fixy::ColdInitCtx, ProcFileReadMode>);

}  // namespace crucible::cntp
