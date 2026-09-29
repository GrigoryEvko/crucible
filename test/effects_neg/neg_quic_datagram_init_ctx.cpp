#include <crucible/cntp/_wip/QuicTransport.h>
#include <fixy/Ctx.h>

#include <array>
#include <cstddef>

// A datagram is background work.  The startup context owns Init and no Bg,
// so it can build a connection but cannot send on it.

int main() {
    namespace cntp = crucible::cntp::_wip;
    namespace fe = ::foundation::effects;

    ::fixy::ColdInitCtx init{fe::testing::init()};
    auto fd = cntp::admit_socket_fd(3).value();
    cntp::AuthenticatedMtlsPeer peer{};
    auto config =
        cntp::mint_quic_config(cntp::admit_quic_stream_limit(2).value(), cntp::admit_quic_datagram_bytes(1200).value(),
                               cntp::mint_cc_choice<cntp::CcAlgorithm::Bbr3, cntp::LinkClass::CrossDatacenter>());
    auto connection = cntp::mint_quic_connection(init, fd, peer, config);
    std::array<std::byte, 8> payload{};
    auto sent = connection.send_datagram(init, payload);
    (void)sent;
    return 0;
}
