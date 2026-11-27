// Negative-compile test. This file must fail to compile.
//
// Violation: a bit cast makes a descriptor from 176 bytes.  The bytes would
// give the descriptor a key of an address that no front end gave, and the
// members of the descriptor re-mint an address from its key.  The descriptor
// and the key carry no_start_over_bytes, so the bit cast of the Scalar family
// refuses the descriptor.  Bytes from outside go through decode_tensor_meta.

#include <crucible/TensorMeta.h>
#include <fixy/Core.h>

#include <cstddef>

int main() {
    ::fixy::FixedArray<std::byte, sizeof(crucible::TensorMeta)> const image{};
    crucible::TensorMeta const meta = ::fixy::bit_cast<crucible::TensorMeta>(image);
    return meta.ndim;
}
