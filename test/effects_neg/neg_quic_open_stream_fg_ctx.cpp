#include <crucible/cntp/_wip/QuicTransport.h>
#include <fixy/Ctx.h>

// A new stream changes the stream table of the connection, and that is
// background work.  The foreground context owns no Bg, so it cannot open one.

int main() {
    namespace cntp = crucible::cntp::_wip;
    namespace fe = ::foundation::effects;

    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::HotFgCtx fg{fe::testing::foreground()};
    auto fd = cntp::admit_socket_fd(3).value();
    cntp::AuthenticatedMtlsPeer peer{};
    auto config =
        cntp::mint_quic_config(cntp::admit_quic_stream_limit(2).value(), cntp::admit_quic_datagram_bytes(1200).value(),
                               cntp::mint_cc_choice<cntp::CcAlgorithm::Bbr3, cntp::LinkClass::CrossDatacenter>());
    auto connection = cntp::mint_quic_connection(init, fd, peer, config);
    auto stream = connection.open_stream(fg);
    (void)stream;
    return 0;
}
