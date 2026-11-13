#include <crucible/topology/Discovery.h>

#include <crucible/topology/TextFields.h>

namespace crucible::topology {

namespace {

using ::fixy::TextView;

// A record line of a discovery tool is a key, a colon or a tab, and a value.
[[nodiscard]] constexpr text::Field split_key_value(TextView line) noexcept { return text::split_field(line, ":\t"); }

[[nodiscard]] constexpr DiscoveryNodeKind node_kind_from_lspci(TextView cls, TextView dev) noexcept {
    if (text::contains_ignoring_case(cls, "ethernet") || text::contains_ignoring_case(cls, "network")) {
        return DiscoveryNodeKind::NicPort;
    }
    if (text::contains_ignoring_case(cls, "vga") || text::contains_ignoring_case(cls, "3d")
        || text::contains_ignoring_case(dev, "gpu") || text::contains_ignoring_case(dev, "nvidia")
        || text::contains_ignoring_case(dev, "amd")) {
        return DiscoveryNodeKind::Gpu;
    }
    if (text::contains_ignoring_case(cls, "non-volatile") || text::contains_ignoring_case(dev, "nvme")) {
        return DiscoveryNodeKind::NvmeNamespace;
    }
    if (text::contains_ignoring_case(cls, "bridge")) {
        return DiscoveryNodeKind::PcieRoot;
    }
    return DiscoveryNodeKind::PcieLaneGroup;
}

// The bytes per second of a line rate such as "100G", "25000M" or a plain
// count of bits per second.  Zero when the text starts with no number or the
// number is zero.
[[nodiscard]] constexpr std::uint64_t parse_line_rate(TextView value) noexcept {
    std::uint64_t const number = text::leading_u64(value).value_or(0);
    if (number == 0) {
        return 0;
    }
    TextView const suffix = text::tail(value, text::leading_digits(value));
    if (text::contains_ignoring_case(suffix, "g")) {
        return number * 1000ull * 1000ull * 1000ull / 8ull;
    }
    if (text::contains_ignoring_case(suffix, "m")) {
        return number * 1000ull * 1000ull / 8ull;
    }
    return number;
}

template <std::size_t MaxNodes, std::size_t MaxEdges>
[[nodiscard]] ::fixy::Result<::fixy::Unit, DiscoveryError>
flush_lspci_record(DiscoverySnapshot<MaxNodes, MaxEdges>& snapshot, TextView slot, TextView cls, TextView vendor,
                   TextView device, std::uint16_t& records, std::uint16_t& admitted) noexcept {
    if (slot.size() == 0 && cls.size() == 0 && vendor.size() == 0 && device.size() == 0) {
        return ::fixy::Unit{};
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
    CRUCIBLE_TRY(added, snapshot.add_node(fact));
    ++admitted;
    if (added != 0) {
        DiscoveryEdgeFact edge{
            .from_node = 0,
            .to_node = added,
            .kind = LinkKind::PciE,
        };
        CRUCIBLE_TRY(_, snapshot.add_edge(edge));
    }
    return ::fixy::Unit{};
}

}  // namespace

::fixy::Result<DiscoverySourceStatus, DiscoveryError>
parse_lspci_vmm_tree(ExternalDiscoveryText text, DefaultDiscoverySnapshot& snapshot) noexcept {
    TextView input = text.value();
    if (text::trim(input).size() == 0) {
        return ::fixy::err(DiscoveryError::EmptyInput);
    }

    std::uint16_t records = 0;
    std::uint16_t admitted = 0;
    TextView slot{};
    TextView cls{};
    TextView vendor{};
    TextView device{};

    while (input.size() != 0) {
        TextView const line = text::trim(text::take_line(input));
        if (line.size() == 0) {
            CRUCIBLE_TRY(_, flush_lspci_record(snapshot, slot, cls, vendor, device, records, admitted));
            slot = cls = vendor = device = TextView{};
            continue;
        }
        text::Field const field = split_key_value(line);
        if (field.key == "Slot") {
            slot = field.value;
        } else if (field.key == "Class") {
            cls = field.value;
        } else if (field.key == "Vendor") {
            vendor = field.value;
        } else if (field.key == "Device") {
            device = field.value;
        }
    }
    CRUCIBLE_TRY(_, flush_lspci_record(snapshot, slot, cls, vendor, device, records, admitted));
    DiscoverySourceStatus status{
        .source = DiscoverySource::PcieLspci,
        .outcome = admitted == records ? DiscoveryOutcome::Complete : DiscoveryOutcome::Partial,
        .records_seen = records,
        .records_admitted = admitted,
        .error = admitted == records ? DiscoveryError::EmptyInput : DiscoveryError::MalformedRecord,
    };
    static_cast<void>(snapshot.record(status));
    return status;
}

::fixy::Result<DiscoverySourceStatus, DiscoveryError>
parse_ethtool_info(ExternalDiscoveryText text, DefaultDiscoverySnapshot& snapshot, std::uint16_t node_index) noexcept {
    if (node_index >= snapshot.node_count()) {
        return ::fixy::err(DiscoveryError::InvalidNodeIndex);
    }
    TextView input = text.value();
    if (text::trim(input).size() == 0) {
        return ::fixy::err(DiscoveryError::EmptyInput);
    }

    std::uint16_t records = 0;
    TextView driver{};
    TextView firmware{};
    TextView bus{};
    while (input.size() != 0) {
        text::Field const field = split_key_value(text::take_line(input));
        if (field.key == "driver") {
            driver = field.value;
            ++records;
        } else if (field.key == "firmware-version") {
            firmware = field.value;
            ++records;
        } else if (field.key == "bus-info") {
            bus = field.value;
            ++records;
        }
    }

    DiscoveryNodeFact fact = *snapshot.node_facts().get(node_index).expect("the node index is below the node count");
    fact.driver = tag_vendor_discovery_string(driver);
    fact.firmware = tag_vendor_discovery_string(firmware);
    fact.bus_info = bus.size() == 0 ? fact.bus_info : tag_vendor_discovery_string(bus);
    CRUCIBLE_TRY(_, snapshot.update_node(node_index, fact));
    DiscoverySourceStatus status{
        .source = DiscoverySource::EthtoolInfo,
        .outcome = records == 0 ? DiscoveryOutcome::Failed : DiscoveryOutcome::Complete,
        .records_seen = records,
        .records_admitted = records,
        .error = records == 0 ? DiscoveryError::MissingRequiredField : DiscoveryError::EmptyInput,
    };
    static_cast<void>(snapshot.record(status));
    return status;
}

::fixy::Result<::fixy::Bits<cog::NicFeature>, DiscoveryError>
parse_ethtool_features(ExternalDiscoveryText text) noexcept {
    TextView input = text.value();
    if (text::trim(input).size() == 0) {
        return ::fixy::err(DiscoveryError::EmptyInput);
    }
    ::fixy::Bits<cog::NicFeature> bits{};
    while (input.size() != 0) {
        text::Field const field = split_key_value(text::take_line(input));
        if (!text::contains_ignoring_case(field.value, "on")) {
            continue;
        }
        if (text::contains_ignoring_case(field.key, "tcp-segmentation-offload")) {
            bits.set(cog::NicFeature::Tso);
        } else if (text::contains_ignoring_case(field.key, "generic-segmentation-offload")) {
            bits.set(cog::NicFeature::Gso);
        } else if (text::contains_ignoring_case(field.key, "generic-receive-offload")) {
            bits.set(cog::NicFeature::Gro);
        } else if (text::contains_ignoring_case(field.key, "large-receive-offload")) {
            bits.set(cog::NicFeature::Lro);
        } else if (text::contains_ignoring_case(field.key, "rx-vlan-offload")) {
            bits.set(cog::NicFeature::Rss);
        } else if (text::contains_ignoring_case(field.key, "hw-tc-offload")) {
            bits.set(cog::NicFeature::TcEbpf);
        }
    }
    return bits;
}

::fixy::Result<DiscoverySourceStatus, DiscoveryError>
parse_lldp_neighbors(ExternalDiscoveryText text, DefaultDiscoverySnapshot& snapshot) noexcept {
    TextView input = text.value();
    if (text::trim(input).size() == 0) {
        return ::fixy::err(DiscoveryError::EmptyInput);
    }
    if (snapshot.node_count() == 0) {
        return ::fixy::err(DiscoveryError::InvalidNodeIndex);
    }

    std::uint16_t records = 0;
    std::uint16_t admitted = 0;
    TextView iface{};
    TextView sys_name{};
    TextView port_id{};
    TextView line_rate{};
    auto flush = [&]() noexcept -> ::fixy::Result<::fixy::Unit, DiscoveryError> {
        if (iface.size() == 0 && sys_name.size() == 0 && port_id.size() == 0) {
            return ::fixy::Unit{};
        }
        ++records;
        DiscoveryNodeFact peer{
            .kind = cog::CogKind::NicPort,
            .vendor = tag_vendor_discovery_string("lldp"),
            .model = tag_vendor_discovery_string(sys_name),
            .bus_info = tag_vendor_discovery_string(port_id),
        };
        CRUCIBLE_TRY(idx, snapshot.add_node(peer));
        CRUCIBLE_TRY(_, snapshot.add_edge(DiscoveryEdgeFact{
                            .from_node = 0,
                            .to_node = idx,
                            .kind = LinkKind::Ethernet,
                            .bandwidth_bytes_per_sec = parse_line_rate(line_rate),
                        }));
        ++admitted;
        return ::fixy::Unit{};
    };

    while (input.size() != 0) {
        TextView const line = text::trim(text::take_line(input));
        if (line.size() == 0) {
            CRUCIBLE_TRY(_, flush());
            iface = sys_name = port_id = line_rate = TextView{};
            continue;
        }
        text::Field const field = split_key_value(line);
        if (field.key == "Interface") {
            iface = field.value;
        } else if (field.key == "LineRate") {
            line_rate = field.value;
        } else if (field.key == "SysName" || field.key == "SysName:") {
            sys_name = field.value;
        } else if (field.key == "PortID" || field.key == "PortID:") {
            port_id = field.value;
        }
    }
    CRUCIBLE_TRY(_, flush());

    DiscoverySourceStatus status{
        .source = DiscoverySource::Lldp,
        .outcome = admitted == 0 ? DiscoveryOutcome::Failed : DiscoveryOutcome::Complete,
        .records_seen = records,
        .records_admitted = admitted,
        .error = admitted == 0 ? DiscoveryError::MissingRequiredField : DiscoveryError::EmptyInput,
    };
    static_cast<void>(snapshot.record(status));
    return status;
}

}  // namespace crucible::topology
