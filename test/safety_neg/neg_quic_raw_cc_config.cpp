#include <crucible/cntp/_wip/QuicTransport.h>

// mint_quic_config takes a congestion-control choice under the CcAlgorithm
// tag.  A bare CcSelection has no conversion to the tagged choice.

int main() {
    namespace cntp = crucible::cntp::_wip;

    auto streams = cntp::admit_quic_stream_limit(8).value();
    auto datagram = cntp::admit_quic_datagram_bytes(1200).value();
    cntp::CcSelection raw_cc{};
    auto config = cntp::mint_quic_config(streams, datagram, raw_cc);
    (void)config;
    return 0;
}
