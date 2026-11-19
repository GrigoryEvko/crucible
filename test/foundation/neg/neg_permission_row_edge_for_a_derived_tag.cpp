// An edge in permission_rows for a tag that derives its row from a
// parent.
//
// The shard's parent does IO, so the shard does IO.  An edge for the shard
// with the empty row would give the shard a row lighter than its parent's,
// and its permission could then be minted under a context that admits no
// IO.  The seal of permission_rows counts its members, so the planted edge
// stops the build at the first read of a row.  The fixture
// neg_permission_row_member_for_a_derived_tag refuses the member form of
// the same claim.

#include <foundation/Quarantine.h>
#include <foundation/permissions/Permission.h>

namespace user_code {
struct io_parent {
    using permission_row = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};
struct shard {
    using parent_type = io_parent;
};
}  // namespace user_code

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
namespace foundation::permissions::permission_rows {
inline constexpr ::foundation::fail_closed::edge<user_code::shard, ::foundation::effects::Row<>> laundered_shard{};
}  // namespace foundation::permissions::permission_rows
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// The requirement is the one use of the row, so the refusal is the only
// error.
static_assert(requires { typename ::foundation::permissions::permission_row_t<user_code::shard>; });

int main() { return 0; }
