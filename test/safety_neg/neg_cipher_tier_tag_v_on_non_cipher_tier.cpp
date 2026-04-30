// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D30 fixture (CipherTier) — pins constrained-extractor.
// cipher_tier_tag_v is constrained on `requires is_cipher_tier_v<T>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsCipherTier.h>

int main() {
    auto t = crucible::safety::extract::cipher_tier_tag_v<int>;
    (void)t;
    return 0;
}
