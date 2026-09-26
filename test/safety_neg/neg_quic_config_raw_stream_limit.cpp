#include <crucible/cntp/_wip/QuicTransport.h>

// mint_quic_config takes an admitted stream limit.  A raw integer has no
// conversion to PositiveQuicStreamLimit, so a zero limit cannot reach a
// config.

int main() {
    namespace cntp = crucible::cntp::_wip;

    auto datagram = cntp::admit_quic_datagram_bytes(1200).value();
    auto config = cntp::mint_quic_config(std::uint16_t{0}, datagram,
                                         cntp::mint_cc_choice<cntp::CcAlgorithm::Bbr3, cntp::LinkClass::CrossDatacenter>());
    (void)config;
    return 0;
}
