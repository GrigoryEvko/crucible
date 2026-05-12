#include <crucible/cntp/QuicTransport.h>

// GAPS-128 fixture #2: QUIC config minting requires a declared
// congestion-control choice. Raw CcSelection cannot enter policy.

int main() {
    namespace cntp = crucible::cntp;

    auto streams = cntp::admit_quic_stream_limit(8).value();
    auto datagram = cntp::admit_quic_datagram_bytes(1200).value();
    cntp::CcSelection raw_cc{};
    auto config = cntp::mint_quic_config(streams, datagram, raw_cc);
    (void)config;
    return 0;
}
