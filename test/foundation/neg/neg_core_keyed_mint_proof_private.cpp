// A unit of the base that is no friend of the door of KeyedMint.h calls its
// member for the proof of a brand that it names, so it could build a keyed
// owner of that brand outside a mint.  mint_keyed_owner is the one friend of
// the door, so its member is private here.

#include <foundation/core/KeyedMint.h>

struct NamedBrand final {};

int main() {
    auto const proof = foundation::core::KeyedMintFrameKeyDoor::proof_<NamedBrand>();
    static_cast<void>(proof);
    return 0;
}
