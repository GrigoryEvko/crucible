// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-F12-AUDIT fixture — pins IsEffectRow concept rejection on
// the federation bridge primitives.
//
// `federation_content_hash<FnPtr, Row, Args...>`,
// `federation_row_hash<Row>`, and `federation_key<FnPtr, Row, Args...>`
// all carry `requires IsEffectRow<Row>` constraints.  Without the
// fence, a caller could accidentally pass a non-Row type as the
// `Row` template argument — the row-hash fold's primary template
// would silently return 0, producing a row-blind federation key
// that aliases entries written with `Row<>`.
//
// `int` is not an `effects::Row<Es...>` — IsEffectRow rejects.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsEffectRow<int>.

#include <crucible/cipher/ComputationCacheFederation.h>

inline void f12_test_fn(int) noexcept {}

int main() {
    // Second template arg is `int` — bound to Row.  IsEffectRow<int>
    // is false (int is not effects::Row<Es...>).  The federation
    // primitive's requires clause rejects.
    (void)::crucible::cipher::federation::federation_key<
        &f12_test_fn, int, double>();
    return 0;
}
