// Negative-compile test. This file must fail to compile.
//
// Violation: the external tag of a descriptor names a temporary descriptor.
// The tag names the descriptor by reference, and the temporary ends at the end
// of the full expression, so a read of the size of its storage would read a
// dead object.  The overload for an rvalue is deleted.

#include <crucible/TensorMeta.h>

int main() {
    auto const tagged = crucible::external_tensor_meta(crucible::TensorMeta{});
    return tagged.value().ndim;
}
