#pragma once

// Text that telemetry reads from the host: /proc/net/dev counters, the tc
// qdisc backlog, and a sysctl snapshot.  The first byte picks the parser
// and the rest is the text.  Every parser must survive any text.

#include "../harness.h"

#include <crucible/topology/Telemetry.h>

namespace crucible::fuzz::boundary {

[[nodiscard]] inline Seeds seeds_telemetry_text() {
    static constexpr std::string_view kTexts[] = {
        "Inter-|   Receive\n face |bytes packets\n  eth0: 100 2 0 0 0 0 0 0 200 4 0 0 0 0 0 0\n",
        "qdisc fq 0: root refcnt 2\n backlog 0b 0p requeues 0\n",
        "net.core.rmem_max = 212992\nnet.core.wmem_max = 212992\nnet.ipv4.tcp_congestion_control = bbr\n",
    };
    Seeds seeds;
    for (std::uint8_t which = 0; which < std::size(kTexts); ++which) {
        std::vector<std::uint8_t> seed{which};
        append_bytes(seed, text_bytes(kTexts[which]));
        seeds.push_back(std::move(seed));
    }
    return seeds;
}

inline void run_telemetry_text(std::span<const std::uint8_t> bytes) {
    ByteCursor cursor{bytes};
    const auto which = cursor.take<std::uint8_t>();
    const auto text = topology::tag_external_telemetry_text(as_text(cursor.rest()));
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
