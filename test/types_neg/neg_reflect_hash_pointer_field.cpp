// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// reflect_hash refuses a pointer field.  An address changes from run to
// run, so a hash of it breaks the bit-exact replay.  A TensorMeta holds the
// data pointer of a tensor in a private member of a Tagged.  The field walk
// does not read the private state of a class that is not plain, so
// for_each_field refuses the Tagged, and the hash never reaches the pointer.

#include <crucible/Reflect.h>
#include <crucible/TensorMeta.h>

int main() {
    crucible::TensorMeta const meta{};
    return static_cast<int>(crucible::reflect_hash(meta) & 1u);
}
