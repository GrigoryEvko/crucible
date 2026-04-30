// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D30 fixture (CipherTier) — pins constrained-extractor.
// cipher_tier_value_t is constrained on `requires is_cipher_tier_v<T>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsCipherTier.h>

int main() {
    using V = crucible::safety::extract::cipher_tier_value_t<int>;
    V const v{};
    (void)v;
    return 0;
}
