// A capability source authorizes only its permitted row.  An init
// source never permits Block, so minting a Block capability from it
// fails the CanMintCap constraint.

#include <foundation/effects/Capability.h>

int main() {
    auto init = ::foundation::effects::testing::init();
    [[maybe_unused]] auto blocked = ::foundation::effects::mint_cap<::foundation::effects::Effect::Block>(init);
    return 0;
}
