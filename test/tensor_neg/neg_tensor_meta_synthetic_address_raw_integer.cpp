// Negative-compile test.  This file must fail to compile.
//
// mint_synthetic_data_ptr takes a SyntheticAddress and no other type, so a
// raw integer cannot become the data pointer of a tensor.

#include <crucible/TensorMeta.h>

#include <cstdint>

int main() {
    auto const pointer = crucible::mint_synthetic_data_ptr(std::uint64_t{0x1000});
    return pointer.is_some() ? 0 : 1;
}
