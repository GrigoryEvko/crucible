// Negative-compile test. This file must fail to compile.
//
// A ConstSlot reads a lane of a block.  An assignment of one ConstSlot to
// another wrote no lane and changed only the temporary slot, so the write had
// no effect and gave no error.  The copy assignment of a ConstSlot is
// deleted.

#include <crucible/TensorMeta.h>

int main() {
    crucible::TensorMeta const meta{};
    meta.sizes[0] = meta.sizes[1];
    return crucible::raw_tensor_dim(meta.sizes[0]) == 0 ? 0 : 1;
}
