// One row has one source.  A tag that declares a row as an edge and
// again as a member would have two sources that can disagree.  The seal of
// permission_rows counts its members, so an edge that another file plants
// stops the build at the first read of a row, before the row is read
// twice.

#include <foundation/Quarantine.h>
#include <foundation/permissions/Permission.h>

namespace {
struct Twice {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
namespace foundation::permissions::permission_rows {
inline constexpr ::foundation::fail_closed::edge<Twice, ::foundation::effects::Row<>> twice{};
}  // namespace foundation::permissions::permission_rows
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

static_assert(::foundation::permissions::has_permission_row(^^Twice));

int main() { return 0; }
