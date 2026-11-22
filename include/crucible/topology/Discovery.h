#pragma once

// The graph that graph() returns holds non-owning views.  A snapshot owns the
// node and edge storage, and the graph is only a view over it.  A graph built
// over storage local to a function would dangle at the return.

#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/TargetCaps.h>
#include <crucible/topology/TopologyGraph.h>
#include <fixy/Bits.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/core/Array.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace crucible::topology {

// The text a discovery parser reads comes from a tool or a peer, never from
// this process.  A parser takes only this type, so a caller must name the
// source with mint_tagged before the parser sees a byte.
using ExternalDiscoveryText = ::fixy::Tagged<::fixy::TextView, ::fixy::tags::source::External>;

// The largest count of the characters of one vendor text of a discovery fact.
// The longest text of each source fits.  lldpctl writes an LLDP port id of up
// to 255 octets after the name of its subtype and a space, and an LLDP system
// name holds up to 255 octets.  The longest name of pci.ids holds 119
// characters, and ethtool writes each field in at most 31 characters.  The
// count is a multiple of 8, so a text holds no padding byte.
inline constexpr std::size_t vendor_discovery_text_capacity = 272;

// The characters of one vendor text, which a fact owns.  A fact thus holds no
// view of the text of its tool, and it outlives that text.
using VendorDiscoveryText = ::fixy::FixedText<vendor_discovery_text_capacity>;
using VendorDiscoveryString = cog::VendorClaim<VendorDiscoveryText>;

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

// At most max_statuses statuses, in the order of their pushes.  push() is
// the only writer, and a push into a full report keeps the report as it is.
class DiscoveryReport {
public:
    static constexpr std::size_t max_statuses = 16;

    constexpr DiscoveryReport() noexcept = default;

    [[nodiscard]] constexpr ::fixy::View<DiscoverySourceStatus const> view() const noexcept { return statuses_.view(); }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return statuses_.size(); }

    [[nodiscard]] constexpr bool empty() const noexcept { return statuses_.size() == 0; }

    [[nodiscard]] constexpr bool full() const noexcept { return statuses_.size() >= max_statuses; }

    [[nodiscard]] constexpr bool push(DiscoverySourceStatus status) noexcept {
        return statuses_.append(status).is_ok();
    }

private:
    ::fixy::FixedList<DiscoverySourceStatus, max_statuses> statuses_{};
};

// Each pad member of the two facts holds bytes that the compiler would
// otherwise pad, so a fact has no padding byte.  A snapshot holds 64 node
// facts and 128 edge facts, and with -ftrivial-auto-var-init=zero GCC writes
// zero to each padding hole of each fact in an automatic snapshot, one store
// for each hole.  test/test_topology_discovery.cpp makes sure that the facts
// have no padding byte.  A node fact owns each of its five vendor texts.  Its
// one borrow is the view of the GPU peers in nic_caps, which no parser fills.
struct DiscoveryNodeFact {
    cog::Uuid uuid{};
    cog::CogLevel level = cog::CogLevel::L0_Atomic;
    cog::CogKind kind = cog::CogKind::Gpu;
    ::fixy::FixedArray<std::uint8_t, 6> pad_after_kind{};
    VendorDiscoveryString vendor{};
    VendorDiscoveryString model{};
    VendorDiscoveryString driver{};
    VendorDiscoveryString firmware{};
    VendorDiscoveryString bus_info{};
    std::int16_t numa_node = -1;
    ::fixy::FixedArray<std::uint8_t, 6> pad_after_numa_node{};
    cog::NicPortTargetCaps nic_caps{};
};

struct DiscoveryEdgeFact {
    std::uint16_t from_node = 0;
    std::uint16_t to_node = 0;
    LinkKind kind = LinkKind::Unknown;
    ::fixy::FixedArray<std::uint8_t, 3> pad_after_kind{};
    std::uint64_t bandwidth_bytes_per_sec = 0;
    std::uint64_t rtt_ns_p50 = 0;
    std::uint64_t rtt_ns_p99 = 0;
    float drop_rate = 0.0f;
    ::fixy::FixedArray<std::uint8_t, 4> pad_tail{};
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

// foundation::core::enum_name_text gives the log spelling of the four enums
// above.

[[nodiscard]] constexpr ExternalDiscoveryText tag_external_discovery_text(::fixy::TextView text) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::External>(text);
}

// The vendor string of a text that the caller owns.
[[nodiscard]] constexpr VendorDiscoveryString tag_vendor_discovery_string(VendorDiscoveryText const& text) noexcept {
    return ::fixy::mint_tagged<::fixy::tags::source::Vendor>(text);
}

// A copy of a text of a tool as a vendor string, or no value when the text
// holds more than vendor_discovery_text_capacity characters.  A parser refuses
// the record of such a text, and it cuts no text: the uuid of a node hashes
// three of its texts, so a cut could give two devices one uuid.  Complexity:
// one copy of each character.
[[nodiscard]] constexpr ::fixy::Option<VendorDiscoveryString>
copy_vendor_discovery_string(::fixy::TextView text) noexcept {
    VendorDiscoveryText copy{};
    if (copy.append(text).is_err()) return ::fixy::none;
    return ::fixy::some(tag_vendor_discovery_string(copy));
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

[[nodiscard]] constexpr std::uint64_t stable_discovery_hash(::fixy::TextView a, ::fixy::TextView b = {},
                                                            ::fixy::TextView c = {}) noexcept {
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&h](::fixy::TextView s) constexpr noexcept {
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
//
// The snapshot owns each vendor text: each node fact holds a copy.  The
// vendor and model views of each identity name the texts of the fact at the
// same index in this snapshot.  So a snapshot never copies and never moves: a
// copy or a moved snapshot would hold views into its source.  The mint gives
// the snapshot as a value that the caller names, and no later move exists.
// HoldsNoBorrow holds for each vendor text of a fact, and not for a snapshot:
// an identity holds views by its type, its vendor and model views into the
// snapshot itself, and its parent, children and neighbor views.
template <std::size_t MaxNodes, std::size_t MaxEdges>
    requires DiscoveryShape<MaxNodes, MaxEdges>
class DiscoverySnapshot {
    template <std::size_t N, std::size_t E, class Ctx>
        requires DiscoveryShape<N, E> && CtxFitsDiscoveryInit<Ctx>
    friend constexpr DiscoverySnapshot<N, E> mint_discovery_snapshot(Ctx const&) noexcept;

    constexpr DiscoverySnapshot() noexcept = default;

public:
    DiscoverySnapshot(DiscoverySnapshot const&) =
        delete("each identity views the texts of the facts of its snapshot, so a copy would view its source");
    DiscoverySnapshot& operator=(DiscoverySnapshot const&) =
        delete("each identity views the texts of the facts of its snapshot, so a copy would view its source");
    DiscoverySnapshot(DiscoverySnapshot&&) =
        delete("each identity views the texts of the facts of its snapshot, so a moved snapshot would view its source");
    DiscoverySnapshot& operator=(DiscoverySnapshot&&) =
        delete("each identity views the texts of the facts of its snapshot, so a moved snapshot would view its source");

    // Each count is at most the capacity of its table, so each window fits.
    [[nodiscard]] constexpr ::fixy::View<cog::CogIdentity const> nodes() const noexcept {
        return nodes_.view().window(0, node_count_).value_or(::fixy::View<cog::CogIdentity const>{});
    }

    [[nodiscard]] constexpr ::fixy::View<TopologyEdge const> edges() const noexcept {
        return edges_.view().window(0, edge_count_).value_or(::fixy::View<TopologyEdge const>{});
    }

    [[nodiscard]] constexpr ::fixy::View<DiscoveryNodeFact const> node_facts() const noexcept {
        return node_facts_.view().window(0, node_count_).value_or(::fixy::View<DiscoveryNodeFact const>{});
    }

    [[nodiscard]] constexpr ::fixy::View<DiscoveryEdgeFact const> edge_facts() const noexcept {
        return edge_facts_.view().window(0, edge_count_).value_or(::fixy::View<DiscoveryEdgeFact const>{});
    }

    [[nodiscard]] constexpr std::size_t node_count() const noexcept { return node_count_; }

    [[nodiscard]] constexpr std::size_t edge_count() const noexcept { return edge_count_; }

    [[nodiscard]] constexpr DiscoveryReport const& report() const noexcept { return report_; }

    [[nodiscard]] constexpr ::fixy::Result<std::uint16_t, DiscoveryError> add_node(DiscoveryNodeFact fact) noexcept {
        if (node_count_ == MaxNodes) {
            return ::fixy::err(DiscoveryError::TooManyNodes);
        }
        const std::uint16_t idx = static_cast<std::uint16_t>(node_count_);
        ::fixy::Index<MaxNodes> const slot =
            ::fixy::Index<MaxNodes>::check(node_count_).expect("a snapshot that is not full has a free node slot");
        if (fact.uuid.is_zero()) {
            const std::uint64_t lo = stable_discovery_hash(fact.bus_info.value().view(), fact.vendor.value().view(),
                                                           fact.model.value().view());
            fact.uuid = cog::Uuid{0xD15C0111ull, lo};
        }
        fact.level = fact.level == cog::CogLevel::L0_Atomic ? default_level_for(fact.kind) : fact.level;
        node_facts_.with_element(slot, [&fact](DiscoveryNodeFact& held) noexcept { held = fact; });
        DiscoveryNodeFact const& held = node_facts_.at(slot);
        nodes_.with_element(slot, [&held](cog::CogIdentity& node) noexcept {
            node = cog::CogIdentity{
                .uuid = held.uuid,
                .level = held.level,
                .kind = held.kind,
                .vendor = vendor_view(held.vendor),
                .model = vendor_view(held.model),
                .firmware_revision = firmware_claim(held),
                .bios_revision = {},
            };
        });
        ++node_count_;
        return idx;
    }

    [[nodiscard]] constexpr ::fixy::Result<::fixy::Unit, DiscoveryError> update_node(std::uint16_t idx,
                                                                                     DiscoveryNodeFact fact) noexcept {
        if (idx >= node_count_) {
            return ::fixy::err(DiscoveryError::InvalidNodeIndex);
        }
        ::fixy::Index<MaxNodes> const slot =
            ::fixy::Index<MaxNodes>::check(idx).expect("an index below the node count is below the capacity");
        node_facts_.with_element(slot, [&fact](DiscoveryNodeFact& held) noexcept { held = fact; });
        DiscoveryNodeFact const& held = node_facts_.at(slot);
        auto const firmware_revision = firmware_claim(held);
        nodes_.with_element(slot, [&held, firmware_revision](cog::CogIdentity& node) noexcept {
            node.uuid = held.uuid;
            node.level = held.level;
            node.kind = held.kind;
            node.vendor = vendor_view(held.vendor);
            node.model = vendor_view(held.model);
            node.firmware_revision = firmware_revision;
        });
        return ::fixy::Unit{};
    }

    [[nodiscard]] constexpr ::fixy::Result<std::uint16_t, DiscoveryError> add_edge(DiscoveryEdgeFact fact) noexcept {
        if (edge_count_ == MaxEdges) {
            return ::fixy::err(DiscoveryError::TooManyEdges);
        }
        if (fact.from_node >= node_count_ || fact.to_node >= node_count_) {
            return ::fixy::err(DiscoveryError::InvalidNodeIndex);
        }
        const std::uint16_t idx = static_cast<std::uint16_t>(edge_count_);
        ::fixy::Index<MaxEdges> const slot =
            ::fixy::Index<MaxEdges>::check(edge_count_).expect("a snapshot that is not full has a free edge slot");
        edge_facts_.with_element(slot, [&fact](DiscoveryEdgeFact& held) noexcept { held = fact; });
        TopologyEdge const edge{
            .id = EdgeId{idx},
            .kind = fact.kind,
            .peer = NodeId{fact.to_node},
            .bandwidth_bytes_per_sec =
                ::fixy::mint_tagged<::fixy::tags::source::Calibrated>(fact.bandwidth_bytes_per_sec),
            .rtt_ns_p50 = ::fixy::mint_tagged<::fixy::tags::source::Calibrated>(fact.rtt_ns_p50),
            .rtt_ns_p99 = ::fixy::mint_tagged<::fixy::tags::source::Calibrated>(fact.rtt_ns_p99),
            .drop_rate = ::fixy::mint_tagged<::fixy::tags::source::Calibrated>(fact.drop_rate),
        };
        edges_.with_element(slot, [&edge](TopologyEdge& held) noexcept { held = edge; });
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
        return ::fixy::mint_tagged<::fixy::tags::source::Vendor>(stable_discovery_hash(fact.firmware.value().view()));
    }

    // The view of a vendor text of a fact of this snapshot, for an identity of
    // this snapshot.  The snapshot never moves, so the view lives as long as
    // the snapshot.
    [[nodiscard]] static constexpr cog::VendorClaim<::fixy::TextView> vendor_view(VendorDiscoveryString const& text
                                                                                  CRUCIBLE_LIFETIMEBOUND) noexcept {
        return ::fixy::mint_tagged<::fixy::tags::source::Vendor>(text.value().view());
    }
    static cog::VendorClaim<::fixy::TextView> vendor_view(VendorDiscoveryString const&& text) =
        delete("a view of a temporary vendor text dangles at the end of the full expression");

    ::fixy::FixedArray<DiscoveryNodeFact, MaxNodes> node_facts_{};
    ::fixy::FixedArray<DiscoveryEdgeFact, MaxEdges> edge_facts_{};
    ::fixy::FixedArray<cog::CogIdentity, MaxNodes> nodes_{};
    ::fixy::FixedArray<TopologyEdge, MaxEdges> edges_{};
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
[[nodiscard]] constexpr ::fixy::Result<DiscoverySourceStatus, DiscoveryError>
notify_rediscovery_trigger(Ctx const&, DiscoverySource source) noexcept {
    return DiscoverySourceStatus{
        .source = source,
        .outcome = DiscoveryOutcome::NotAttempted,
        .error = DiscoveryError::UnsupportedSource,
    };
}

[[nodiscard]] ::fixy::Result<DiscoverySourceStatus, DiscoveryError>
parse_lspci_vmm_tree(ExternalDiscoveryText text, DefaultDiscoverySnapshot& snapshot) noexcept;

[[nodiscard]] ::fixy::Result<DiscoverySourceStatus, DiscoveryError>
parse_ethtool_info(ExternalDiscoveryText text, DefaultDiscoverySnapshot& snapshot, std::uint16_t node_index) noexcept;

[[nodiscard]] ::fixy::Result<::fixy::Bits<cog::NicFeature>, DiscoveryError>
parse_ethtool_features(ExternalDiscoveryText text) noexcept;

[[nodiscard]] ::fixy::Result<DiscoverySourceStatus, DiscoveryError>
parse_lldp_neighbors(ExternalDiscoveryText text, DefaultDiscoverySnapshot& snapshot) noexcept;

}  // namespace crucible::topology
