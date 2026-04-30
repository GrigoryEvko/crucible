// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-F11 fixture — pins IsEffectRow concept rejection on
// non-Row template parameters to the row-aware cache API.
//
// Without the IsEffectRow fence, `typename Row` would accept any
// type — including bare types like `int` — and the row-hash
// fold's primary template would silently return 0, producing a
// row-blind cache key that aliases legacy slots.  Worse: the
// caller might intend `lookup_in_row<&fn, int, double>()` where
// `int` is supposed to be the first Arg; without the fence, `int`
// would silently bind to Row, producing a key that accidentally
// matches some other (FnPtr, Args...) tuple.
//
// `int` is not an `effects::Row<Es...>` — IsEffectRow rejects.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsEffectRow<int>.

#include <crucible/cipher/ComputationCache.h>

inline void f11_test_fn(int) noexcept {}

int main() {
    // `int` is the second template arg — bound to Row.  Concept
    // IsEffectRow<int> is false (int is not effects::Row<Es...>).
    // The lookup template's requires clause rejects.
    (void)crucible::cipher::lookup_computation_cache_in_row<
        &f11_test_fn, int, double>();
    return 0;
}
