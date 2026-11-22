// Negative-compile test.  This file must fail to compile.
//
// mint_synthetic_data_ptr refuses the zero address, because zero is no
// storage.  A constant evaluation of the mint with zero breaks its
// precondition.

#include <crucible/TensorMeta.h>

int main() {
    constexpr crucible::ExternalDataPtr pointer = crucible::mint_synthetic_data_ptr(crucible::SyntheticAddress{0});
    return pointer.value() == nullptr ? 0 : 1;
}
