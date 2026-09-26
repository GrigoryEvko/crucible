// An RX frame is External-tagged wire data.  A consumer that takes only a
// sanitized frame refuses it, because External and Sanitized are different
// tags with no conversion between them.  The only way across is
// sanitize_rx_frame, which retags along the one admitted edge.

#include <crucible/cntp/AfXdp.h>

#include <cstddef>

namespace cntp = crucible::cntp;

using Socket = cntp::AfXdpSocket<131072, 2048, 64, 64, 64, 64>;

namespace {
void consume_sanitized(Socket::sanitized_frame) {}
}  // namespace

int main() {
    std::byte raw[64]{};
    Socket::packet_view view{raw};
    auto untrusted = ::fixy::mint_tagged<::fixy::tags::source::External>(view);
    consume_sanitized(untrusted);
    return 0;
}
