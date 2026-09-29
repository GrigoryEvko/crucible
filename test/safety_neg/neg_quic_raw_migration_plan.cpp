#include <crucible/cntp/_wip/QuicTransport.h>
#include <fixy/Ctx.h>

// A migration takes a plan under the PathSwap tag.  A bare PathSwapPlan, even
// one read out of a declared plan, has no conversion to the tagged plan.

int main() {
    namespace cntp = crucible::cntp::_wip;
    namespace fe = ::foundation::effects;

    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgDrainCtx bg{fe::testing::bg()};
    auto fd = cntp::admit_socket_fd(3).value();
    cntp::AuthenticatedMtlsPeer peer{};
    auto config =
        cntp::mint_quic_config(cntp::admit_quic_stream_limit(2).value(), cntp::admit_quic_datagram_bytes(1200).value(),
                               cntp::mint_cc_choice<cntp::CcAlgorithm::Bbr3, cntp::LinkClass::CrossDatacenter>());
    auto connection = cntp::mint_quic_connection(init, fd, peer, config);
    auto declared = cntp::mint_path_swap_plan(cntp::admit_path_id(1).value(), cntp::admit_path_id(2).value(),
                                              cntp::admit_path_id(3).value(), cntp::admit_swap_timeout_ns(9).value());
    cntp::PathSwapPlan raw = declared.value().value();
    auto migration = connection.plan_migration(bg, raw);
    (void)migration;
    return 0;
}
