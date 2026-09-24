// A capability source authorizes only its permitted row.  An init
// source does not permit Bg, so minting a Bg capability from it fails
// the CanMintCap constraint.

#include <foundation/effects/Capability.h>

int main() {
    auto init = ::foundation::effects::testing::init();
    [[maybe_unused]] auto background = ::foundation::effects::mint_cap<::foundation::effects::Effect::Bg>(init);
    return 0;
}
