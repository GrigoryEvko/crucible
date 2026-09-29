#include <crucible/cntp/_wip/QuicTransport.h>
#include <fixy/Ctx.h>

// mint_quic_connection is the only door to a connection, so a connection
// exists only where a context that owns Init built it.  The constructor is
// private.

int main() {
    namespace cntp = crucible::cntp::_wip;
    namespace fe = ::foundation::effects;

    ::fixy::ColdInitCtx init{fe::testing::init()};
    auto fd = cntp::admit_socket_fd(3).value();
    cntp::AuthenticatedMtlsPeer peer{};
    auto config =
        cntp::mint_quic_config(cntp::admit_quic_stream_limit(2).value(), cntp::admit_quic_datagram_bytes(1200).value(),
                               cntp::mint_cc_choice<cntp::CcAlgorithm::Bbr3, cntp::LinkClass::CrossDatacenter>());
    auto minted = cntp::mint_quic_connection<4>(init, fd, peer, config);
    (void)minted;
    cntp::QuicConnection<4> forged{fd, peer, config};
    (void)forged;
    return 0;
}
