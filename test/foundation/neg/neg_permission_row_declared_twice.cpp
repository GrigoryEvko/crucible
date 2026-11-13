// One row has one source.  A tag that declares a row as an edge and
// again as a member is refused when the row is first read, so two
// sources that disagree cannot each answer at different sites.

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
