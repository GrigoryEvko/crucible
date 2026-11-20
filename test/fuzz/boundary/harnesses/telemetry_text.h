#pragma once

// Text that telemetry reads from the host: /proc/net/dev counters, the tc
// qdisc backlog, and a sysctl snapshot.  The first byte picks the parser
// and the rest is the text.  Every parser must survive any text.

#include "../harness.h"

#include <crucible/topology/Telemetry.h>

#include <cstddef>
#include <cstdint>

namespace crucible::fuzz::boundary {

[[nodiscard]] inline SeedList seeds_telemetry_text() noexcept {
    constexpr ::fixy::FixedArray<::fixy::TextView, 3> kTexts{
        ::fixy::TextView{"Inter-|   Receive\n face |bytes packets\n  eth0: 100 2 0 0 0 0 0 0 200 4 0 0 0 0 0 0\n"},
        ::fixy::TextView{"qdisc fq 0: root refcnt 2\n backlog 0b 0p requeues 0\n"},
        ::fixy::TextView{
            "net.core.rmem_max = 212992\nnet.core.wmem_max = 212992\nnet.ipv4.tcp_congestion_control = bbr\n"},
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

inline void run_telemetry_text(::fixy::View<std::byte const> bytes) noexcept {
    InputCursor cursor{bytes};
    const auto which = cursor.take<std::uint8_t>();
    // The text owns a copy of the rest of the input, so the parsers read
    // characters of the base.
    const ::fixy::HeapText owned = text_of(cursor.rest());
    const auto text = topology::tag_external_telemetry_text(owned.view());
    switch (which % 3) {
        case 0:
            (void)topology::parse_netdev_counters(text);
            break;
        case 1:
            (void)topology::parse_qdisc_backlog(text);
            break;
        default:
            (void)topology::parse_sysctl_snapshot(text);
            break;
    }
}

}  // namespace crucible::fuzz::boundary
