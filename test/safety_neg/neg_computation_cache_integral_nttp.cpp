// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-F09-AUDIT-2 fixture — pins IsCacheableFunction concept
// rejection on integral-constant NTTPs.  Without the concept fence,
// `auto FnPtr` accepts ANY structural NTTP — including raw integers
// — silently producing nonsense cache slots that hash off integral
// values.
//
// The concept requires `decltype(FnPtr)` to be a pointer-to-function;
// `int` (the type of `42`) trivially fails `is_pointer_v`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsCacheableFunction<42>.

#include <crucible/cipher/ComputationCache.h>

int main() {
    // 42 is an integral NTTP — IsCacheableFunction<42> is false.
    // The lookup template's requires clause must reject this.
    (void)crucible::cipher::lookup_computation_cache<42, int>();
    return 0;
}
