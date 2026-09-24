// One row has one source.  A tag that declares a row as an edge and
// again as a member is refused when the row is first read, so two
// sources that disagree cannot each answer at different sites.

#include <foundation/permissions/Permission.h>

namespace {
struct Twice {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

namespace foundation::permissions::permission_rows {
inline constexpr ::foundation::fail_closed::edge<Twice, ::foundation::effects::Row<>> twice{};
}  // namespace foundation::permissions::permission_rows

static_assert(::foundation::permissions::has_permission_row_v<Twice>);

int main() { return 0; }
