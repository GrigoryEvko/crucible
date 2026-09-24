// An edge in permission_rows for a tag that derives its row from a
// parent.
//
// The shard's parent does IO, so the shard does IO.  The relation is
// open, and its edge took precedence over the parent, so an edge for
// the shard with the empty row gave the shard a row lighter than its
// parent's, and its permission could be minted under a context that
// admits no IO.  A derived tag declares no row of its own.

#include <foundation/permissions/Permission.h>

namespace user_code {
struct io_parent {
    using permission_row = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};
struct shard {
    using parent_type = io_parent;
};
}  // namespace user_code

namespace foundation::permissions::permission_rows {
inline constexpr ::foundation::fail_closed::edge<user_code::shard, ::foundation::effects::Row<>> laundered_shard{};
}  // namespace foundation::permissions::permission_rows

using laundered_row = ::foundation::permissions::permission_row_t<user_code::shard>;

int main() { return 0; }
