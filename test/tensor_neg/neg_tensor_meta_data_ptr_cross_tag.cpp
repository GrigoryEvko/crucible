// Negative-compile test. This file must fail to compile.
//
// Violation: the key of an address of another owner goes into the data
// pointer of a descriptor.  The members of the owner of a key re-mint its
// address, so a key of another owner would let that owner reach the bytes
// of a tensor.  Two owners make two types of the one template, and no
// conversion joins them.
//
// The companion fixture neg_tensor_meta_data_ptr_raw_pointer gives a raw
// pointer.

#include <crucible/TensorMeta.h>
#include <fixy/Core.h>

namespace {
struct OtherOwner {};
}  // namespace

int main() {
    unsigned char byte = 0;
    ::fixy::Option<::fixy::ForeignAddressKey<OtherOwner>> const other =
        ::fixy::Option<::fixy::ForeignAddressKey<OtherOwner>>::some(::fixy::mint_foreign_address_key<OtherOwner>(
            ::fixy::mint_foreign_run_address(&byte).expect("a byte of the stack is not null")));
    crucible::TensorMeta meta{};
    meta.data_ptr = other;
    return 0;
}
