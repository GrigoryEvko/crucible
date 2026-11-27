// Negative-compile test. This file must fail to compile.
//
// Violation: the mint of a synthetic data pointer takes an enumeration of
// another type with the same base type.  Only a SyntheticAddress names the
// address of a storage of a test or a bench, so the constraint of the mint
// refuses each other type.  The companion fixture
// neg_tensor_meta_synthetic_address_raw_integer gives a raw integer.

#include <crucible/TensorMeta.h>

#include <cstdint>

namespace {
enum class OtherAddress : std::uint64_t {
};
}  // namespace

int main() {
    auto const pointer = crucible::mint_synthetic_data_ptr(OtherAddress{0x1000});
    return pointer.is_some() ? 0 : 1;
}
