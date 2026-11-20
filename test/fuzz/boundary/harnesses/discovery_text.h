#pragma once

// Text that discovery reads from host tools: the lspci tree, ethtool
// driver info and feature lists, and lldpctl neighbours.  The first byte
// picks the parser and the rest is the text.  Every parser must survive any
// text, and a snapshot that a parser fills keeps its node count in range.

#include "../harness.h"

#include <crucible/topology/Discovery.h>

#include <cstddef>
#include <cstdint>

namespace crucible::fuzz::boundary {

[[nodiscard]] inline SeedList seeds_discovery_text() noexcept {
    constexpr ::fixy::FixedArray<::fixy::TextView, 4> kTexts{
        ::fixy::TextView{"-[0000:00]-+-00.0\n           +-01.0-[01]----00.0\n           \\-02.0\n"},
        ::fixy::TextView{"driver: mlx5_core\nversion: 6.1\nfirmware-version: 22.39.1002\nbus-info: 0000:65:00.0\n"},
        ::fixy::TextView{
            "tcp-segmentation-offload: on\ngeneric-receive-offload: on\nlarge-receive-offload: off [fixed]\n"},
        ::fixy::TextView{"Interface: eth0, via: LLDP\n  SysName: tor-a\n  PortID: ifname swp17\n  LineRate: 100G\n"},
    };
    SeedList seeds = empty_seed_list();
    std::uint8_t which = 0;
    for (const ::fixy::TextView text : kTexts) {
        SeedWriter seed{};
        seed.append_raw(which);
        seed.append_text(text);
        add_seed(seeds, seed.finish());
        ++which;
    }
    return seeds;
}

inline void run_discovery_text(::fixy::View<std::byte const> bytes) noexcept {
    InputCursor cursor{bytes};
    const auto which = cursor.take<std::uint8_t>();
    // The text owns a copy of the rest of the input, so the parsers read
    // characters of the base.
    const ::fixy::HeapText owned = text_of(cursor.rest());
    const auto text = topology::tag_external_discovery_text(owned.view());

    const ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    auto snapshot = topology::mint_discovery_snapshot(init);
    switch (which % 4) {
        case 0:
            (void)topology::parse_lspci_vmm_tree(text, snapshot);
            break;
        case 1: {
            topology::DiscoveryNodeFact local{.kind = cog::CogKind::NicPort};
            snapshot.add_node(local).match(
                [&](std::uint16_t index) { (void)topology::parse_ethtool_info(text, snapshot, index); },
                [](topology::DiscoveryError) {});
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
