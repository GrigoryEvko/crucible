// Negative-compile test. This file must fail to compile.
//
// Violation: the mint of a synthetic data pointer takes the zero address in a
// constant evaluation.  Zero is no address, and the word zero is the empty
// value of an Option of a key, so the precondition of the mint refuses it, and
// a constant gives a compile error.  test_tensor_meta_synthetic_address runs
// a zero address of the run time in a child process.

#include <crucible/TensorMeta.h>

int main() {
    constexpr ::fixy::Option<crucible::TensorDataKey> pointer =
        crucible::mint_synthetic_data_ptr(crucible::SyntheticAddress{0});
    return pointer.is_some() ? 0 : 1;
}
