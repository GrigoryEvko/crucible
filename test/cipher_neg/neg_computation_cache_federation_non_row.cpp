// Negative compile test.  This file must fail to compile.
//
// Pins IsEffectRow concept rejection on
// the federation bridge primitives.
//
// `computation_identity<FnPtr, Row, Args...>` and
// `federation_key<FnPtr, Row, Args...>` carry `requires IsEffectRow<Row>`
// constraints.  Without the fence, a caller could accidentally pass a
// non-Row type as the `Row` template argument, and the key would hold
// the name of that type in the place of the row.
//
// `int` is not an `effects::Row<Es...>` — IsEffectRow rejects.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsEffectRow<int>.

#include <crucible/cipher/ComputationCacheFederation.h>

#include <cstddef>

inline void cacheable_fn(int) noexcept {}

int main() {
    constexpr ::foundation::core::FixedArray<std::byte, 32> key_bytes{};
    const ::crucible::cipher::federation::FederationHashKey hash_key{key_bytes.view()};
    // Second template arg is `int` — bound to Row.  IsEffectRow<int>
    // is false (int is not effects::Row<Es...>).  The federation
    // primitive's requires clause rejects.
    (void)::crucible::cipher::federation::federation_key<&cacheable_fn, int, double>(hash_key);
    return 0;
}
