// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-F12-AUDIT fixture — pins IsCacheableFunction concept
// rejection on the federation bridge primitives.
//
// `federation_content_hash<FnPtr, Row, Args...>` and
// `federation_key<FnPtr, Row, Args...>` carry
// `requires IsCacheableFunction<FnPtr>` constraints.  Without the
// fence, a caller could pass an integral NTTP, a data-pointer
// global, or a member-function pointer as the FnPtr — every one
// of those would compile to a structurally-distinct cache slot
// the dispatcher cannot redispatch to (no `this` argument
// available; no callable target).
//
// Here we pass an integral NTTP `42` as FnPtr.
// `IsCacheableFunction<42>` is false because `decltype(42)` is
// `int`, not a function-pointer type.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsCacheableFunction<42>.

#include <crucible/cipher/ComputationCacheFederation.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    // First template arg is `42` — an integral NTTP.  Not a
    // pointer-to-function, so IsCacheableFunction<42> is false.
    // The federation primitive's requires clause rejects at
    // substitution time.
    (void)::crucible::cipher::federation::federation_key<
        42, eff::Row<>, int>();
    return 0;
}
