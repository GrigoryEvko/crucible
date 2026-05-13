#include <crucible/cntp/_wip/QuicTransport.h>

// GAPS-128 fixture #4: QUIC migration consumes a tagged path-swap plan.
// Raw path IDs cannot directly drive connection migration.

int main() {
    namespace cntp = crucible::cntp::_wip;

    auto fd = cntp::admit_socket_fd(3).value();
    cntp::AuthenticatedMtlsPeer peer{cntp::MtlsPeerIdentity{}};
    auto streams = cntp::admit_quic_stream_limit(2).value();
    auto datagram = cntp::admit_quic_datagram_bytes(1200).value();
    auto config = cntp::mint_quic_config(
        streams,
        datagram,
        cntp::mint_cc_choice<cntp::CcAlgorithm::Bbr3,
                             cntp::LinkClass::CrossDatacenter>());
    auto connection = cntp::mint_quic_connection(
        crucible::effects::ColdInitCtx{}, fd, peer, config);
    cntp::PathSwapPlan raw{};
    auto migration = connection.plan_migration(
        crucible::effects::BgDrainCtx{}, raw);
    (void)migration;
    return 0;
}
