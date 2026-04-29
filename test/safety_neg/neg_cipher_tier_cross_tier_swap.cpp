// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: swap()-ing CipherTier<TIER_A, T> with CipherTier<TIER_B, T>
// when TIER_A != TIER_B.
//
// swap() takes a reference to the SAME class — a member taking
// `CipherTier<Tier, T>&`.  Cross-tier swap is rejected at overload
// resolution.
//
// [GCC-WRAPPER-TEXT] — swap parameter-type mismatch.

#include <crucible/safety/CipherTier.h>
#include <utility>

using namespace crucible::safety;

int main() {
    CipherTier<CipherTierTag_v::Hot,  int> hot_value{42};
    CipherTier<CipherTierTag_v::Cold, int> cold_value{7};

    // Should FAIL: CipherTier<Hot, int>::swap takes
    // CipherTier<Hot, int>&; cold_value is a different type.
    hot_value.swap(cold_value);

    using std::swap;
    swap(hot_value, cold_value);

    return hot_value.peek();
}
