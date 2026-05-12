#include <crucible/topology/Discovery.h>

#include <algorithm>
#include <charconv>
#include <system_error>
#include <string_view>
#include <utility>

namespace crucible::topology {

namespace {

[[nodiscard]] constexpr std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' ||
                         s.front() == '\r' || s.front() == '\n')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' ||
                         s.back() == '\r' || s.back() == '\n')) {
        s.remove_suffix(1);
    }
    return s;
}

[[nodiscard]] constexpr std::pair<std::string_view, std::string_view>
split_key_value(std::string_view line) noexcept {
    const std::size_t colon = line.find(':');
    if (colon != std::string_view::npos) {
        return {trim(line.substr(0, colon)), trim(line.substr(colon + 1))};
    }
    const std::size_t tab = line.find('\t');
    if (tab != std::string_view::npos) {
        return {trim(line.substr(0, tab)), trim(line.substr(tab + 1))};
    }
    return {trim(line), std::string_view{}};
}

[[nodiscard]] constexpr bool contains_ci(std::string_view haystack,
                                         std::string_view needle) noexcept {
    if (needle.empty() || needle.size() > haystack.size()) {
        return false;
    }
    auto lower = [](char c) constexpr noexcept {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
    };
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        bool matched = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (lower(haystack[i + j]) != lower(needle[j])) {
                matched = false;
                break;
            }
        }
        if (matched) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] constexpr DiscoveryNodeKind node_kind_from_lspci(std::string_view cls,
                                                               std::string_view dev) noexcept {
    if (contains_ci(cls, "ethernet") || contains_ci(cls, "network")) {
        return DiscoveryNodeKind::NicPort;
    }
    if (contains_ci(cls, "vga") || contains_ci(cls, "3d") ||
        contains_ci(dev, "gpu") || contains_ci(dev, "nvidia") ||
        contains_ci(dev, "amd")) {
        return DiscoveryNodeKind::Gpu;
    }
    if (contains_ci(cls, "non-volatile") || contains_ci(dev, "nvme")) {
        return DiscoveryNodeKind::NvmeNamespace;
    }
    if (contains_ci(cls, "bridge")) {
        return DiscoveryNodeKind::PcieRoot;
    }
    return DiscoveryNodeKind::PcieLaneGroup;
}

[[nodiscard]] constexpr std::uint64_t parse_line_rate(std::string_view value) noexcept {
    std::uint64_t number = 0;
    auto first = value.data();
    auto last = value.data() + value.size();
    auto [ptr, ec] = std::from_chars(first, last, number);
    if (ec != std::errc{} || number == 0) {
        return 0;
    }
    std::string_view suffix{ptr, static_cast<std::size_t>(last - ptr)};
    if (contains_ci(suffix, "g")) {
        return number * 1000ull * 1000ull * 1000ull / 8ull;
    }
    if (contains_ci(suffix, "m")) {
        return number * 1000ull * 1000ull / 8ull;
    }
    return number;
}

template <std::size_t MaxNodes, std::size_t MaxEdges>
[[nodiscard]] std::expected<void, DiscoveryError>
flush_lspci_record(DiscoverySnapshot<MaxNodes, MaxEdges>& snapshot,
                   std::string_view slot,
                   std::string_view cls,
                   std::string_view vendor,
                   std::string_view device,
                   std::uint16_t& records,
                   std::uint16_t& admitted) noexcept {
    if (slot.empty() && cls.empty() && vendor.empty() && device.empty()) {
        return {};
    }
    ++records;
    DiscoveryNodeKind node_kind = node_kind_from_lspci(cls, device);
    DiscoveryNodeFact fact{
        .kind = cog_kind_from_discovery(node_kind),
        .vendor = tag_vendor_discovery_string(vendor),
        .model = tag_vendor_discovery_string(device),
        .bus_info = tag_vendor_discovery_string(slot),
    };
    fact.level = default_level_for(fact.kind);
    auto added = snapshot.add_node(fact);
    if (!added.has_value()) {
        return std::unexpected(added.error());
    }
    ++admitted;
    if (*added != 0) {
        DiscoveryEdgeFact edge{
            .from_node = 0,
            .to_node = *added,
            .kind = LinkKind::PciE,
        };
        auto edge_added = snapshot.add_edge(edge);
        if (!edge_added.has_value()) {
            return std::unexpected(edge_added.error());
        }
    }
    return {};
}

}  // namespace

std::string_view discovery_error_name(DiscoveryError error) noexcept {
    switch (error) {
        case DiscoveryError::EmptyInput:             return "EmptyInput";
        case DiscoveryError::MalformedRecord:        return "MalformedRecord";
        case DiscoveryError::TooManyNodes:           return "TooManyNodes";
        case DiscoveryError::TooManyEdges:           return "TooManyEdges";
        case DiscoveryError::InvalidNodeIndex:       return "InvalidNodeIndex";
        case DiscoveryError::InvalidInterfaceName:   return "InvalidInterfaceName";
        case DiscoveryError::MissingRequiredField:   return "MissingRequiredField";
        case DiscoveryError::UnsupportedSource:      return "UnsupportedSource";
        case DiscoveryError::RediscoveryRequiresBg:  return "RediscoveryRequiresBg";
        default:                                     return "<unknown DiscoveryError>";
    }
}

std::string_view discovery_source_name(DiscoverySource source) noexcept {
    switch (source) {
        case DiscoverySource::PcieLspci:       return "PcieLspci";
        case DiscoverySource::PcieSysfs:       return "PcieSysfs";
        case DiscoverySource::NicSysfs:        return "NicSysfs";
        case DiscoverySource::EthtoolInfo:     return "EthtoolInfo";
        case DiscoverySource::EthtoolFeatures: return "EthtoolFeatures";
        case DiscoverySource::Lldp:            return "Lldp";
        case DiscoverySource::Gpu:             return "Gpu";
        case DiscoverySource::Nvme:            return "Nvme";
        case DiscoverySource::NvSwitch:        return "NvSwitch";
        case DiscoverySource::Optical:         return "Optical";
        case DiscoverySource::Udev:            return "Udev";
        default:                               return "<unknown DiscoverySource>";
    }
}

std::string_view discovery_outcome_name(DiscoveryOutcome outcome) noexcept {
    switch (outcome) {
        case DiscoveryOutcome::NotAttempted: return "NotAttempted";
        case DiscoveryOutcome::Complete:     return "Complete";
        case DiscoveryOutcome::Partial:      return "Partial";
        case DiscoveryOutcome::Failed:       return "Failed";
        default:                             return "<unknown DiscoveryOutcome>";
    }
}

std::string_view discovery_node_kind_name(DiscoveryNodeKind kind) noexcept {
    switch (kind) {
        case DiscoveryNodeKind::Unknown:            return "Unknown";
        case DiscoveryNodeKind::Gpu:                return "Gpu";
        case DiscoveryNodeKind::NicPort:            return "NicPort";
        case DiscoveryNodeKind::NicCard:            return "NicCard";
        case DiscoveryNodeKind::NvSwitch:           return "NvSwitch";
        case DiscoveryNodeKind::NvmeNamespace:      return "NvmeNamespace";
        case DiscoveryNodeKind::NvmeDrive:          return "NvmeDrive";
        case DiscoveryNodeKind::PcieRoot:           return "PcieRoot";
        case DiscoveryNodeKind::PcieLaneGroup:      return "PcieLaneGroup";
        case DiscoveryNodeKind::OpticalTransceiver: return "OpticalTransceiver";
        default:                                    return "<unknown DiscoveryNodeKind>";
    }
}

std::expected<DiscoverySourceStatus, DiscoveryError>
parse_lspci_vmm_tree(ExternalDiscoveryText text,
                     DefaultDiscoverySnapshot& snapshot) noexcept {
    std::string_view input = text.value();
    if (trim(input).empty()) {
        return std::unexpected(DiscoveryError::EmptyInput);
    }

    std::uint16_t records = 0;
    std::uint16_t admitted = 0;
    std::string_view slot{};
    std::string_view cls{};
    std::string_view vendor{};
    std::string_view device{};

    while (!input.empty()) {
        const std::size_t nl = input.find('\n');
        std::string_view line = nl == std::string_view::npos
            ? input
            : input.substr(0, nl);
        input = nl == std::string_view::npos ? std::string_view{} : input.substr(nl + 1);
        line = trim(line);
        if (line.empty()) {
            auto flushed = flush_lspci_record(snapshot, slot, cls, vendor,
                                              device, records, admitted);
            if (!flushed.has_value()) {
                return std::unexpected(flushed.error());
            }
            slot = cls = vendor = device = {};
            continue;
        }
        auto [key, value] = split_key_value(line);
        if (key == "Slot") {
            slot = value;
        } else if (key == "Class") {
            cls = value;
        } else if (key == "Vendor") {
            vendor = value;
        } else if (key == "Device") {
            device = value;
        }
    }
    auto flushed = flush_lspci_record(snapshot, slot, cls, vendor,
                                      device, records, admitted);
    if (!flushed.has_value()) {
        return std::unexpected(flushed.error());
    }
    DiscoverySourceStatus status{
        .source = DiscoverySource::PcieLspci,
        .outcome = admitted == records ? DiscoveryOutcome::Complete
                                       : DiscoveryOutcome::Partial,
        .records_seen = records,
        .records_admitted = admitted,
        .error = admitted == records ? DiscoveryError::EmptyInput
                                     : DiscoveryError::MalformedRecord,
    };
    static_cast<void>(snapshot.record(status));
    return status;
}

std::expected<DiscoverySourceStatus, DiscoveryError>
parse_ethtool_info(ExternalDiscoveryText text,
                   DefaultDiscoverySnapshot& snapshot,
                   std::uint16_t node_index) noexcept {
    if (node_index >= snapshot.node_count()) {
        return std::unexpected(DiscoveryError::InvalidNodeIndex);
    }
    std::string_view input = text.value();
    if (trim(input).empty()) {
        return std::unexpected(DiscoveryError::EmptyInput);
    }

    std::uint16_t records = 0;
    std::string_view driver{};
    std::string_view firmware{};
    std::string_view bus{};
    while (!input.empty()) {
        const std::size_t nl = input.find('\n');
        std::string_view line = nl == std::string_view::npos
            ? input
            : input.substr(0, nl);
        input = nl == std::string_view::npos ? std::string_view{} : input.substr(nl + 1);
        auto [key, value] = split_key_value(line);
        if (key == "driver") {
            driver = value;
            ++records;
        } else if (key == "firmware-version") {
            firmware = value;
            ++records;
        } else if (key == "bus-info") {
            bus = value;
            ++records;
        }
    }

    DiscoveryNodeFact fact = snapshot.node_facts()[node_index];
    fact.driver = tag_vendor_discovery_string(driver);
    fact.firmware = tag_vendor_discovery_string(firmware);
    fact.bus_info = bus.empty() ? fact.bus_info : tag_vendor_discovery_string(bus);
    auto updated = snapshot.update_node(node_index, fact);
    if (!updated.has_value()) {
        return std::unexpected(updated.error());
    }
    DiscoverySourceStatus status{
        .source = DiscoverySource::EthtoolInfo,
        .outcome = records == 0 ? DiscoveryOutcome::Failed : DiscoveryOutcome::Complete,
        .records_seen = records,
        .records_admitted = records,
        .error = records == 0 ? DiscoveryError::MissingRequiredField
                              : DiscoveryError::EmptyInput,
    };
    static_cast<void>(snapshot.record(status));
    return status;
}

std::expected<safety::Bits<cog::NicFeature>, DiscoveryError>
parse_ethtool_features(ExternalDiscoveryText text) noexcept {
    std::string_view input = text.value();
    if (trim(input).empty()) {
        return std::unexpected(DiscoveryError::EmptyInput);
    }
    safety::Bits<cog::NicFeature> bits{};
    while (!input.empty()) {
        const std::size_t nl = input.find('\n');
        std::string_view line = nl == std::string_view::npos
            ? input
            : input.substr(0, nl);
        input = nl == std::string_view::npos ? std::string_view{} : input.substr(nl + 1);
        auto [key, value] = split_key_value(line);
        const bool enabled = contains_ci(value, "on");
        if (!enabled) {
            continue;
        }
        if (contains_ci(key, "tcp-segmentation-offload")) {
            bits.set(cog::NicFeature::Tso);
        } else if (contains_ci(key, "generic-segmentation-offload")) {
            bits.set(cog::NicFeature::Gso);
        } else if (contains_ci(key, "generic-receive-offload")) {
            bits.set(cog::NicFeature::Gro);
        } else if (contains_ci(key, "large-receive-offload")) {
            bits.set(cog::NicFeature::Lro);
        } else if (contains_ci(key, "rx-vlan-offload")) {
            bits.set(cog::NicFeature::Rss);
        } else if (contains_ci(key, "tls-hw-tx-offload")) {
            bits.set(cog::NicFeature::KtlsOffload);
        } else if (contains_ci(key, "hw-tc-offload")) {
            bits.set(cog::NicFeature::TcEbpf);
        }
    }
    return bits;
}

std::expected<DiscoverySourceStatus, DiscoveryError>
parse_lldp_neighbors(ExternalDiscoveryText text,
                     DefaultDiscoverySnapshot& snapshot) noexcept {
    std::string_view input = text.value();
    if (trim(input).empty()) {
        return std::unexpected(DiscoveryError::EmptyInput);
    }
    if (snapshot.node_count() == 0) {
        return std::unexpected(DiscoveryError::InvalidNodeIndex);
    }

    std::uint16_t records = 0;
    std::uint16_t admitted = 0;
    std::string_view iface{};
    std::string_view sys_name{};
    std::string_view port_id{};
    std::string_view line_rate{};
    auto flush = [&]() noexcept -> std::expected<void, DiscoveryError> {
        if (iface.empty() && sys_name.empty() && port_id.empty()) {
            return {};
        }
        ++records;
        DiscoveryNodeFact peer{
            .kind = cog::CogKind::NicPort,
            .vendor = tag_vendor_discovery_string("lldp"),
            .model = tag_vendor_discovery_string(sys_name),
            .bus_info = tag_vendor_discovery_string(port_id),
        };
        auto idx = snapshot.add_node(peer);
        if (!idx.has_value()) {
            return std::unexpected(idx.error());
        }
        auto edge = snapshot.add_edge(DiscoveryEdgeFact{
            .from_node = 0,
            .to_node = *idx,
            .kind = LinkKind::Ethernet,
            .bandwidth_bytes_per_sec = parse_line_rate(line_rate),
        });
        if (!edge.has_value()) {
            return std::unexpected(edge.error());
        }
        ++admitted;
        return {};
    };

    while (!input.empty()) {
        const std::size_t nl = input.find('\n');
        std::string_view line = nl == std::string_view::npos
            ? input
            : input.substr(0, nl);
        input = nl == std::string_view::npos ? std::string_view{} : input.substr(nl + 1);
        line = trim(line);
        if (line.empty()) {
            auto flushed = flush();
            if (!flushed.has_value()) {
                return std::unexpected(flushed.error());
            }
            iface = sys_name = port_id = line_rate = {};
            continue;
        }
        auto [key, value] = split_key_value(line);
        if (key == "Interface") {
            iface = value;
        } else if (key == "LineRate") {
            line_rate = value;
        } else if (key == "SysName" || key == "SysName:") {
            sys_name = value;
        } else if (key == "PortID" || key == "PortID:") {
            port_id = value;
        }
    }
    auto flushed = flush();
    if (!flushed.has_value()) {
        return std::unexpected(flushed.error());
    }

    DiscoverySourceStatus status{
        .source = DiscoverySource::Lldp,
        .outcome = admitted == 0 ? DiscoveryOutcome::Failed : DiscoveryOutcome::Complete,
        .records_seen = records,
        .records_admitted = admitted,
        .error = admitted == 0 ? DiscoveryError::MissingRequiredField
                               : DiscoveryError::EmptyInput,
    };
    static_cast<void>(snapshot.record(status));
    return status;
}

}  // namespace crucible::topology
