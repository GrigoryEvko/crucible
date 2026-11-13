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
    switch (which % 3) {
        case 0: {
            // The output of tc qdisc show is some short lines, so the harness
            // gives the parser at most 4096 characters of it.
            ::fixy::FixedText<4096> shown{};
            while (!cursor.is_empty() && shown.push(cursor.take<char>()).is_ok()) {}
            (void)cntp::parse_tc_qdisc_show(shown.view());
            break;
        }
        case 1: {
            // The product reads at most counter_read_bytes characters of a
            // counter, so the harness gives the parser at most as many of
            // each text.  A character past that bound is dropped.
            ::fixy::FixedText<cntp::detail::roce::counter_read_bytes> rx{};
            ::fixy::FixedText<cntp::detail::roce::counter_read_bytes> tx{};
            bool is_past_split = false;
            while (!cursor.is_empty()) {
                char const character = cursor.take<char>();
                if (!is_past_split && character == '\x1f') {
                    is_past_split = true;
                } else if (is_past_split) {
                    static_cast<void>(tx.push(character).is_ok());
                } else {
                    static_cast<void>(rx.push(character).is_ok());
                }
            }
            (void)cntp::parse_pfc_pause_counters(rx.view(), tx.view());
            break;
        }
        default: {
            // The product reads at most available_cc_capacity characters of
            // the list, so the harness gives the parser at most as many.
            ::fixy::FixedText<cntp::detail::available_cc_capacity> listed{};
            while (!cursor.is_empty() && listed.push(cursor.take<char>()).is_ok()) {}
            (void)cntp::parse_available_congestion_control(listed.view());
            break;
        }
    }
}

}  // namespace crucible::fuzz::boundary
