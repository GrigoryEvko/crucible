// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-172 — the AF_XDP RX trust-laundering boundary has teeth.
//
// `dequeue_rx()` hands out `rx_frame` = Tagged<packet_view,
// source::External> — untrusted wire data.  A consumer that requires
// validated input takes `sanitized_frame` = Tagged<packet_view,
// source::Sanitized>.  source::External and source::Sanitized are
// distinct phantom tags with NO implicit conversion, so an untrusted
// RX frame cannot reach a sanitized-only consumer without first
// passing through `sanitize_rx_frame` (the single External →
// Sanitized retag boundary).  Skipping the launder is a compile error.
//
// Expected diagnostic: could not convert / no matching function /
// Tagged / Sanitized / External.

#include <crucible/cntp/AfXdp.h>

#include <cstddef>

namespace cntp = crucible::cntp;

using Socket = cntp::AfXdpSocket<131'072, 2'048, 64, 64, 64, 64>;

// A consumer that only accepts validated (Sanitized) frames.
void consume_sanitized(Socket::sanitized_frame) {}

int main() {
    std::byte raw[64]{};
    Socket::packet_view view{raw};
    Socket::rx_frame untrusted{view};   // External-tagged (untrusted wire data)

    // ERROR: External is not Sanitized — must launder via sanitize_rx_frame.
    consume_sanitized(untrusted);
    return 0;
}
