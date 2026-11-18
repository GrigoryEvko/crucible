// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Pins IsCacheableFunction concept
// rejection on the federation bridge primitives.
//
// `computation_identity<FnPtr, Row, Args...>` and
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
#include <foundation/effects/Row.h>

#include <cstddef>

namespace eff = ::foundation::effects;

int main() {
    constexpr ::foundation::core::FixedArray<std::byte, 32> key_bytes{};
    const ::crucible::cipher::federation::FederationHashKey hash_key{key_bytes.view()};
    // First template arg is `42` — an integral NTTP.  Not a
    // pointer-to-function, so IsCacheableFunction<42> is false.
    // The federation primitive's requires clause rejects at
    // substitution time.
    (void)::crucible::cipher::federation::federation_key<42, eff::Row<>, int>(hash_key);
    return 0;
}
