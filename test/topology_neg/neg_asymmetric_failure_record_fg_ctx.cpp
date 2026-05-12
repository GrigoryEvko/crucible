#include <crucible/topology/AsymmetricFailure.h>

int main() {
    auto detector = crucible::topology::mint_asymmetric_failure_detector<
        crucible::effects::ColdInitCtx, 2>(
            crucible::effects::ColdInitCtx{});
    crucible::cog::CogIdentity peer{};
    peer.uuid = crucible::cog::Uuid{0x127, 0x1};

    (void)detector.record_outbound(
        crucible::effects::HotFgCtx{}, peer, true, 1);
    return 0;
}
