#include <crucible/effects/Capabilities.h>
#include <crucible/fixy/Canopy.h>

// FIXY-V-213 fixture #1: the fixy::canopy::mint_hlc re-export MUST
// preserve the substrate's `effects::Init` parameter-type gate.  The
// Hlc is an init-tier-only primitive — calling the factory with the
// background-tier capability (effects::Bg, minted via the test-
// witness scaffolding so the private ctor is reachable) must fail
// compilation through the fixy:: surface with the same conversion
// diagnostic as the bare substrate call.

int main() {
    // effects::testing::bg() yields a real effects::Bg with a valid
    // ctor path; passing it where effects::Init is expected fires
    // the parameter-type mismatch — that's the gate we're proving.
    auto bg = crucible::effects::testing::bg();
    auto&& clock = crucible::fixy::canopy::mint_hlc(bg);
    (void)clock;
    return 0;
}
