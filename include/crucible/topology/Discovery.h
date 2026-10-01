#pragma once

// The graph that graph() returns holds non-owning spans.  A snapshot owns the
// node and edge storage, and the graph is only a view over it.  A graph built
// over storage local to a function would dangle at the return.

#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/TargetCaps.h>
#include <crucible/topology/TopologyGraph.h>
#include <fixy/Bits.h>
#include <fixy/Ctx.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>

namespace crucible::topology {

// The text a discovery parser reads comes from a tool or a peer, never from
// this process.  A parser takes only this type, so a caller must name the
// source with mint_tagged before the parser sees a byte.
using ExternalDiscoveryText = ::fixy::Tagged<std::string_view, ::fixy::tags::source::External>;
using VendorDiscoveryString = cog::VendorClaim<std::string_view>;

enum class DiscoveryError : std::uint8_t {
    EmptyInput = 0,
    MalformedRecord = 1,
    TooManyNodes = 2,
    TooManyEdges = 3,
    InvalidNodeIndex = 4,
    InvalidInterfaceName = 5,
    MissingRequiredField = 6,
    UnsupportedSource = 7,
    RediscoveryRequiresBg = 8,
};

enum class DiscoverySource : std::uint8_t {
    PcieLspci = 0,
    PcieSysfs = 1,
    NicSysfs = 2,
    EthtoolInfo = 3,
    EthtoolFeatures = 4,
    Lldp = 5,
    Gpu = 6,
    Nvme = 7,
    NvSwitch = 8,
    Optical = 9,
    Udev = 10,
};

enum class DiscoveryOutcome : std::uint8_t {
    NotAttempted = 0,
    Complete = 1,
    Partial = 2,
    Failed = 3,
};

enum class DiscoveryNodeKind : std::uint8_t {
    Unknown = 0,
    Gpu = 1,
    NicPort = 2,
    NicCard = 3,
    NvSwitch = 4,
    NvmeNamespace = 5,
    NvmeDrive = 6,
    PcieRoot = 7,
    PcieLaneGroup = 8,
    OpticalTransceiver = 9,
};

struct DiscoverySourceStatus {
    DiscoverySource source = DiscoverySource::PcieLspci;
    DiscoveryOutcome outcome = DiscoveryOutcome::NotAttempted;
    std::uint16_t records_seen = 0;
    std::uint16_t records_admitted = 0;
    DiscoveryError error = DiscoveryError::EmptyInput;
};

// `size` used to sit public beside the array, so push() was not its
// only writer.  A stored 255 then read as "not full" against the
// equality test this carried, and push() wrote statuses[255] on a
// 16-element array; view() handed the same range to every reader.
// Both members are private now and push() is the only writer, so a
// size above max_statuses is unrepresentable and neither accessor
// needs a bound check.  A check would not have helped in production
// anyway: the release build ships -DNDEBUG with no
// _GLIBCXX_ASSERTIONS, so std::array::operator[] is unchecked there.
class DiscoveryReport {
public:
    static constexpr std::size_t max_statuses = 16;

    constexpr DiscoveryReport() noexcept = default;

    [[nodiscard]] constexpr std::span<const DiscoverySourceStatus> view() const noexcept {
        return {statuses_.data(), size_};
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }

    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }

    [[nodiscard]] constexpr bool full() const noexcept { return size_ >= max_statuses; }

    [[nodiscard]] constexpr bool push(DiscoverySourceStatus status) noexcept {
        if (full()) {
            return false;
        }
        statuses_[size_] = status;
        ++size_;
        return true;
    }

private:
    std::array<DiscoverySourceStatus, max_statuses> statuses_{};
    std::uint8_t size_ = 0;
};

struct DiscoveryNodeFact {
    cog::Uuid uuid{};
    cog::CogLevel level = cog::CogLevel::L0_Atomic;
    cog::CogKind kind = cog::CogKind::Gpu;
    VendorDiscoveryString vendor{};
    VendorDiscoveryString model{};
    VendorDiscoveryString driver{};
    VendorDiscoveryString firmware{};
    VendorDiscoveryString bus_info{};
    std::int16_t numa_node = -1;
    cog::NicPortTargetCaps nic_caps{};
};

struct DiscoveryEdgeFact {
    std::uint16_t from_node = 0;
    std::uint16_t to_node = 0;
    LinkKind kind = LinkKind::Unknown;
    std::uint64_t bandwidth_bytes_per_sec = 0;
    std::uint64_t rtt_ns_p50 = 0;
    std::uint64_t rtt_ns_p99 = 0;
    float drop_rate = 0.0f;
};

template <std::size_t MaxNodes, std::size_t MaxEdges>
concept DiscoveryShape = MaxNodes > 0 && MaxEdges > 0;

template <class Ctx>
concept CtxFitsDiscoveryInit =
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::CtxAdmits<Ctx, ::foundation::effects::Row<::foundation::effects::Effect::Init>>;

template <class Ctx>
concept CtxFitsDiscoveryBg =
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::CtxAdmits<Ctx, ::foundation::effects::Row<::foundation::effects::Effect::Bg>>;

// foundation::reflect::enum_name gives the log spelling of the four enums
// above.

[[nodiscard]] constexpr ExternalDiscoveryText tag_external_discovery_text(std::string_view text) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::External>(text);
}

[[nodiscard]] constexpr VendorDiscoveryString tag_vendor_discovery_string(std::string_view text) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::Vendor>(text);
}

[[nodiscard]] constexpr cog::CogKind cog_kind_from_discovery(DiscoveryNodeKind kind) noexcept {
    switch (kind) {
        case DiscoveryNodeKind::Gpu:
            return cog::CogKind::Gpu;
        case DiscoveryNodeKind::NicPort:
            return cog::CogKind::NicPort;
        case DiscoveryNodeKind::NicCard:
            return cog::CogKind::NicCard;
        case DiscoveryNodeKind::NvSwitch:
            return cog::CogKind::NvSwitch;
        case DiscoveryNodeKind::NvmeNamespace:
            return cog::CogKind::NvmeNamespace;
        case DiscoveryNodeKind::NvmeDrive:
            return cog::CogKind::NvmeDrive;
        case DiscoveryNodeKind::PcieRoot:
            return cog::CogKind::PcieRoot;
        case DiscoveryNodeKind::PcieLaneGroup:
            return cog::CogKind::PcieLaneGroup;
        case DiscoveryNodeKind::OpticalTransceiver:
            return cog::CogKind::OpticalTransceiver;
        case DiscoveryNodeKind::Unknown:
            return cog::CogKind::PcieLaneGroup;
        default:
            return cog::CogKind::PcieLaneGroup;
    }
}

[[nodiscard]] constexpr cog::CogLevel default_level_for(cog::CogKind kind) noexcept {
    switch (kind) {
        case cog::CogKind::NicCard:
        case cog::CogKind::NvmeDrive:
        case cog::CogKind::PcieRoot:
            return cog::CogLevel::L1_Component;
        default:
            return cog::CogLevel::L0_Atomic;
    }
}

[[nodiscard]] constexpr std::uint64_t stable_discovery_hash(std::string_view a, std::string_view b = {},
                                                            std::string_view c = {}) noexcept {
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&h](std::string_view s) constexpr noexcept {
        for (char ch : s) {
            h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(ch));
            h *= 1099511628211ull;
        }
        h ^= 0xffu;
        h *= 1099511628211ull;
    };
    mix(a);
    mix(b);
    mix(c);
    return h == 0 ? 1 : h;
}

template <std::size_t MaxNodes, std::size_t MaxEdges>
    requires DiscoveryShape<MaxNodes, MaxEdges>
class DiscoverySnapshot;

// The shape the host parsers fill.
inline constexpr std::size_t default_discovery_nodes = 64;
inline constexpr std::size_t default_discovery_edges = 128;
using DefaultDiscoverySnapshot = DiscoverySnapshot<default_discovery_nodes, default_discovery_edges>;

template <std::size_t MaxNodes = default_discovery_nodes, std::size_t MaxEdges = default_discovery_edges, class Ctx>
    requires DiscoveryShape<MaxNodes, MaxEdges> && CtxFitsDiscoveryInit<Ctx>
[[nodiscard]] constexpr DiscoverySnapshot<MaxNodes, MaxEdges> mint_discovery_snapshot(Ctx const&) noexcept;

// The storage behind a discovered graph.  Only mint_discovery_snapshot builds
// one, so a snapshot exists only where an initialisation context exists.
template <std::size_t MaxNodes, std::size_t MaxEdges>
    requires DiscoveryShape<MaxNodes, MaxEdges>
class DiscoverySnapshot {
    template <std::size_t N, std::size_t E, class Ctx>
        requires DiscoveryShape<N, E> && CtxFitsDiscoveryInit<Ctx>
    friend constexpr DiscoverySnapshot<N, E> mint_discovery_snapshot(Ctx const&) noexcept;

    constexpr DiscoverySnapshot() noexcept = default;

public:
    [[nodiscard]] constexpr std::span<const cog::CogIdentity> nodes() const noexcept {
        return {nodes_.data(), node_count_};
    }

    [[nodiscard]] constexpr std::span<const TopologyEdge> edges() const noexcept {
        return {edges_.data(), edge_count_};
    }

    [[nodiscard]] constexpr std::span<const DiscoveryNodeFact> node_facts() const noexcept {
        return {node_facts_.data(), node_count_};
    }

    [[nodiscard]] constexpr std::span<const DiscoveryEdgeFact> edge_facts() const noexcept {
        return {edge_facts_.data(), edge_count_};
    }

    [[nodiscard]] constexpr std::size_t node_count() const noexcept { return node_count_; }

    [[nodiscard]] constexpr std::size_t edge_count() const noexcept { return edge_count_; }

    [[nodiscard]] constexpr DiscoveryReport const& report() const noexcept { return report_; }

    [[nodiscard]] constexpr std::expected<std::uint16_t, DiscoveryError> add_node(DiscoveryNodeFact fact) noexcept {
        if (node_count_ == MaxNodes) {
            return std::unexpected(DiscoveryError::TooManyNodes);
        }
        const std::uint16_t idx = static_cast<std::uint16_t>(node_count_);
        if (fact.uuid.is_zero()) {
            const std::uint64_t lo =
                stable_discovery_hash(fact.bus_info.value(), fact.vendor.value(), fact.model.value());
            fact.uuid = cog::Uuid{0xD15C0111ull, lo};
        }
        fact.level = fact.level == cog::CogLevel::L0_Atomic ? default_level_for(fact.kind) : fact.level;
        node_facts_[node_count_] = fact;
        nodes_[node_count_] = cog::CogIdentity{
            .uuid = fact.uuid,
            .level = fact.level,
            .kind = fact.kind,
            .vendor = fact.vendor,
            .model = fact.model,
            .firmware_revision = firmware_claim(fact),
            .bios_revision = {},
        };
        ++node_count_;
        return idx;
    }

    [[nodiscard]] constexpr std::expected<void, DiscoveryError> update_node(std::uint16_t idx,
                                                                            DiscoveryNodeFact fact) noexcept {
        if (idx >= node_count_) {
            return std::unexpected(DiscoveryError::InvalidNodeIndex);
        }
        node_facts_[idx] = fact;
        nodes_[idx].uuid = fact.uuid;
        nodes_[idx].level = fact.level;
        nodes_[idx].kind = fact.kind;
        nodes_[idx].vendor = fact.vendor;
        nodes_[idx].model = fact.model;
        nodes_[idx].firmware_revision = firmware_claim(fact);
        return {};
    }

    [[nodiscard]] constexpr std::expected<std::uint16_t, DiscoveryError> add_edge(DiscoveryEdgeFact fact) noexcept {
        if (edge_count_ == MaxEdges) {
            return std::unexpected(DiscoveryError::TooManyEdges);
        }
        if (fact.from_node >= node_count_ || fact.to_node >= node_count_) {
            return std::unexpected(DiscoveryError::InvalidNodeIndex);
        }
        const std::uint16_t idx = static_cast<std::uint16_t>(edge_count_);
        edge_facts_[edge_count_] = fact;
        edges_[edge_count_] = TopologyEdge{
            .id = EdgeId{idx},
            .kind = fact.kind,
            .peer = &nodes_[fact.to_node],
            .bandwidth_bytes_per_sec =
                ::fixy::mint_tagged<::fixy::tags::source::Calibrated>(fact.bandwidth_bytes_per_sec),
            .rtt_ns_p50 = ::fixy::mint_tagged<::fixy::tags::source::Calibrated>(fact.rtt_ns_p50),
            .rtt_ns_p99 = ::fixy::mint_tagged<::fixy::tags::source::Calibrated>(fact.rtt_ns_p99),
            .drop_rate = ::fixy::mint_tagged<::fixy::tags::source::Calibrated>(fact.drop_rate),
        };
        ++edge_count_;
        return idx;
    }

    [[nodiscard]] constexpr bool record(DiscoverySourceStatus status) noexcept { return report_.push(status); }

    template <class Ctx>
        requires CtxFitsDiscoveryInit<Ctx>
    [[nodiscard]] constexpr TopologyGraph graph(Ctx const& ctx) const noexcept {
        return mint_topology_graph(ctx, nodes(), edges());
    }

private:
    // The firmware string is opaque, so the identity carries its hash.
    [[nodiscard]] static constexpr cog::VendorClaim<std::uint64_t>
    firmware_claim(DiscoveryNodeFact const& fact) noexcept {
        return ::fixy::mint_tagged<::fixy::tags::source::Vendor>(stable_discovery_hash(fact.firmware.value()));
    }

    std::array<DiscoveryNodeFact, MaxNodes> node_facts_{};
    std::array<DiscoveryEdgeFact, MaxEdges> edge_facts_{};
    std::array<cog::CogIdentity, MaxNodes> nodes_{};
    std::array<TopologyEdge, MaxEdges> edges_{};
    std::size_t node_count_ = 0;
    std::size_t edge_count_ = 0;
    DiscoveryReport report_{};
};

template <std::size_t MaxNodes, std::size_t MaxEdges, class Ctx>
    requires DiscoveryShape<MaxNodes, MaxEdges> && CtxFitsDiscoveryInit<Ctx>
[[nodiscard]] constexpr DiscoverySnapshot<MaxNodes, MaxEdges> mint_discovery_snapshot(Ctx const&) noexcept {
    return DiscoverySnapshot<MaxNodes, MaxEdges>{};
}

template <std::size_t MaxNodes, std::size_t MaxEdges, class Ctx>
    requires DiscoveryShape<MaxNodes, MaxEdges> && CtxFitsDiscoveryInit<Ctx>
[[nodiscard]] constexpr TopologyGraph
discover_local_topology(Ctx const& ctx, DiscoverySnapshot<MaxNodes, MaxEdges>& snapshot) noexcept {
    static_cast<void>(snapshot.record(DiscoverySourceStatus{
        .source = DiscoverySource::PcieLspci,
        .outcome = DiscoveryOutcome::NotAttempted,
        .error = DiscoveryError::UnsupportedSource,
    }));
    return snapshot.graph(ctx);
}

template <class Ctx>
    requires CtxFitsDiscoveryBg<Ctx>
[[nodiscard]] constexpr std::expected<DiscoverySourceStatus, DiscoveryError>
notify_rediscovery_trigger(Ctx const&, DiscoverySource source) noexcept {
    return DiscoverySourceStatus{
        .source = source,
        .outcome = DiscoveryOutcome::NotAttempted,
        .error = DiscoveryError::UnsupportedSource,
    };
}

[[nodiscard]] std::expected<DiscoverySourceStatus, DiscoveryError>
parse_lspci_vmm_tree(ExternalDiscoveryText text, DefaultDiscoverySnapshot& snapshot) noexcept;

[[nodiscard]] std::expected<DiscoverySourceStatus, DiscoveryError>
parse_ethtool_info(ExternalDiscoveryText text, DefaultDiscoverySnapshot& snapshot, std::uint16_t node_index) noexcept;

[[nodiscard]] std::expected<::fixy::Bits<cog::NicFeature>, DiscoveryError>
parse_ethtool_features(ExternalDiscoveryText text) noexcept;

[[nodiscard]] std::expected<DiscoverySourceStatus, DiscoveryError>
parse_lldp_neighbors(ExternalDiscoveryText text, DefaultDiscoverySnapshot& snapshot) noexcept;

}  // namespace crucible::topology
