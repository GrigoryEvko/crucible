// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_restore refuses a payload that cannot move.  A restore checks the
// content hash and then moves the cold value into the warm tier, so the
// payload must be move-constructible.

#include <crucible/cipher/CipherTierPromotion.h>

#include <utility>

struct ImmovablePayload {
    ImmovablePayload() = default;
    ImmovablePayload(const ImmovablePayload&) = delete;
    ImmovablePayload& operator=(const ImmovablePayload&) = delete;
    ImmovablePayload(ImmovablePayload&&) = delete;
    ImmovablePayload& operator=(ImmovablePayload&&) = delete;
};

// The size of the call is its one use, so the refused call is the only
// error.
int main() {
    static_cast<void>(sizeof(::crucible::cipher::mint_restore<ImmovablePayload>(
        std::declval<::fixy::cipher_tier::Cold<ImmovablePayload>>(), std::declval<::crucible::ContentHash>())));
    return 0;
}
