// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// The foreground hot-path context cannot change probe or witness state.
// A foreground context is built only from the producer claim, so the
// refused call names it in an unevaluated operand.  The size of the call
// is its one use, so the refused call is the only error.

#include <crucible/topology/AsymmetricFailure.h>

#include <utility>

int main() {
    auto detector = crucible::topology::mint_asymmetric_failure_detector<::fixy::ColdInitCtx, 2>(
        ::fixy::ColdInitCtx{::foundation::effects::testing::init()});
    crucible::cog::CogIdentity peer{};
    peer.uuid = crucible::cog::Uuid{0x127, 0x1};

    static_cast<void>(sizeof(detector.record_outbound(std::declval<::fixy::HotFgCtx const&>(), peer, true, 1)));
    return 0;
}
