// Negative-compile test. This file must fail to compile.
//
// Violation: a raw pointer goes into the data pointer of a descriptor.  The
// field holds the key of an address, which only external_data_ptr gives from
// a pointer of the front end, so no assignment takes a raw pointer.
//
// The companion fixture neg_tensor_meta_data_ptr_cross_tag gives the key of
// another owner.

#include <crucible/TensorMeta.h>
#include <foundation/Quarantine.h>

int main() {
    crucible::TensorMeta meta{};
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture gives a raw pointer to a typed door, and the door refuses it")
    void* raw = nullptr;
    meta.data_ptr = raw;
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    return 0;
}
