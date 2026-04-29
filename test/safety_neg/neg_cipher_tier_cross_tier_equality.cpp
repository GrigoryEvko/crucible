// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: comparing CipherTier<TIER_A, T> with CipherTier<TIER_B, T>
// via operator==.
//
// The friend operator== is a non-template friend declared inside
// the class template.  Each (Tier, T) instantiation has its OWN
// friend taking two CipherTier<Tier, T>&.  Cross-tier comparison
// fails to find a viable operator==.
//
// [GCC-WRAPPER-TEXT] — operator== overload-resolution rejection.

#include <crucible/safety/CipherTier.h>

using namespace crucible::safety;

int main() {
    CipherTier<CipherTierTag_v::Hot,  int> hot_value{42};
    CipherTier<CipherTierTag_v::Cold, int> cold_value{42};

    // Should FAIL: operator== for CipherTier<Hot, int> takes two
    // CipherTier<Hot, int>&; cold_value is CipherTier<Cold, int>.
    return static_cast<int>(hot_value == cold_value);
}
