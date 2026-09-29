#pragma once

// Text that the transport reads from the host: tc qdisc output, the PFC
// pause counters of ethtool -S, and the list of congestion-control modules.
// The first byte picks the parser and the rest is the text.  For the pause
// counters, byte 0x1F splits the receive text from the transmit text.
// Every parser must survive any text.

#include "../harness.h"

#include <crucible/cntp/CongestionControl.h>
#include <crucible/cntp/Pacing.h>
#include <crucible/cntp/RoceConfig.h>

namespace crucible::fuzz::boundary {

[[nodiscard]] inline Seeds seeds_cntp_text() {
    static constexpr std::string_view kTexts[] = {
        "qdisc fq 8001: root refcnt 2 limit 10000p flow_limit 100p buckets 1024\n",
        "rx_prio3_pause: 5\n\x1ftx_prio3_pause: 7\n",
        "reno cubic bbr",
    };
    Seeds seeds;
    for (std::uint8_t which = 0; which < std::size(kTexts); ++which) {
        std::vector<std::uint8_t> seed{which};
        append_bytes(seed, text_bytes(kTexts[which]));
        seeds.push_back(std::move(seed));
    }
    return seeds;
}

inline void run_cntp_text(std::span<const std::uint8_t> bytes) {
    ByteCursor cursor{bytes};
    const auto which = cursor.take<std::uint8_t>();
    const std::string_view text = as_text(cursor.rest());
    switch (which % 3) {
        case 0:
            (void)cntp::parse_tc_qdisc_show(text);
            break;
        case 1: {
            const std::size_t split = text.find('\x1f');
            const std::string_view rx = text.substr(0, split);
            const std::string_view tx = split == std::string_view::npos ? std::string_view{} : text.substr(split + 1);
            (void)cntp::parse_pfc_pause_counters(rx, tx);
            break;
        }
        default:
            (void)cntp::parse_available_congestion_control(text);
            break;
    }
}

}  // namespace crucible::fuzz::boundary
