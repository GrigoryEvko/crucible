#pragma once

// Text that discovery reads from host tools: the lspci tree, ethtool
// driver info and feature lists, and lldpctl neighbours.  The first byte
// picks the parser and the rest is the text.  Every parser must survive any
// text, and a snapshot that a parser fills keeps its node count in range.

#include "../harness.h"

#include <crucible/topology/Discovery.h>

namespace crucible::fuzz::boundary {

[[nodiscard]] inline Seeds seeds_discovery_text() {
    static constexpr std::string_view kTexts[] = {
        "-[0000:00]-+-00.0\n           +-01.0-[01]----00.0\n           \\-02.0\n",
        "driver: mlx5_core\nversion: 6.1\nfirmware-version: 22.39.1002\nbus-info: 0000:65:00.0\n",
        "tcp-segmentation-offload: on\ngeneric-receive-offload: on\nlarge-receive-offload: off [fixed]\n",
        "Interface: eth0, via: LLDP\n  SysName: tor-a\n  PortID: ifname swp17\n  LineRate: 100G\n",
    };
    Seeds seeds;
    for (std::uint8_t which = 0; which < std::size(kTexts); ++which) {
        std::vector<std::uint8_t> seed{which};
        append_bytes(seed, text_bytes(kTexts[which]));
        seeds.push_back(std::move(seed));
    }
    return seeds;
}

inline void run_discovery_text(std::span<const std::uint8_t> bytes) {
    ByteCursor cursor{bytes};
    const auto which = cursor.take<std::uint8_t>();
    const auto text = topology::tag_external_discovery_text(as_text(cursor.rest()));

    auto snapshot = topology::DefaultDiscoverySnapshot{};
    switch (which % 4) {
        case 0:
            (void)topology::parse_lspci_vmm_tree(text, snapshot);
            break;
        case 1: {
            topology::DiscoveryNodeFact local{.kind = cog::CogKind::NicPort};
            if (auto index = snapshot.add_node(local)) (void)topology::parse_ethtool_info(text, snapshot, *index);
            break;
        }
        case 2:
            (void)topology::parse_ethtool_features(text);
            break;
        default:
            (void)topology::parse_lldp_neighbors(text, snapshot);
            break;
    }
}

}  // namespace crucible::fuzz::boundary
