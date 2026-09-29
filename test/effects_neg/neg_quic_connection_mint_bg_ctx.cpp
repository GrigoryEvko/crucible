#include <crucible/cntp/_wip/QuicTransport.h>
#include <fixy/Ctx.h>

// A connection is built at startup, in a context that owns Init.  The
// background drain context owns Bg and no Init, so the mint refuses it.

int main() {
    namespace cntp = crucible::cntp::_wip;
    namespace fe = ::foundation::effects;

    ::fixy::BgDrainCtx bg{fe::testing::bg()};
    auto fd = cntp::admit_socket_fd(3).value();
    cntp::AuthenticatedMtlsPeer peer{};
    auto config =
        cntp::mint_quic_config(cntp::admit_quic_stream_limit(2).value(), cntp::admit_quic_datagram_bytes(1200).value(),
                               cntp::mint_cc_choice<cntp::CcAlgorithm::Bbr3, cntp::LinkClass::CrossDatacenter>());
    auto connection = cntp::mint_quic_connection(bg, fd, peer, config);
    (void)connection;
    return 0;
}
