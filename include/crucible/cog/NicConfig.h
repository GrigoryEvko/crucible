#pragma once

// Admission for NIC configuration intent. Nothing here runs a command,
// writes a kernel tunable or does any work that needs the network
// administration capability. A privileged backend consumes the
// declared values this header mints.
//
// foundation::reflect::enum_name gives the name of each enumerator.
// qdisc_kind_name is a different mapping: it gives the name that the
// kernel uses for the queueing discipline.

#include <crucible/cntp/CongestionControl.h>
#include <crucible/cntp/Pacing.h>
#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/NicOffloadAudit.h>
#include <crucible/cog/TargetCaps.h>
#include <fixy/Bits.h>
#include <fixy/Ctx.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <cstdint>
#include <expected>
#include <string_view>
#include <type_traits>

namespace crucible::cog::nic {

// Admission and validation are live. Every privileged apply and query
// function is a stub. While this is false, an apply returns
// PrivilegedApplyDeferred and a query returns
// PrivilegedBackendUnavailable or QueryDeferred. Setting it true means
// a backend exists that drives the real interfaces, and the tests that
// assert the stubbed behaviour change with it.
inline constexpr bool privileged_apply_implemented = false;

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

enum class NicOffload : std::uint32_t {
    Tso = 1u << 0,
    Gso = 1u << 1,
    Gro = 1u << 2,
    Lro = 1u << 3,
    RxChecksum = 1u << 4,
    TxChecksum = 1u << 5,
    ScatterGather = 1u << 6,
    RxVlan = 1u << 7,
    TxVlan = 1u << 8,
    RxHash = 1u << 9,
};

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
        case QdiscKind::Fq:
            return "fq";
        case QdiscKind::FqCodel:
            return "fq_codel";
        case QdiscKind::Htb:
            return "htb";
        case QdiscKind::Mq:
            return "mq";
        case QdiscKind::Prio:
            return "prio";
        default:
            return "unknown";
    }
}

// Each bound is named once, so a mint and the type it fills cannot
// spell two different predicates.
inline constexpr auto ring_size_bound =
    ::fixy::all_of<::fixy::power_of_two, ::fixy::in_range<std::uint16_t{256}, std::uint16_t{8192}>>;
inline constexpr auto queue_count_bound = ::fixy::in_range<std::uint16_t{1}, std::uint16_t{4096}>;
inline constexpr auto sysctl_bytes_bound =
    ::fixy::all_of<::fixy::positive, ::fixy::bounded_above<std::uint64_t{1ull << 40u}>>;
inline constexpr auto busy_poll_us_bound = ::fixy::in_range<std::uint32_t{0}, std::uint32_t{1000000}>;
inline constexpr auto tcp_rto_min_us_bound = ::fixy::in_range<std::uint32_t{1}, std::uint32_t{60000000}>;

using NicRingSize = ::fixy::Refined<ring_size_bound, std::uint16_t>;
// A queue count and an indirection table size share one bound, so they
// are one type under two names.
using NicQueueCount = ::fixy::Refined<queue_count_bound, std::uint16_t>;
using RssTableSize = NicQueueCount;
using LinkSpeedMbps = ::fixy::Positive<std::uint32_t>;
using PositiveQdiscParam = ::fixy::Positive<std::uint32_t>;
using SysctlBytes = ::fixy::Refined<sysctl_bytes_bound, std::uint64_t>;
using SysctlPackets = ::fixy::Positive<std::uint32_t>;
using BusyPollUs = ::fixy::Refined<busy_poll_us_bound, std::uint32_t>;
using TcpRtoMinUs = ::fixy::Refined<tcp_rto_min_us_bound, std::uint32_t>;

inline constexpr NicRingSize default_ring_size = ::fixy::mint_refined<ring_size_bound>(std::uint16_t{4096});
inline constexpr NicQueueCount single_queue_count = ::fixy::mint_refined<queue_count_bound>(std::uint16_t{1});

struct RssConfig {
    RssHashFunction hash = RssHashFunction::Toeplitz;
    RssTableSize indirection_entries = ::fixy::mint_refined<queue_count_bound>(std::uint16_t{128});
    bool four_tuple_hash = true;
};

struct PauseConfig {
    bool autoneg = true;
    bool rx_pause = false;
    bool tx_pause = false;
};

struct LinkConfig {
    LinkSpeedMbps speed_mbps = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{100000});
    DuplexMode duplex = DuplexMode::Full;
    bool autoneg = true;
};

struct EthtoolConfig {
    cntp::NicInterfaceName interface{};
    NicRingSize tx_ring_size = default_ring_size;
    NicRingSize rx_ring_size = default_ring_size;
    NicQueueCount tx_queues = single_queue_count;
    NicQueueCount rx_queues = single_queue_count;
    NicQueueCount combined_queues = single_queue_count;
    NicQueueCount other_queues = single_queue_count;
    ::fixy::Bits<NicOffload> offloads{};
    RssConfig rss{};
    PauseConfig pause{};
    LinkConfig link{};
};

struct QdiscConfig {
    cntp::NicInterfaceName interface{};
    QdiscKind kind = QdiscKind::Fq;
    PositiveQdiscParam max_quantum = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{8192});
    PositiveQdiscParam flow_limit = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{100});
    PositiveQdiscParam ce_threshold_us = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{1});
    NicQueueCount bands = ::fixy::mint_refined<queue_count_bound>(std::uint16_t{3});
};

struct TcpMemoryTriple {
    SysctlBytes min = ::fixy::mint_refined<sysctl_bytes_bound>(std::uint64_t{4096});
    SysctlBytes pressure = ::fixy::mint_refined<sysctl_bytes_bound>(std::uint64_t{87380});
    SysctlBytes max = ::fixy::mint_refined<sysctl_bytes_bound>(std::uint64_t{6291456});
};

struct SysctlConfig {
    SysctlBytes rmem_max = ::fixy::mint_refined<sysctl_bytes_bound>(std::uint64_t{134217728});
    SysctlBytes wmem_max = ::fixy::mint_refined<sysctl_bytes_bound>(std::uint64_t{134217728});
    SysctlBytes rmem_default = ::fixy::mint_refined<sysctl_bytes_bound>(std::uint64_t{262144});
    SysctlBytes wmem_default = ::fixy::mint_refined<sysctl_bytes_bound>(std::uint64_t{262144});
    SysctlPackets netdev_max_backlog = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{250000});
    SysctlPackets netdev_budget = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{600});
    BusyPollUs busy_poll_us = ::fixy::mint_refined<busy_poll_us_bound>(std::uint32_t{0});
    BusyPollUs busy_read_us = ::fixy::mint_refined<busy_poll_us_bound>(std::uint32_t{0});
    cntp::KernelCcName tcp_congestion{cntp::KernelCcName::from("bbr").value()};
    TcpRtoMinUs tcp_rto_min_us = ::fixy::mint_refined<tcp_rto_min_us_bound>(std::uint32_t{10000});
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

using DeclaredEthtoolConfig = ::fixy::Tagged<EthtoolConfig, ::fixy::tags::source::NicConfig>;
using DeclaredQdiscConfig = ::fixy::Tagged<QdiscConfig, ::fixy::tags::source::NicConfig>;
using DeclaredSysctlConfig = ::fixy::Tagged<SysctlConfig, ::fixy::tags::source::NicConfig>;
using DeclaredNicConfig = ::fixy::Tagged<NicConfigPlan, ::fixy::tags::source::NicConfig>;

template <class Ctx>
concept CtxFitsNicConfigMint =
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::CtxAdmits<Ctx, ::foundation::effects::Row<::foundation::effects::Effect::Init>>;

[[nodiscard]] constexpr std::expected<NicRingSize, NicConfigError> admit_ring_size(std::uint16_t size) noexcept {
    return ::fixy::admit_refined<ring_size_bound>(size, NicConfigError::InvalidRingSize);
}

[[nodiscard]] constexpr std::expected<NicQueueCount, NicConfigError> admit_queue_count(std::uint16_t count) noexcept {
    return ::fixy::admit_refined<queue_count_bound>(count, NicConfigError::InvalidQueueCount);
}

[[nodiscard]] constexpr std::expected<RssTableSize, NicConfigError> admit_rss_table_size(std::uint16_t count) noexcept {
    return ::fixy::admit_refined<queue_count_bound>(count, NicConfigError::InvalidRssTableSize);
}

[[nodiscard]] constexpr std::expected<SysctlBytes, NicConfigError> admit_sysctl_bytes(std::uint64_t bytes) noexcept {
    return ::fixy::admit_refined<sysctl_bytes_bound>(bytes, NicConfigError::InvalidSysctlBytes);
}

[[nodiscard]] constexpr std::expected<BusyPollUs, NicConfigError> admit_busy_poll_us(std::uint32_t us) noexcept {
    return ::fixy::admit_refined<busy_poll_us_bound>(us, NicConfigError::InvalidBusyPollUs);
}

[[nodiscard]] constexpr std::expected<TcpRtoMinUs, NicConfigError> admit_tcp_rto_min_us(std::uint32_t us) noexcept {
    return ::fixy::admit_refined<tcp_rto_min_us_bound>(us, NicConfigError::InvalidTcpRtoMinUs);
}

[[nodiscard]] constexpr bool tcp_memory_triple_ordered(TcpMemoryTriple const& triple) noexcept {
    return triple.min.value() <= triple.pressure.value() && triple.pressure.value() <= triple.max.value();
}

[[nodiscard]] constexpr bool interface_name_present(cntp::NicInterfaceName interface) noexcept {
    return !interface.view().empty();
}

[[nodiscard]] constexpr bool same_interface(cntp::NicInterfaceName lhs, cntp::NicInterfaceName rhs) noexcept {
    return lhs.view() == rhs.view();
}

[[nodiscard]] constexpr std::expected<void, NicConfigError>
validate_ethtool_config(EthtoolConfig const& config) noexcept {
    if (!interface_name_present(config.interface)) {
        return std::unexpected(NicConfigError::InvalidInterfaceName);
    }
    return {};
}

[[nodiscard]] constexpr std::expected<void, NicConfigError> validate_qdisc_config(QdiscConfig const& config) noexcept {
    if (!interface_name_present(config.interface)) {
        return std::unexpected(NicConfigError::InvalidInterfaceName);
    }
    return {};
}

[[nodiscard]] constexpr std::expected<void, NicConfigError>
validate_sysctl_config(SysctlConfig const& config) noexcept {
    if (!tcp_memory_triple_ordered(config.tcp_rmem) || !tcp_memory_triple_ordered(config.tcp_wmem)) {
        return std::unexpected(NicConfigError::InvalidTcpMemoryTriple);
    }
    return {};
}

[[nodiscard]] constexpr std::expected<void, NicConfigError> validate_nic_config(NicConfigPlan const& plan) noexcept {
    if (plan.identity.uuid.is_zero()) {
        return std::unexpected(NicConfigError::ZeroCog);
    }
    if (plan.identity.kind != CogKind::NicPort) {
        return std::unexpected(NicConfigError::NonNicCog);
    }
    if (auto valid = validate_ethtool_config(plan.ethtool); !valid.has_value()) {
        return valid;
    }
    if (auto valid = validate_qdisc_config(plan.qdisc); !valid.has_value()) {
        return valid;
    }
    if (!same_interface(plan.ethtool.interface, plan.qdisc.interface)) {
        return std::unexpected(NicConfigError::InterfaceMismatch);
    }
    return validate_sysctl_config(plan.sysctl);
}

namespace detail {

// Each declaration in this header runs the validator of its
// configuration before it writes the source tag. A configuration that
// fails the check gets its error, not the tag.
template <typename Config, auto Validate>
[[nodiscard]] constexpr std::expected<::fixy::Tagged<Config, ::fixy::tags::source::NicConfig>, NicConfigError>
declare_validated(Config const& config) noexcept {
    if (auto valid = Validate(config); !valid.has_value()) {
        return std::unexpected(valid.error());
    }
    return ::fixy::mint_tagged<::fixy::tags::source::NicConfig>(config);
}

}  // namespace detail

template <class Ctx>
    requires CtxFitsNicConfigMint<Ctx>
[[nodiscard]] constexpr std::expected<DeclaredNicConfig, NicConfigError>
mint_nic_config(Ctx const&, CogIdentity identity, cntp::NicInterfaceName interface, EthtoolConfig ethtool = {},
                QdiscConfig qdisc = {}, SysctlConfig sysctl = {}, bool allow_privileged_apply = false) noexcept {
    ethtool.interface = interface;
    qdisc.interface = interface;
    return detail::declare_validated<NicConfigPlan, validate_nic_config>(NicConfigPlan{
        .identity = identity,
        .ethtool = ethtool,
        .qdisc = qdisc,
        .sysctl = sysctl,
        .allow_privileged_apply = allow_privileged_apply,
    });
}

[[nodiscard]] constexpr std::expected<DeclaredEthtoolConfig, NicConfigError>
declare_ethtool_config(EthtoolConfig const& config) noexcept {
    return detail::declare_validated<EthtoolConfig, validate_ethtool_config>(config);
}

[[nodiscard]] constexpr std::expected<DeclaredQdiscConfig, NicConfigError>
declare_qdisc_config(QdiscConfig const& config) noexcept {
    return detail::declare_validated<QdiscConfig, validate_qdisc_config>(config);
}

[[nodiscard]] constexpr std::expected<DeclaredSysctlConfig, NicConfigError>
declare_sysctl_config(SysctlConfig const& config) noexcept {
    return detail::declare_validated<SysctlConfig, validate_sysctl_config>(config);
}

[[nodiscard]] constexpr NicTxQdisc qdisc_to_audit_qdisc(QdiscKind kind) noexcept {
    switch (kind) {
        case QdiscKind::Fq:
            return NicTxQdisc::Fq;
        case QdiscKind::FqCodel:
            return NicTxQdisc::FqCodel;
        case QdiscKind::Mq:
            return NicTxQdisc::Mq;
        default:
            return NicTxQdisc::Unknown;
    }
}

[[nodiscard]] constexpr ::fixy::Bits<NicFeature>
audit_features_from_offloads(::fixy::Bits<NicOffload> offloads) noexcept {
    ::fixy::Bits<NicFeature> out{};
    if (offloads.test(NicOffload::Tso)) out.set(NicFeature::Tso);
    if (offloads.test(NicOffload::Gso)) out.set(NicFeature::Gso);
    if (offloads.test(NicOffload::Gro)) out.set(NicFeature::Gro);
    if (offloads.test(NicOffload::Lro)) out.set(NicFeature::Lro);
    if (offloads.test(NicOffload::RxHash)) out.set(NicFeature::Rss);
    return out;
}

// The five privileged surfaces below are stubs, and the deprecation
// attribute is what makes a caller see that at compile time rather
// than only through the returned sentinel. A caller that means to
// touch a stub suppresses the warning around the call.
[[nodiscard, deprecated("CRUCIBLE_STUB: no privileged backend drives the NIC "
                        "configuration interfaces; returns PrivilegedApplyDeferred or "
                        "PrivilegedBackendUnavailable")]]
std::expected<void, NicConfigError> apply_config(DeclaredNicConfig config) noexcept;
[[nodiscard, deprecated("CRUCIBLE_STUB: no privileged backend applies ring, "
                        "queue or hash settings; returns PrivilegedApplyDeferred")]]
std::expected<void, NicConfigError> apply_ethtool(DeclaredEthtoolConfig config) noexcept;
[[nodiscard, deprecated("CRUCIBLE_STUB: no privileged backend installs queueing "
                        "disciplines; returns PrivilegedApplyDeferred")]]
std::expected<void, NicConfigError> apply_qdisc(DeclaredQdiscConfig config) noexcept;
[[nodiscard, deprecated("CRUCIBLE_STUB: no privileged backend writes the kernel "
                        "network tunables; returns PrivilegedApplyDeferred")]]
std::expected<void, NicConfigError> apply_sysctl(DeclaredSysctlConfig config) noexcept;
[[nodiscard, deprecated("CRUCIBLE_STUB: no backend reads the live NIC "
                        "configuration; returns NicConfigError::QueryDeferred")]]
std::expected<DeclaredNicConfig, NicConfigError> query_current(CogIdentity identity,
                                                               cntp::NicInterfaceName interface) noexcept;

static_assert(sizeof(NicRingSize) == sizeof(std::uint16_t));
static_assert(sizeof(NicQueueCount) == sizeof(std::uint16_t));
static_assert(sizeof(SysctlBytes) == sizeof(std::uint64_t));
static_assert(sizeof(BusyPollUs) == sizeof(std::uint32_t));
static_assert(sizeof(TcpRtoMinUs) == sizeof(std::uint32_t));
static_assert(sizeof(DeclaredNicConfig) == sizeof(NicConfigPlan));
static_assert(CtxFitsNicConfigMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsNicConfigMint<::fixy::BgDrainCtx>);
static_assert(!CtxFitsNicConfigMint<::fixy::HotFgCtx>);

// A refined field keeps a configuration cheap to copy and free to
// destroy. No byte copy builds a refined field, so a configuration is
// not trivially copyable.
static_assert(std::is_trivially_copy_constructible_v<EthtoolConfig> && std::is_trivially_destructible_v<EthtoolConfig>);
static_assert(std::is_trivially_copy_constructible_v<QdiscConfig> && std::is_trivially_destructible_v<QdiscConfig>);
static_assert(std::is_trivially_copy_constructible_v<SysctlConfig> && std::is_trivially_destructible_v<SysctlConfig>);
static_assert(!std::is_trivially_copyable_v<SysctlConfig>);

}  // namespace crucible::cog::nic
