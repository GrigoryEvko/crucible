// A derived tag that declares a permission_row member of its own.
//
// A lookup that read the member before the parent would let a shard
// state the empty row beside a parent that does IO.  A derived tag has
// the row of its parent and no other.

#include <foundation/permissions/Permission.h>

namespace user_code {
struct io_parent {
    using permission_row = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};
struct shard {
    using parent_type = io_parent;
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace user_code

using laundered_row = ::foundation::permissions::permission_row_t<user_code::shard>;

int main() { return 0; }
