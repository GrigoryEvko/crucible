// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// reflect_hash refuses a pointer field.  An address changes from run to
// run, so a hash of it breaks the bit-exact replay.  The refusal is a
// static_assert in hash_field, with a message that names the reason.
// A TensorMeta holds the data pointer of a tensor in a Tagged, so the walk
// reaches the pointer through the members of the Tagged.

#include <crucible/Reflect.h>
#include <crucible/TensorMeta.h>

int main() {
    crucible::TensorMeta const meta{};
    return static_cast<int>(crucible::reflect_hash(meta) & 1u);
}
