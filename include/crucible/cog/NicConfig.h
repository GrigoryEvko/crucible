#pragma once

// GAPS-192. Typed NIC layer-1 configuration substrate.
//
// This header owns admission for ethtool/sysctl/tc-qdisc intent. It does not
// execute shell commands, mutate sysctls, or perform privileged CAP_NET_ADMIN
// work by itself. Operator-gated backends consume Declared* values minted here.

#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/NicOffloadAudit.h>
#include <crucible/cog/TargetCaps.h>
#include <crucible/cntp/CongestionControl.h>
#include <crucible/cntp/Pacing.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/Bits.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/RefinedAlgebra.h>
#include <crucible/safety/Tagged.h>

#include <array>
#include <cstdint>
#include <expected>
#include <string_view>
#include <type_traits>

namespace crucible::cog::nic {

enum class NicConfigError : std::uint8_t {
    None = 0,
    ZeroCog = 1,
    NonNicCog = 2,
    InvalidRingSize = 3,
    InvalidQueueCount = 4,
    InvalidRssTableSize = 5,
    InvalidSpeedMbps = 6,
    InvalidSysctlBytes = 7,
    InvalidSysctlPackets = 8,
    InvalidBusyPollUs = 9,
    InvalidTcpRtoMinUs = 10,
    InvalidTcpMemoryTriple = 11,
    InvalidInterfaceName = 12,
    InterfaceMismatch = 13,
    PrivilegedApplyDeferred = 14,
    PrivilegedBackendUnavailable = 15,
    QueryDeferred = 16,
};

[[nodiscard]] std::string_view nic_config_error_name(NicConfigError error) noexcept;

enum class NicOffload : std::uint32_t {
    Tso           = 1u << 0,
    Gso           = 1u << 1,
    Gro           = 1u << 2,
    Lro           = 1u << 3,
    RxChecksum    = 1u << 4,
    TxChecksum    = 1u << 5,
    ScatterGather = 1u << 6,
    RxVlan        = 1u << 7,
    TxVlan        = 1u << 8,
    RxHash        = 1u << 9,
};

[[nodiscard]] constexpr std::string_view nic_offload_name(NicOffload offload) noexcept {
    switch (offload) {
        case NicOffload::Tso:           return "Tso";
        case NicOffload::Gso:           return "Gso";
        case NicOffload::Gro:           return "Gro";
        case NicOffload::Lro:           return "Lro";
        case NicOffload::RxChecksum:    return "RxChecksum";
        case NicOffload::TxChecksum:    return "TxChecksum";
        case NicOffload::ScatterGather: return "ScatterGather";
        case NicOffload::RxVlan:        return "RxVlan";
        case NicOffload::TxVlan:        return "TxVlan";
        case NicOffload::RxHash:        return "RxHash";
        default:                        return "<unknown NicOffload>";
    }
}

enum class RssHashFunction : std::uint8_t {
    Toeplitz = 0,
    Xor = 1,
    Crc32 = 2,
};

enum class DuplexMode : std::uint8_t {
    Full = 0,
    Half = 1,
};

enum class QdiscKind : std::uint8_t {
    Fq = 0,
    FqCodel = 1,
    Htb = 2,
    Mq = 3,
    Prio = 4,
};

[[nodiscard]] constexpr std::string_view qdisc_kind_name(QdiscKind kind) noexcept {
    switch (kind) {
        case QdiscKind::Fq:      return "fq";
        case QdiscKind::FqCodel: return "fq_codel";
        case QdiscKind::Htb:     return "htb";
        case QdiscKind::Mq:      return "mq";
        case QdiscKind::Prio:    return "prio";
        default:                 return "unknown";
    }
}

using NicRingSize = safety::Refined<
    safety::all_of<safety::power_of_two,
                   safety::in_range<std::uint16_t{256}, std::uint16_t{8192}>>,
    std::uint16_t>;
using NicQueueCount =
    safety::Bounded<std::uint16_t{1}, std::uint16_t{4096}, std::uint16_t>;
using RssTableSize =
    safety::Bounded<std::uint16_t{1}, std::uint16_t{4096}, std::uint16_t>;
using LinkSpeedMbps = safety::Positive<std::uint32_t>;
using PositiveQdiscParam = safety::Positive<std::uint32_t>;
using SysctlBytes = safety::Refined<
    safety::all_of<safety::positive,
                   safety::bounded_above<std::uint64_t{1ull << 40u}>>,
    std::uint64_t>;
using SysctlPackets = safety::Positive<std::uint32_t>;
using BusyPollUs =
    safety::Bounded<std::uint32_t{0}, std::uint32_t{1'000'000}, std::uint32_t>;
using TcpRtoMinUs =
    safety::Bounded<std::uint32_t{1}, std::uint32_t{60'000'000}, std::uint32_t>;

struct RssConfig {
    RssHashFunction hash = RssHashFunction::Toeplitz;
    RssTableSize indirection_entries{std::uint16_t{128}};
    bool four_tuple_hash = true;
};

struct PauseConfig {
    bool autoneg = true;
    bool rx_pause = false;
    bool tx_pause = false;
};

struct LinkConfig {
    LinkSpeedMbps speed_mbps{std::uint32_t{100'000}};
    DuplexMode duplex = DuplexMode::Full;
    bool autoneg = true;
};

struct EthtoolConfig {
    cntp::NicInterfaceName interface{};
    NicRingSize tx_ring_size{std::uint16_t{4096}};
    NicRingSize rx_ring_size{std::uint16_t{4096}};
    NicQueueCount tx_queues{std::uint16_t{1}};
    NicQueueCount rx_queues{std::uint16_t{1}};
    NicQueueCount combined_queues{std::uint16_t{1}};
    NicQueueCount other_queues{std::uint16_t{1}};
    safety::Bits<NicOffload> offloads{};
    RssConfig rss{};
    PauseConfig pause{};
    LinkConfig link{};
};

struct QdiscConfig {
    cntp::NicInterfaceName interface{};
    QdiscKind kind = QdiscKind::Fq;
    PositiveQdiscParam max_quantum{std::uint32_t{8192}};
    PositiveQdiscParam flow_limit{std::uint32_t{100}};
    PositiveQdiscParam ce_threshold_us{std::uint32_t{1}};
    NicQueueCount bands{std::uint16_t{3}};
};

struct TcpMemoryTriple {
    SysctlBytes min{std::uint64_t{4096}};
    SysctlBytes pressure{std::uint64_t{87380}};
    SysctlBytes max{std::uint64_t{6291456}};
};

struct SysctlConfig {
    SysctlBytes rmem_max{std::uint64_t{134'217'728}};
    SysctlBytes wmem_max{std::uint64_t{134'217'728}};
    SysctlBytes rmem_default{std::uint64_t{262'144}};
    SysctlBytes wmem_default{std::uint64_t{262'144}};
    SysctlPackets netdev_max_backlog{std::uint32_t{250'000}};
    SysctlPackets netdev_budget{std::uint32_t{600}};
    BusyPollUs busy_poll_us{std::uint32_t{0}};
    BusyPollUs busy_read_us{std::uint32_t{0}};
    cntp::KernelCcName tcp_congestion{
        cntp::KernelCcName::from("bbr").value()};
    TcpRtoMinUs tcp_rto_min_us{std::uint32_t{10'000}};
    TcpMemoryTriple tcp_rmem{};
    TcpMemoryTriple tcp_wmem{};
    bool tcp_sack = true;
    bool tcp_recovery = true;
    bool tcp_frto = false;
};

struct NicConfigPlan {
    CogIdentity identity{};
    EthtoolConfig ethtool{};
    QdiscConfig qdisc{};
    SysctlConfig sysctl{};
    bool allow_privileged_apply = false;
};

using DeclaredEthtoolConfig =
    safety::Tagged<EthtoolConfig, safety::source::NicConfig>;
using DeclaredQdiscConfig =
    safety::Tagged<QdiscConfig, safety::source::NicConfig>;
using DeclaredSysctlConfig =
    safety::Tagged<SysctlConfig, safety::source::NicConfig>;
using DeclaredNicConfig =
    safety::Tagged<NicConfigPlan, safety::source::NicConfig>;

template <class Ctx>
concept CtxFitsNicConfigMint =
    effects::IsExecCtx<Ctx>
    && effects::CtxAdmits<Ctx, effects::Row<effects::Effect::Init>>;

[[nodiscard]] constexpr std::expected<NicRingSize, NicConfigError>
admit_ring_size(std::uint16_t size) noexcept {
    if (size < 256u || size > 8192u || (size & (size - 1u)) != 0u) {
        return std::unexpected(NicConfigError::InvalidRingSize);
    }
    return NicRingSize{size, typename NicRingSize::Trusted{}};
}

[[nodiscard]] constexpr std::expected<NicQueueCount, NicConfigError>
admit_queue_count(std::uint16_t count) noexcept {
    if (count == 0u || count > 4096u) {
        return std::unexpected(NicConfigError::InvalidQueueCount);
    }
    return NicQueueCount{count, typename NicQueueCount::Trusted{}};
}

[[nodiscard]] constexpr std::expected<RssTableSize, NicConfigError>
admit_rss_table_size(std::uint16_t count) noexcept {
    if (count == 0u || count > 4096u) {
        return std::unexpected(NicConfigError::InvalidRssTableSize);
    }
    return RssTableSize{count, typename RssTableSize::Trusted{}};
}

[[nodiscard]] constexpr std::expected<SysctlBytes, NicConfigError>
admit_sysctl_bytes(std::uint64_t bytes) noexcept {
    if (bytes == 0u || bytes > (1ull << 40u)) {
        return std::unexpected(NicConfigError::InvalidSysctlBytes);
    }
    return SysctlBytes{bytes, typename SysctlBytes::Trusted{}};
}

[[nodiscard]] constexpr std::expected<BusyPollUs, NicConfigError>
admit_busy_poll_us(std::uint32_t us) noexcept {
    if (us > 1'000'000u) {
        return std::unexpected(NicConfigError::InvalidBusyPollUs);
    }
    return BusyPollUs{us, typename BusyPollUs::Trusted{}};
}

[[nodiscard]] constexpr std::expected<TcpRtoMinUs, NicConfigError>
admit_tcp_rto_min_us(std::uint32_t us) noexcept {
    if (us == 0u || us > 60'000'000u) {
        return std::unexpected(NicConfigError::InvalidTcpRtoMinUs);
    }
    return TcpRtoMinUs{us, typename TcpRtoMinUs::Trusted{}};
}

[[nodiscard]] constexpr bool
tcp_memory_triple_ordered(TcpMemoryTriple triple) noexcept {
    return triple.min.value() <= triple.pressure.value()
        && triple.pressure.value() <= triple.max.value();
}

[[nodiscard]] constexpr bool
interface_name_present(cntp::NicInterfaceName interface) noexcept {
    return !interface.view().empty();
}

[[nodiscard]] constexpr bool
same_interface(cntp::NicInterfaceName lhs,
               cntp::NicInterfaceName rhs) noexcept {
    return lhs.view() == rhs.view();
}

[[nodiscard]] constexpr std::expected<void, NicConfigError>
validate_ethtool_config(EthtoolConfig const& config) noexcept {
    if (!interface_name_present(config.interface)) {
        return std::unexpected(NicConfigError::InvalidInterfaceName);
    }
    return {};
}

[[nodiscard]] constexpr std::expected<void, NicConfigError>
validate_qdisc_config(QdiscConfig const& config) noexcept {
    if (!interface_name_present(config.interface)) {
        return std::unexpected(NicConfigError::InvalidInterfaceName);
    }
    return {};
}

[[nodiscard]] constexpr std::expected<void, NicConfigError>
validate_sysctl_config(SysctlConfig const& config) noexcept {
    if (!tcp_memory_triple_ordered(config.tcp_rmem)
        || !tcp_memory_triple_ordered(config.tcp_wmem)) {
        return std::unexpected(NicConfigError::InvalidTcpMemoryTriple);
    }
    return {};
}

[[nodiscard]] constexpr std::expected<void, NicConfigError>
validate_nic_config(NicConfigPlan const& plan) noexcept {
    if (plan.identity.uuid.is_zero()) {
        return std::unexpected(NicConfigError::ZeroCog);
    }
    if (plan.identity.kind != CogKind::NicPort) {
        return std::unexpected(NicConfigError::NonNicCog);
    }
    auto valid_ethtool = validate_ethtool_config(plan.ethtool);
    if (!valid_ethtool.has_value()) {
        return std::unexpected(valid_ethtool.error());
    }
    auto valid_qdisc = validate_qdisc_config(plan.qdisc);
    if (!valid_qdisc.has_value()) {
        return std::unexpected(valid_qdisc.error());
    }
    if (!same_interface(plan.ethtool.interface, plan.qdisc.interface)) {
        return std::unexpected(NicConfigError::InterfaceMismatch);
    }
    return validate_sysctl_config(plan.sysctl);
}

template <class Ctx>
    requires CtxFitsNicConfigMint<Ctx>
[[nodiscard]] constexpr std::expected<DeclaredNicConfig, NicConfigError>
mint_nic_config(Ctx const&,
                CogIdentity identity,
                cntp::NicInterfaceName interface,
                EthtoolConfig ethtool = {},
                QdiscConfig qdisc = {},
                SysctlConfig sysctl = {},
                bool allow_privileged_apply = false) noexcept {
    ethtool.interface = interface;
    qdisc.interface = interface;
    NicConfigPlan plan{
        .identity = identity,
        .ethtool = ethtool,
        .qdisc = qdisc,
        .sysctl = sysctl,
        .allow_privileged_apply = allow_privileged_apply,
    };
    auto valid = validate_nic_config(plan);
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }
    return DeclaredNicConfig{plan};
}

[[nodiscard]] constexpr DeclaredEthtoolConfig
declare_ethtool_config(EthtoolConfig config) noexcept {
    return DeclaredEthtoolConfig{config};
}

[[nodiscard]] constexpr DeclaredQdiscConfig
declare_qdisc_config(QdiscConfig config) noexcept {
    return DeclaredQdiscConfig{config};
}

[[nodiscard]] constexpr std::expected<DeclaredSysctlConfig, NicConfigError>
declare_sysctl_config(SysctlConfig config) noexcept {
    auto valid = validate_sysctl_config(config);
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }
    return DeclaredSysctlConfig{config};
}

[[nodiscard]] constexpr NicTxQdisc
qdisc_to_audit_qdisc(QdiscKind kind) noexcept {
    switch (kind) {
        case QdiscKind::Fq:      return NicTxQdisc::Fq;
        case QdiscKind::FqCodel: return NicTxQdisc::FqCodel;
        case QdiscKind::Mq:      return NicTxQdisc::Mq;
        default:                 return NicTxQdisc::Unknown;
    }
}

[[nodiscard]] constexpr safety::Bits<NicFeature>
audit_features_from_offloads(safety::Bits<NicOffload> offloads) noexcept {
    safety::Bits<NicFeature> out{};
    if (offloads.test(NicOffload::Tso)) out.set(NicFeature::Tso);
    if (offloads.test(NicOffload::Gso)) out.set(NicFeature::Gso);
    if (offloads.test(NicOffload::Gro)) out.set(NicFeature::Gro);
    if (offloads.test(NicOffload::Lro)) out.set(NicFeature::Lro);
    if (offloads.test(NicOffload::RxHash)) out.set(NicFeature::Rss);
    return out;
}

[[nodiscard]] std::expected<void, NicConfigError>
apply_config(DeclaredNicConfig config) noexcept;
[[nodiscard]] std::expected<void, NicConfigError>
apply_ethtool(DeclaredEthtoolConfig config) noexcept;
[[nodiscard]] std::expected<void, NicConfigError>
apply_qdisc(DeclaredQdiscConfig config) noexcept;
[[nodiscard]] std::expected<void, NicConfigError>
apply_sysctl(DeclaredSysctlConfig config) noexcept;
[[nodiscard]] std::expected<DeclaredNicConfig, NicConfigError>
query_current(CogIdentity identity, cntp::NicInterfaceName interface) noexcept;

static_assert(sizeof(NicRingSize) == sizeof(std::uint16_t));
static_assert(sizeof(NicQueueCount) == sizeof(std::uint16_t));
static_assert(sizeof(RssTableSize) == sizeof(std::uint16_t));
static_assert(sizeof(SysctlBytes) == sizeof(std::uint64_t));
static_assert(sizeof(BusyPollUs) == sizeof(std::uint32_t));
static_assert(sizeof(TcpRtoMinUs) == sizeof(std::uint32_t));
static_assert(sizeof(DeclaredNicConfig) == sizeof(NicConfigPlan));
static_assert(CtxFitsNicConfigMint<effects::ColdInitCtx>);
static_assert(!CtxFitsNicConfigMint<effects::BgDrainCtx>);
static_assert(std::is_trivially_copyable_v<RssConfig>);
static_assert(std::is_trivially_copyable_v<EthtoolConfig>);
static_assert(std::is_trivially_copyable_v<QdiscConfig>);
static_assert(std::is_trivially_copyable_v<SysctlConfig>);

}  // namespace crucible::cog::nic
